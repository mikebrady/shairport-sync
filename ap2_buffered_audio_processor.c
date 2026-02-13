/*
 * AirPlay 2 Buffered Audio Processor. This file is part of Shairport Sync
 * Copyright (c) Mike Brady 2025
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "ap2_buffered_audio_processor.h"
#include "common.h"
#include "player.h"
#include "rtp.h"
#include "utilities/buffered_read.h"
#include "utilities/mod23.h"
#include <sodium.h>
#include <stdint.h>

#ifdef CONFIG_CONVOLUTION
#include "FFTConvolver/convolver.h"
#endif

void addADTStoPacket(uint8_t *packet, int packetLen, int rate, int channel_configuration) {
  // https://stackoverflow.com/questions/18862715/how-to-generate-the-aac-adts-elementary-stream-with-android-mediacodec
  // with thanks!

  // See https://wiki.multimedia.cx/index.php/Understanding_AAC
  // see also https://wiki.multimedia.cx/index.php/ADTS for the ADTS layout
  // see https://wiki.multimedia.cx/index.php/MPEG-4_Audio#Sampling_Frequencies for sampling
  // frequencies

  /**
   *  Add ADTS header at the beginning of each and every AAC packet.
   *  This is needed as the packet is raw AAC data.
   *
   *  Note the packetLen must count in the ADTS header itself.
   **/

  int profile = 2;
  int freqIdx = 4;
  if (rate == 44100)
    freqIdx = 4;
  else if (rate == 48000)
    freqIdx = 3;
  else
    debug(1, "Unsupported AAC sample rate %d.", rate);

  // Channel Configuration
  // https://wiki.multimedia.cx/index.php/MPEG-4_Audio#Channel_Configurations
  // clang-format off
  // 0: Defined in AOT Specifc Config
  // 1: 1 channel: front-center
  // 2: 2 channels: front-left, front-right
  // 3: 3 channels: front-center, front-left, front-right
  // 4: 4 channels: front-center, front-left, front-right, back-center
  // 5: 5 channels: front-center, front-left, front-right, back-left, back-right
  // 6: 6 channels: front-center, front-left, front-right, back-left, back-right, LFE-channel
  // 7: 8 channels: front-center, front-left, front-right, side-left, side-right, back-left, back-right, LFE-channel
  // 8-15: Reserved
  // clang-format on

  int chanCfg = channel_configuration; // CPE

  // fill in ADTS data
  packet[0] = 0xFF;
  packet[1] = 0xF9;
  packet[2] = ((profile - 1) << 6) + (freqIdx << 2) + (chanCfg >> 2);
  packet[3] = ((chanCfg & 3) << 6) + (packetLen >> 11);
  packet[4] = (packetLen & 0x7FF) >> 3;
  packet[5] = ((packetLen & 7) << 5) + 0x1F;
  packet[6] = 0xFC;
}

void rtp_buffered_audio_cleanup_handler(__attribute__((unused)) void *arg) {
  debug(2, "Buffered Audio Receiver Cleanup Start.");
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  close(conn->buffered_audio_socket);
  debug(3, "Connection %d: closing TCP Buffered Audio port: %u.", conn->connection_number,
        conn->local_buffered_audio_port);
  conn->buffered_audio_socket = 0;
  debug(2, "Connection %d: rtp_buffered_audio_processor exit.", conn->connection_number);
}

void *rtp_buffered_audio_processor(void *arg) {
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  // #include <syscall.h>
  // debug(1, "Connection %d: rtp_buffered_audio_processor PID %d start", conn->connection_number,
  //         syscall(SYS_gettid));
  conn->incoming_ssrc = 0; // reset
  conn->resampler_ssrc = 0;

  // turn off all flush requests that might have been pending in the connection. Not sure if this is
  // right...
  unsigned int fr = 0;
  for (fr = 0; fr < MAX_DEFERRED_FLUSH_REQUESTS; fr++) {
    conn->ap2_deferred_flush_requests[fr].inUse = 0;
    conn->ap2_deferred_flush_requests[fr].active = 0;
  }
  conn->ap2_immediate_flush_requested = 0;

  pthread_cleanup_push(rtp_buffered_audio_cleanup_handler, arg);

  pthread_t *buffered_reader_thread = malloc(sizeof(pthread_t));
  if (buffered_reader_thread == NULL)
    debug(1, "cannot allocate a buffered_reader_thread!");
  memset(buffered_reader_thread, 0, sizeof(pthread_t));
  pthread_cleanup_push(malloc_cleanup, &buffered_reader_thread);

  buffered_tcp_desc *buffered_audio = malloc(sizeof(buffered_tcp_desc));
  if (buffered_audio == NULL)
    debug(1, "cannot allocate a buffered_tcp_desc!");
  // initialise the

  memset(buffered_audio, 0, sizeof(buffered_tcp_desc));
  pthread_cleanup_push(malloc_cleanup, &buffered_audio);

  if (pthread_mutex_init(&buffered_audio->mutex, NULL))
    debug(1, "Connection %d: error %d initialising buffered_audio mutex.", conn->connection_number,
          errno);
  pthread_cleanup_push(mutex_cleanup, &buffered_audio->mutex);

  if (pthread_cond_init(&buffered_audio->not_empty_cv, NULL))
    die("Connection %d: error %d initialising not_empty cv.", conn->connection_number, errno);
  pthread_cleanup_push(cv_cleanup, &buffered_audio->not_empty_cv);

  if (pthread_cond_init(&buffered_audio->not_full_cv, NULL))
    die("Connection %d: error %d initialising not_full cv.", conn->connection_number, errno);
  pthread_cleanup_push(cv_cleanup, &buffered_audio->not_full_cv);

  // initialise the buffer data structure
  buffered_audio->buffer_max_size = conn->ap2_audio_buffer_size;
  buffered_audio->buffer = malloc(conn->ap2_audio_buffer_size);
  if (buffered_audio->buffer == NULL)
    debug(1, "cannot allocate an audio buffer of %zu bytes!", buffered_audio->buffer_max_size);
  pthread_cleanup_push(malloc_cleanup, &buffered_audio->buffer);

  // pthread_mutex_lock(&conn->buffered_audio_mutex);
  buffered_audio->toq = buffered_audio->buffer;
  buffered_audio->eoq = buffered_audio->buffer;

  buffered_audio->sock_fd = conn->buffered_audio_socket;

  named_pthread_create(buffered_reader_thread, NULL, &buffered_tcp_reader, buffered_audio,
                       "ap2_buf_rdr_%d", conn->connection_number);
  pthread_cleanup_push(thread_cleanup, buffered_reader_thread);

  const size_t leading_free_space_length =
      256; // leave this many bytes free to make room for prefixes that might be added later
  uint8_t packet[32 * 1024];
  unsigned char m[32 * 1024 + leading_free_space_length];

  unsigned char *payload_pointer = NULL;
  unsigned long long payload_length = 0;
  uint32_t payload_ssrc =
      SSRC_NONE; // this is the SSRC of the payload, needed to decide if it should be muted
  uint32_t previous_ssrc = SSRC_NONE;

  uint32_t seq_no =
      0; // audio packet number. Initialised to avoid a "possibly uninitialised" warning.
  uint32_t previous_seqno = 0;
  uint16_t sequence_number_for_player = 0;

  uint32_t timestamp = 0; // initialised to avoid a "possibly uninitialised" warning.
  uint32_t previous_timestamp = 0;

  uint32_t expected_timestamp = 0;
  uint64_t previous_buffer_should_be_time = 0;

  ssize_t nread;

  int new_audio_block_needed = 0; // goes true when a block is needed, false one is read in, but
                                  // will be made true by flushing or by playing the block
  int finished = 0;

  uint64_t blocks_read_since_play_began = 0;
  uint64_t blocks_read = 0;

  int ap2_immediate_flush_requested = 0; // for diagnostics, probably

  uint32_t first_timestamp_in_this_sequence = 0;
  int packets_played_in_this_sequence = 0;

  int play_enabled = 0;
  // double requested_lead_time = 0.0; // normal lead time minimum -- maybe  it should be about 0.1

  // wait until our timing information is valid
  while (have_ptp_timing_information(conn) == 0)
    usleep(1000);

  reset_buffer(conn); // in case there is any garbage in the player

  do {

    if ((play_enabled == 0) && (conn->ap2_play_enabled != 0)) {
      // play newly started
      debug(2, "Play started.");
      new_audio_block_needed = 1;
      blocks_read_since_play_began = 0;
    }

    if ((play_enabled != 0) && (conn->ap2_play_enabled == 0)) {
      debug(2, "Play stopped.");
      packets_played_in_this_sequence = 0; // not all blocks read are played...
#ifdef CONFIG_CONVOLUTION
      convolver_clear_state();
#endif
      reset_buffer(conn); // stop play ASAP
    }

    play_enabled = conn->ap2_play_enabled;

    // now, if get_next_block is non-zero, read a block. We may flush or use it

    if (new_audio_block_needed != 0) {
      // a block is preceded by its length in a uint16_t
      uint16_t data_len;
      // here we read from the buffer that our thread has been reading

      size_t bytes_remaining_in_buffer;
      nread =
          read_sized_block(buffered_audio, &data_len, sizeof(data_len), &bytes_remaining_in_buffer);
      data_len = ntohs(data_len);

      // diagnostic
      if ((conn->ap2_audio_buffer_minimum_size < 0) ||
          (bytes_remaining_in_buffer < (size_t)conn->ap2_audio_buffer_minimum_size))
        conn->ap2_audio_buffer_minimum_size = bytes_remaining_in_buffer;

      if (nread > 0) {
        // get the block itself
        // debug(1,"buffered audio packet of size %u detected.", data_len - 2);
        nread = read_sized_block(buffered_audio, packet, data_len - 2, &bytes_remaining_in_buffer);

        // diagnostic
        if ((conn->ap2_audio_buffer_minimum_size < 0) ||
            (bytes_remaining_in_buffer < (size_t)conn->ap2_audio_buffer_minimum_size))
          conn->ap2_audio_buffer_minimum_size = bytes_remaining_in_buffer;
        // debug(1, "buffered audio packet of size %u received.", nread);

        if (nread > 0) {
          // got the block
          blocks_read++;                  // note, this doesn't mean they are valid audio blocks
          blocks_read_since_play_began++; // 1 means previous seq_no and timestamps are invalid

          // get the sequence number
          // see https://en.wikipedia.org/wiki/Real-time_Transport_Protocol#Packet_header
          // the Marker bit is always set, and it and the remaining 23 bits form the sequence number

          previous_seqno = seq_no;
          seq_no = nctohl(&packet[0]) & 0x7FFFFF;

          previous_timestamp = timestamp;
          timestamp = nctohl(&packet[4]);

          if (payload_ssrc != SSRC_NONE)
            previous_ssrc = payload_ssrc;
          payload_ssrc = nctohl(&packet[8]);
          
          

          if ((payload_ssrc != previous_ssrc) && (payload_ssrc != SSRC_NONE)) {
            if (ssrc_is_recognised(payload_ssrc) == 0) {
              debug(2, "Unrecognised SSRC: %u.", payload_ssrc);
            } else {
              debug(2, "Connection %d: incoming audio encoding is%s \"%s\".",
                    conn->connection_number, previous_ssrc == SSRC_NONE ? "" : " switching to", get_ssrc_name(payload_ssrc));
            }
          }

          if ((payload_ssrc != previous_ssrc) && (ssrc_is_recognised(payload_ssrc) == 0)) {
              debug(2, "Unrecognised SSRC: %u.", payload_ssrc);
          }

          if (blocks_read_since_play_began == 1) {
            debug(2, "Preparing initial decoding chain for %s.", get_ssrc_name(payload_ssrc));
            prepare_decoding_chain(conn, payload_ssrc); // needed to set the input rate...
            sequence_number_for_player =
                seq_no & 0xffff; // this is arbitrary -- the sequence_number_for_player numbers will
                                 // be sequential irrespective of seq_no jumps...
          }

          if (blocks_read_since_play_began > 1) {

            uint32_t t_expected_seqno = (previous_seqno + 1) & 0x7fffff;
            if (t_expected_seqno != seq_no) {
              debug(2,
                    "reading block %u, the sequence number differs from the expected sequence "
                    "number %u. The previous sequence number was %u",
                    seq_no, t_expected_seqno, previous_seqno);
            }
            uint32_t t_expected_timestamp =
                previous_timestamp + get_ssrc_block_length(previous_ssrc);
            int32_t diff = timestamp - t_expected_timestamp;
            if (diff != 0) {
              debug(2, "reading block %u, the timestamp %u differs from expected_timestamp %u.",
                    seq_no, timestamp, t_expected_timestamp);
            }
          }
          new_audio_block_needed = 0; // block has been read.
        }
      }

      if (nread == 0) {
        // nread is 0 -- the port has been closed
        debug(2, "Connection %d: buffered audio port closed!", conn->connection_number);
        finished = 1;
      } else if (nread < 0) {
        char errorstring[1024];
        strerror_r(errno, (char *)errorstring, sizeof(errorstring));
        debug(1, "error in rtp_buffered_audio_processor %d: \"%s\". Could not recv a data_len .",
              errno, errorstring);
        finished = 1;
      }
    }

    if (finished == 0) {
      pthread_cleanup_debug_mutex_lock(&conn->flush_mutex, 25000,
                                       1); // 25 ms is a long time to wait!
      if (blocks_read != 0) {
        if (conn->ap2_immediate_flush_requested != 0) {
          if (ap2_immediate_flush_requested == 0) {
            debug(2, "immediate flush started at sequence number %u until sequence number of %u.",
                  seq_no, conn->ap2_immediate_flush_until_sequence_number);
          }
          if ((blocks_read != 0) && ((a_minus_b_mod23(seq_no, conn->ap2_immediate_flush_until_sequence_number) > 0))) {
            debug(1, "immediate flush may have escaped its endpoint! Seq_no is %u, conn->ap2_immediate_flush_until_sequence_number is %u.", seq_no, conn->ap2_immediate_flush_until_sequence_number);
          }
          
          if ((blocks_read != 0) && ((a_minus_b_mod23(seq_no, conn->ap2_immediate_flush_until_sequence_number) >= 0))) {
            debug(2, "immediate flush completed at seq_no: %u, conn->ap2_immediate_flush_until_sequence_number: %u.", seq_no, conn->ap2_immediate_flush_until_sequence_number);

            conn->ap2_immediate_flush_requested = 0;
            ap2_immediate_flush_requested = 0;

            
            // turn off all deferred requests. Not sure if this is right...
            unsigned int f = 0;
            for (f = 0; f < MAX_DEFERRED_FLUSH_REQUESTS; f++) {
              if ((conn->ap2_deferred_flush_requests[f].inUse != 0) && (conn->ap2_deferred_flush_requests[f].active = 0)) {
                debug(1,
                  "deferred flush cancelled by an immediate flush:  flushFromTS: %12u, flushFromSeq: %12u, "
                  "flushUntilTS: %12u, flushUntilSeq: %12u, timestamp: %12u.",
                  conn->ap2_deferred_flush_requests[f].flushFromTS,
                  conn->ap2_deferred_flush_requests[f].flushFromSeq,
                  conn->ap2_deferred_flush_requests[f].flushUntilTS,
                  conn->ap2_deferred_flush_requests[f].flushUntilSeq, timestamp);
              }
              conn->ap2_deferred_flush_requests[f].inUse = 0;
              conn->ap2_deferred_flush_requests[f].active = 0;
            }


          } else {
            debug(3, "immediate flush of block %u until block %u", seq_no,
                  conn->ap2_immediate_flush_until_sequence_number);
            ap2_immediate_flush_requested = 1;
            new_audio_block_needed = 1; //
          }
        }
      }

      // now, even if an immediate flush has been requested and is active, we still need to process
      // deferred flush requests as they may refer to sequences that are going to be purged anyway

      unsigned int f = 0;
      for (f = 0; f < MAX_DEFERRED_FLUSH_REQUESTS; f++) {
        if (conn->ap2_deferred_flush_requests[f].inUse != 0) {
          if ((conn->ap2_deferred_flush_requests[f].flushFromSeq == seq_no) &&
              (conn->ap2_deferred_flush_requests[f].flushUntilSeq != seq_no)) {
            debug(2,
                  "deferred flush activated:  flushFromTS: %12u, flushFromSeq: %12u, "
                  "flushUntilTS: %12u, flushUntilSeq: %12u, timestamp: %12u.",
                  conn->ap2_deferred_flush_requests[f].flushFromTS,
                  conn->ap2_deferred_flush_requests[f].flushFromSeq,
                  conn->ap2_deferred_flush_requests[f].flushUntilTS,
                  conn->ap2_deferred_flush_requests[f].flushUntilSeq, timestamp);
            conn->ap2_deferred_flush_requests[f].active = 1;
            new_audio_block_needed = 1;
          }
          if (conn->ap2_deferred_flush_requests[f].flushUntilSeq == seq_no) {
            debug(2,
                  "deferred flush terminated: flushFromTS: %12u, flushFromSeq: %12u, "
                  "flushUntilTS: %12u, flushUntilSeq: %12u, timestamp: %12u.",
                  conn->ap2_deferred_flush_requests[f].flushFromTS,
                  conn->ap2_deferred_flush_requests[f].flushFromSeq,
                  conn->ap2_deferred_flush_requests[f].flushUntilTS,
                  conn->ap2_deferred_flush_requests[f].flushUntilSeq, timestamp);
            conn->ap2_deferred_flush_requests[f].active = 0;
            conn->ap2_deferred_flush_requests[f].inUse = 0;
          } else if (a_minus_b_mod23(seq_no, conn->ap2_deferred_flush_requests[f].flushUntilSeq) >
                     0) {
            // now, do a modulo 2^23 unsigned int calculation to see if we may have overshot the
            // flushUntilSeq
            debug(2,
                  "deferred flush terminated due to overshoot at block %u: flushFromTS: %12u, "
                  "flushFromSeq: %12u, "
                  "flushUntilTS: %12u, flushUntilSeq: %12u, timestamp: %12u.",
                  seq_no, conn->ap2_deferred_flush_requests[f].flushFromTS,
                  conn->ap2_deferred_flush_requests[f].flushFromSeq,
                  conn->ap2_deferred_flush_requests[f].flushUntilTS,
                  conn->ap2_deferred_flush_requests[f].flushUntilSeq, timestamp);
            conn->ap2_deferred_flush_requests[f].active = 0;
            conn->ap2_deferred_flush_requests[f].inUse = 0;
            debug(2, "immediate flush was %s.", ap2_immediate_flush_requested == 0 ? "off" : "on");
          } else if (conn->ap2_deferred_flush_requests[f].active != 0) {
            new_audio_block_needed = 1;
            debug(3,
                  "deferred flush of block: %u. flushFromTS: %12u, flushFromSeq: %12u, "
                  "flushUntilTS: %12u, flushUntilSeq: %12u, timestamp: %12u.",
                  seq_no, conn->ap2_deferred_flush_requests[f].flushFromTS,
                  conn->ap2_deferred_flush_requests[f].flushFromSeq,
                  conn->ap2_deferred_flush_requests[f].flushUntilTS,
                  conn->ap2_deferred_flush_requests[f].flushUntilSeq, timestamp);
          }
        }
      }
      pthread_cleanup_pop(1); // the mutex

      // now, if the block is not invalidated by the flush code, see if we need
      // to decode it and pass it to the player
      if (new_audio_block_needed == 0) {
        // is there space in the player thread's buffer system?
        size_t player_buffer_occupancy = get_audio_buffer_occupancy(conn);
        // debug(1,"player buffer size and occupancy: %u and %u", player_buffer_size,
        // player_buffer_occupancy);

        // If we are playing and there is room in the player buffer, go ahead and decode the block
        // and send it to the player. Otherwise, keep the block and sleep for a while.
        if ((play_enabled != 0) &&
            (((1.0 * player_buffer_occupancy * conn->frames_per_packet) / conn->input_rate) <=
             config.audio_decoded_buffer_desired_length)) {
          uint64_t buffer_should_be_time;
          frame_to_local_time(timestamp, &buffer_should_be_time, conn);

          // try to identify blocks that are timed to before the last buffer, and drop 'em
          int64_t time_from_last_buffer_time =
              buffer_should_be_time - previous_buffer_should_be_time;

          if ((packets_played_in_this_sequence == 0) || (time_from_last_buffer_time > 0)) {
            int64_t lead_time = buffer_should_be_time - get_absolute_time_in_ns();
            payload_length = 0;
            if (ssrc_is_recognised(payload_ssrc) != 0) {
              // prepare_decoding_chain(conn, payload_ssrc);
              unsigned long long new_payload_length = 0;
              payload_pointer = m + leading_free_space_length;
              if ((lead_time < (int64_t)30000000000L) &&
                  (lead_time >= 0)) { // only decipher the packet if it's not too late or too early
                int response = -1;    // guess that there is a problem
                if (conn->session_key != NULL) {
                  unsigned char nonce[12];
                  memset(nonce, 0, sizeof(nonce));
                  memcpy(
                      nonce + 4, packet + nread - 8,
                      8); // front-pad the 8-byte nonce received to get the 12-byte nonce expected

                  // https://libsodium.gitbook.io/doc/secret-key_cryptography/aead/chacha20-poly1305/ietf_chacha20-poly1305_construction
                  // Note: the eight-byte nonce must be front-padded out to 12 bytes.

                  // Leave leading_free_space_length bytes at the start for possible headers like an
                  // ADTS header (7 bytes)
                  memset(m, 0, leading_free_space_length);
                  response = crypto_aead_chacha20poly1305_ietf_decrypt(
                      payload_pointer,     // where the decrypted payload will start
                      &new_payload_length, // mlen_p
                      NULL,                // nsec,
                      packet +
                          12, // the ciphertext starts 12 bytes in and is followed by the MAC tag,
                      nread - (8 + 12), // clen -- the last 8 bytes are the nonce
                      packet + 4,       // authenticated additional data
                      8,                // authenticated additional data length
                      nonce,
                      conn->session_key); // *k
                  if (response != 0)
                    debug(1, "Error decrypting audio packet %u -- packet length %zd.", seq_no,
                          nread);
                } else {
                  debug(2, "No session key, so the audio packet can not be deciphered -- skipped.");
                }

                if ((response == 0) && (new_payload_length > 0)) {
                  // now we have the deciphered block, so send it to the player if we can
                  payload_length = new_payload_length;

                  if (ssrc_is_aac(payload_ssrc)) {
                    payload_pointer =
                        payload_pointer - 7; // including the 7-byte leader for the ADTS
                    payload_length = payload_length + 7;

                    // now, fill in the 7-byte ADTS information, which seems to be needed by the
                    // decoder we made room for it in the front of the buffer by filling from m + 7.
                    int channelConfiguration = 2; // 2: 2 channels: front-left, front-right
                    if (payload_ssrc == AAC_48000_F24_5P1)
                      channelConfiguration = 6; // 6: 6 channels: front-center, front-left,
                                                // front-right, back-left, back-right, LFE-channel
                    else if (payload_ssrc == AAC_48000_F24_7P1)
                      channelConfiguration =
                          7; // 7: 8 channels: front-center, front-left, front-right,
                             // side-left, side-right, back-left, back-right, LFE-channel
                    addADTStoPacket(payload_pointer, payload_length, conn->input_rate,
                                    channelConfiguration);
                  }
                  int mute =
                      ((packets_played_in_this_sequence == 0) && (ssrc_is_aac(payload_ssrc)));
                  if (mute) {
                    debug(2, "Connection %d: muting first AAC block -- block %u -- timestamp %u.",
                          conn->connection_number, seq_no, timestamp);
                  }
                  int32_t timestamp_difference = 0;
                  if (packets_played_in_this_sequence == 0) {
                    // first_block_in_this_sequence = seq_no;
                    first_timestamp_in_this_sequence = timestamp;
                    debug(2,
                          "Connection %d: "
                          "first block %u, first timestamp %u.",
                          conn->connection_number, seq_no, timestamp);
                  } else {
                    timestamp_difference = timestamp - expected_timestamp;
                    if (timestamp_difference != 0) {
                      debug(2,
                            "Connection %d: "
                            "unexpected timestamp in block %u. Actual: %u, expected: %u "
                            "difference: %d, "
                            "%f ms. "
                            "Positive means later, i.e. a gap. First timestamp was %u, payload "
                            "type: \"%s\".",
                            conn->connection_number, seq_no, timestamp, expected_timestamp,
                            timestamp_difference, 1000.0 * timestamp_difference / conn->input_rate,
                            first_timestamp_in_this_sequence, get_ssrc_name(payload_ssrc));
                      // mute the first packet after a discontinuity
                      if (ssrc_is_aac(payload_ssrc)) {
                        debug(2,
                              "Connection %d: muting first AAC block -- block %u -- following a "
                              "timestamp discontinuity, timestamp %u.",
                              conn->connection_number, seq_no, timestamp);
                        mute = 1;
                      }
                    }
                  }
                  int skip_this_block = 0;
                  if (timestamp_difference < 0) {

                    // uncomment this to work back to replace buffers that have been already decoded
                    // and placed in the player queue with the incoming new buffers this is a bit
                    // trickier, but maybe the new buffers are better than the previous ones they
                    // will replace (?)
                    /*
                    seq_t revised_seqno = get_revised_seqno(conn, timestamp);
                    if (revised_seqno != sequence_number_for_player) {
                      debug(1, "revised seqno calculated: conn->ab_read: %u, revised_seqno: %u,
                    conn->ab_write: %u.", conn->ab_read, revised_seqno, conn->ab_write);
                      clear_buffers_from(conn, revised_seqno);
                      sequence_number_for_player = revised_seqno;
                      timestamp_difference = 0;
                    }
                    */

                    // uncomment this to drop incoming new buffers that are too old and for whose
                    // timings buffers have already been decoded and placed in the player queue this
                    // is easier, but maybe the new late buffers are better than the previous ones
                    // (?)

                    int32_t abs_timestamp_difference = -timestamp_difference;
                    if ((size_t)abs_timestamp_difference > get_ssrc_block_length(payload_ssrc)) {
                      skip_this_block = 1;
                      debug(2,
                            "skipping block %u because it is too old. Timestamp "
                            "difference: %d, length of block: %zu.",
                            seq_no, timestamp_difference, get_ssrc_block_length(payload_ssrc));
                    }
                  }
                  if (skip_this_block == 0) {
                    uint32_t packet_size = player_put_packet(
                        payload_ssrc, sequence_number_for_player, timestamp, payload_pointer,
                        payload_length, mute, timestamp_difference, conn);
                    debug(3, "block %u, timestamp %u, length %u sent to the player.", seq_no,
                          timestamp, packet_size);
                    sequence_number_for_player++;                 // simply increment
                    expected_timestamp = timestamp + packet_size; // for the next time
                    packets_played_in_this_sequence++;
                  }
                }
              } else {
                debug(3,
                      "skipped deciphering block %u with timestamp %u because its lead time is "
                      "out of range at %f "
                      "seconds.",
                      seq_no, timestamp, lead_time * 1.0E-9);
                uint32_t currentAnchorRTP = 0;
                uint64_t currentAnchorLocalTime = 0;
                if (get_ptp_anchor_local_time_info(conn, &currentAnchorRTP,
                                                   &currentAnchorLocalTime) == clock_ok) {
                  debug(3, "anchorRTP: %u, anchorLocalTime: %" PRIu64 ".", currentAnchorRTP,
                        currentAnchorLocalTime);
                } else {
                  debug(3, "Clock not okay");
                }
              }
            } else {
              debug(2, "Unrecognised or invalid ssrc: %s.", get_ssrc_name(payload_ssrc));
            }
          } else {
            debug(1, "dropping buffer that should have played before the last actually played.");
          }
          new_audio_block_needed = 1; // the block has been used up and is no longer current
        } else {
          usleep(20000); // wait for a while
        }
      }
    }
  } while (finished == 0);
  // debug(1, "Connection %d: rtp_buffered_audio_processor PID %d exiting", conn->connection_number,
  //       syscall(SYS_gettid));
  pthread_cleanup_pop(1); // buffered_tcp_reader thread creation
  pthread_cleanup_pop(1); // buffer malloc
  pthread_cleanup_pop(1); // not_full_cv
  pthread_cleanup_pop(1); // not_empty_cv
  pthread_cleanup_pop(1); // mutex
  pthread_cleanup_pop(1); // descriptor malloc
  pthread_cleanup_pop(1); // pthread_t malloc
  pthread_cleanup_pop(1); // do the cleanup.
  // debug(1, "Connection %d: rtp_buffered_audio_processor PID %d finish", conn->connection_number,
  //       syscall(SYS_gettid));
  pthread_exit(NULL);
}
