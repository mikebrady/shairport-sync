/*
 * Slave-clocked ALAC stream player. This file is part of Shairport.
 * Copyright (c) James Laird 2011, 2013
 * All rights reserved.
 *
 * Modifications for audio synchronisation
 * and related work, copyright (c) Mike Brady 2014 -- 2020
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

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syslog.h>
#include <sys/types.h>
#include <unistd.h>

#include "config.h"

#ifdef CONFIG_MBEDTLS
#include <mbedtls/aes.h>
#include <mbedtls/havege.h>
#endif

#ifdef CONFIG_POLARSSL
#include <polarssl/aes.h>
#include <polarssl/havege.h>
#endif

#ifdef CONFIG_OPENSSL
#include <openssl/aes.h>
#endif

#ifdef CONFIG_SOXR
#include <soxr.h>
#endif

#ifdef CONFIG_CONVOLUTION
#include <FFTConvolver/convolver.h>
#endif

#ifdef CONFIG_METADATA_HUB
#include "metadata_hub.h"
#endif

#ifdef CONFIG_DACP_CLIENT
#include "dacp.h"
#endif

#include "common.h"
#include "mdns.h"
#include "player.h"
#include "rtp.h"
#include "rtsp.h"

#include "alac.h"

#ifdef CONFIG_APPLE_ALAC
#include "apple_alac.h"
#endif

#include "loudness.h"

#include "activity_monitor.h"

// make the first audio packet deliberately early to bias the sync error of
// the very first packet, making the error more likely to be too early
// rather than too late. It it's too early,
// a delay exactly compensating for it can be sent just before the
// first packet. This should exactly compensate for the error.

int64_t first_frame_early_bias = 8;

// default buffer size
// needs to be a power of 2 because of the way BUFIDX(seqno) works
//#define BUFFER_FRAMES 512
#define MAX_PACKET 2048

// DAC buffer occupancy stuff
#define DAC_BUFFER_QUEUE_MINIMUM_LENGTH 2500

// static abuf_t audio_buffer[BUFFER_FRAMES];
#define BUFIDX(seqno) ((seq_t)(seqno) % BUFFER_FRAMES)

uint32_t modulo_32_offset(uint32_t from, uint32_t to) {
  if (from <= to)
    return to - from;
  else
    return UINT32_MAX - from + to + 1;
}

uint64_t modulo_64_offset(uint64_t from, uint64_t to) {
  if (from <= to)
    return to - from;
  else
    return UINT64_MAX - from + to + 1;
}

void do_flush(uint32_t timestamp, rtsp_conn_info *conn);

static void ab_resync(rtsp_conn_info *conn) {
  int i;
  for (i = 0; i < BUFFER_FRAMES; i++) {
    conn->audio_buffer[i].ready = 0;
    conn->audio_buffer[i].resend_request_number = 0;
    conn->audio_buffer[i].resend_time =
        0; // this is either zero or the time the last resend was requested.
    conn->audio_buffer[i].initialisation_time =
        0; // this is either the time the packet was received or the time it was noticed the packet
           // was missing.
    conn->audio_buffer[i].sequence_number = 0;
  }
  conn->ab_synced = 0;
  conn->last_seqno_read = -1;
  conn->ab_buffering = 1;
}

// given starting and ending points as unsigned 16-bit integers running modulo 2^16, returns the
// position of x in the interval in *pos
// returns true if x is actually within the buffer

int position_in_modulo_uint16_t_buffer(uint16_t x, uint16_t start, uint16_t end, uint16_t *pos) {
  int response = 0; // not in the buffer
  if (start <= end) {
    if (x < start) { // this means that it's up around the wrap
      if (pos)
        *pos = UINT16_MAX - start + 1 + x;
    } else {
      if (pos)
        *pos = x - start;
      if (x < end)
        response = 1;
    }
  } else if ((x >= start)) { // && (x <= UINT16_MAX)) { // always true
    response = 1;
    if (pos)
      *pos = x - start;
  } else {
    if (pos)
      *pos = UINT16_MAX - start + 1 + x;
    if (x < end) {
      response = 1;
    }
  }
  return response;
}

static inline seq_t SUCCESSOR(seq_t x) { return x + 1; }

// a minus b
int16_t seq_diff(seq_t a, seq_t b) {
  int16_t response;
  seq_t diff = a - b;
  seq_t invdiff = b - a;
  if (diff < invdiff)
    response = diff;
  else
    response = -invdiff;
  return response;
}

static inline seq_t seq_sum(seq_t a, seq_t b) { return a + b; }

// This orders u and v by picking the smaller of the two modulo differences
// in unsigned modulo arithmetic and setting the sign of the result accordingly.

// if u - v gives the smaller modulo difference, then that modulo difference is returned
// otherwise the negative of the  v - u modulo difference is returned.

// (Think of a modulo ring starting at 0 and circling around clockwise finally to
// modulo-2, modulo-1 to 0 again. To determine the ordering of the two numbers,
// Find the shorter path between them around the ring.
// If you go from v to u in the positive (0, 1, 2 ... or clockwise) direction,
// then u is greater than (or "after") v.)

// The result will always fit in an int64_t as it must be less or equal to half way
// around the ring.
// Due to the asymmetry of 2's complement representation, however,
// if the difference is equal to modulo / 2, it is ambiguous as to whether
// u is "after" or "before" (i.e. greater or less than) v.
// and will be returned as -modulo / 2, that is, "before".
// If that ever happens and there is a real ambiguity in the application,
// the modulo chosen is too small.

int64_t int64_mod_difference(const uint64_t u, const uint64_t v, const uint64_t modulo) {
  int64_t response;
  uint64_t diff = (u - v) % modulo;
  uint64_t invdiff = (v - u) % modulo;
  if (diff < invdiff)
    response = diff;
  else
    response = -invdiff;
  return response;
}

void reset_input_flow_metrics(rtsp_conn_info *conn) {
  conn->play_number_after_flush = 0;
  conn->packet_count_since_flush = 0;
  conn->input_frame_rate_starting_point_is_valid = 0;
  conn->initial_reference_time = 0;
  conn->initial_reference_timestamp = 0;
}

void unencrypted_packet_decode(unsigned char *packet, int length, short *dest, int *outsize,
                               int size_limit, rtsp_conn_info *conn) {
  if (conn->stream.type == ast_apple_lossless) {
#ifdef CONFIG_APPLE_ALAC
    if (config.use_apple_decoder) {
      if (conn->decoder_in_use != 1 << decoder_apple_alac) {
        debug(2, "Apple ALAC Decoder used on encrypted audio.");
        conn->decoder_in_use = 1 << decoder_apple_alac;
      }
      apple_alac_decode_frame(packet, length, (unsigned char *)dest, outsize);
      *outsize = *outsize * 4; // bring the size to bytes
    } else
#endif
    {
      if (conn->decoder_in_use != 1 << decoder_hammerton) {
        debug(2, "Hammerton Decoder used on encrypted audio.");
        conn->decoder_in_use = 1 << decoder_hammerton;
      }
      alac_decode_frame(conn->decoder_info, packet, (unsigned char *)dest, outsize);
    }
  } else if (conn->stream.type == ast_uncompressed) {
    int length_to_use = length;
    if (length_to_use > size_limit) {
      warn("unencrypted_packet_decode: uncompressed audio packet too long (size: %d bytes) to "
           "process -- truncated",
           length);
      length_to_use = size_limit;
    }
    int i;
    short *source = (short *)packet;
    for (i = 0; i < (length_to_use / 2); i++) {
      *dest = ntohs(*source);
      dest++;
      source++;
    }
    *outsize = length_to_use;
  }
}

int audio_packet_decode(short *dest, int *destlen, uint8_t *buf, int len, rtsp_conn_info *conn) {
  // parameters: where the decoded stuff goes, its length in samples,
  // the incoming packet, the length of the incoming packet in bytes
  // destlen should contain the allowed max number of samples on entry

  if (len > MAX_PACKET) {
    warn("Incoming audio packet size is too large at %d; it should not exceed %d.", len,
         MAX_PACKET);
    return -1;
  }
  unsigned char packet[MAX_PACKET];
  // unsigned char packetp[MAX_PACKET];
  assert(len <= MAX_PACKET);
  int reply = 0;                                          // everything okay
  int outsize = conn->input_bytes_per_frame * (*destlen); // the size the output should be, in bytes
  int maximum_possible_outsize = outsize;

  if (conn->stream.encrypted) {
    unsigned char iv[16];
    int aeslen = len & ~0xf;
    memcpy(iv, conn->stream.aesiv, sizeof(iv));
#ifdef CONFIG_MBEDTLS
    mbedtls_aes_crypt_cbc(&conn->dctx, MBEDTLS_AES_DECRYPT, aeslen, iv, buf, packet);
#endif
#ifdef CONFIG_POLARSSL
    aes_crypt_cbc(&conn->dctx, AES_DECRYPT, aeslen, iv, buf, packet);
#endif
#ifdef CONFIG_OPENSSL
    AES_cbc_encrypt(buf, packet, aeslen, &conn->aes, iv, AES_DECRYPT);
#endif
    memcpy(packet + aeslen, buf + aeslen, len - aeslen);
    unencrypted_packet_decode(packet, len, dest, &outsize, maximum_possible_outsize, conn);
  } else {
    // not encrypted
    unencrypted_packet_decode(buf, len, dest, &outsize, maximum_possible_outsize, conn);
  }

  if (outsize > maximum_possible_outsize) {
    debug(2,
          "Output from alac_decode larger (%d bytes, not frames) than expected (%d bytes) -- "
          "truncated, but buffer overflow possible! Encrypted = %d.",
          outsize, maximum_possible_outsize, conn->stream.encrypted);
    reply = -1; // output packet is the wrong size
  }

  *destlen = outsize / conn->input_bytes_per_frame;
  if ((outsize % conn->input_bytes_per_frame) != 0)
    debug(1,
          "Number of audio frames (%d) does not correspond exactly to the number of bytes (%d) "
          "and the audio frame size (%d).",
          *destlen, outsize, conn->input_bytes_per_frame);
  return reply;
}

static int init_alac_decoder(int32_t fmtp[12], rtsp_conn_info *conn) {

  // This is a guess, but the format of the fmtp looks identical to the format of an
  // ALACSpecificCOnfig
  // which is detailed in the file ALACMagicCookieDescription.txt in the Apple ALAC sample
  // implementation
  // Here it is:

  /*
      struct	ALACSpecificConfig (defined in ALACAudioTypes.h)
      abstract   	This struct is used to describe codec provided information about the encoded
     Apple Lossless bitstream.
                  It must accompany the encoded stream in the containing audio file and be provided
     to the decoder.

      field      	frameLength 		uint32_t	indicating the frames per packet
     when
     no
     explicit
     frames per packet setting is
                                                            present in the packet header. The
     encoder frames per packet can be explicitly set
                                                            but for maximum compatibility, the
     default encoder setting of 4096 should be used.

      field      	compatibleVersion 	uint8_t 	indicating compatible version,
                                                            value must be set to 0

      field      	bitDepth 		uint8_t 	describes the bit depth of the
     source
     PCM
     data
     (maximum
     value = 32)

      field      	pb 			uint8_t 	currently unused tuning
     parametetbugr.
                                                            value should be set to 40

      field      	mb 			uint8_t 	currently unused tuning parameter.
                                                            value should be set to 14

      field      	kb			uint8_t 	currently unused tuning parameter.
                                                            value should be set to 10

      field      	numChannels 		uint8_t 	describes the channel count (1 =
     mono,
     2
     =
     stereo,
     etc...)
                                                            when channel layout info is not provided
     in the 'magic cookie', a channel count > 2
                                                            describes a set of discreet channels
     with no specific ordering

      field      	maxRun			uint16_t 	currently unused.
                                                    value should be set to 255

      field      	maxFrameBytes 		uint32_t 	the maximum size of an Apple
     Lossless
     packet
     within
     the encoded stream.
                                                          value of 0 indicates unknown

      field      	avgBitRate 		uint32_t 	the average bit rate in bits per
     second
     of
     the
     Apple
     Lossless stream.
                                                          value of 0 indicates unknown

      field      	sampleRate 		uint32_t 	sample rate of the encoded stream
   */

  // We are going to go on that basis

  alac_file *alac;

  alac = alac_create(conn->input_bit_depth,
                     conn->input_num_channels); // no pthread cancellation point in here
  if (!alac)
    return 1;
  conn->decoder_info = alac;

  alac->setinfo_max_samples_per_frame = conn->max_frames_per_packet;
  alac->setinfo_7a = fmtp[2];
  alac->setinfo_sample_size = conn->input_bit_depth;
  alac->setinfo_rice_historymult = fmtp[4];
  alac->setinfo_rice_initialhistory = fmtp[5];
  alac->setinfo_rice_kmodifier = fmtp[6];
  alac->setinfo_7f = fmtp[7];
  alac->setinfo_80 = fmtp[8];
  alac->setinfo_82 = fmtp[9];
  alac->setinfo_86 = fmtp[10];
  alac->setinfo_8a_rate = fmtp[11];
  alac_allocate_buffers(alac); // no pthread cancellation point in here

#ifdef CONFIG_APPLE_ALAC
  apple_alac_init(fmtp); // no pthread cancellation point in here
#endif

  return 0;
}

static void terminate_decoders(rtsp_conn_info *conn) {
  alac_free(conn->decoder_info);
#ifdef CONFIG_APPLE_ALAC
  apple_alac_terminate();
#endif
}

static void init_buffer(rtsp_conn_info *conn) {
  int i;
  for (i = 0; i < BUFFER_FRAMES; i++)
    conn->audio_buffer[i].data = malloc(conn->input_bytes_per_frame * conn->max_frames_per_packet);
  ab_resync(conn);
}

static void free_audio_buffers(rtsp_conn_info *conn) {
  int i;
  for (i = 0; i < BUFFER_FRAMES; i++)
    free(conn->audio_buffer[i].data);
}

int first_possibly_missing_frame = -1;

void player_put_packet(seq_t seqno, uint32_t actual_timestamp, uint8_t *data, int len,
                       rtsp_conn_info *conn) {

  debug_mutex_lock(&conn->ab_mutex, 30000, 0);
  uint64_t time_now = get_absolute_time_in_ns();
  conn->packet_count++;
  conn->packet_count_since_flush++;
  conn->time_of_last_audio_packet = time_now;
  if (conn->connection_state_to_output) { // if we are supposed to be processing these packets
    abuf_t *abuf = 0;
    if (!conn->ab_synced) {
      // if this is the first packet...
      debug(3, "syncing to seqno %u.", seqno);
      conn->ab_write = seqno;
      conn->ab_read = seqno;
      conn->ab_synced = 1;
    }
    int16_t write_point_gap = seq_diff(seqno, conn->ab_write); // this is the difference between
    // the incoming packet number and the packet number that was expected.
    if (write_point_gap ==
        0) { // if this is the expected packet (which could be the first packet...)
      if (conn->input_frame_rate_starting_point_is_valid == 0) {
        if ((conn->packet_count_since_flush >= 500) && (conn->packet_count_since_flush <= 510)) {
          conn->frames_inward_measurement_start_time = time_now;
          conn->frames_inward_frames_received_at_measurement_start_time = actual_timestamp;
          conn->input_frame_rate_starting_point_is_valid = 1; // valid now
        }
      }
      conn->frames_inward_measurement_time = time_now;
      conn->frames_inward_frames_received_at_measurement_time = actual_timestamp;
      abuf = conn->audio_buffer + BUFIDX(seqno);
      conn->ab_write = SUCCESSOR(seqno); // move the write pointer to the next free space
    } else if (write_point_gap > 0) {    // newer than expected
      // initialise  the frames in between
      int i;
      for (i = 0; i < write_point_gap; i++) {
        abuf = conn->audio_buffer + BUFIDX(seq_sum(conn->ab_write, i));
        abuf->ready = 0; // to be sure, to be sure
        abuf->resend_request_number = 0;
        abuf->initialisation_time =
            time_now;          // this represents when the packet was noticed to be missing
        abuf->status = 1 << 0; // signifying missing
        abuf->resend_time = 0;
        abuf->given_timestamp = 0;
        abuf->sequence_number = 0;
      }
      abuf = conn->audio_buffer + BUFIDX(seqno);
      conn->ab_write = SUCCESSOR(seqno);
    } else if (seq_diff(seqno, conn->ab_read) > 0) { // older than expected but still not too late
      conn->late_packets++;
      abuf = conn->audio_buffer + BUFIDX(seqno);
    } else { // too late.
      conn->too_late_packets++;
    }

    if (abuf) {
      int datalen = conn->max_frames_per_packet;
      abuf->initialisation_time = time_now;
      abuf->resend_time = 0;
      if (audio_packet_decode(abuf->data, &datalen, data, len, conn) == 0) {
        abuf->ready = 1;
        abuf->status = 0; // signifying that it was received
        abuf->length = datalen;
        abuf->given_timestamp = actual_timestamp;
        abuf->sequence_number = seqno;
      } else {
        debug(1, "Bad audio packet detected and discarded.");
        abuf->ready = 0;
        abuf->status = 1 << 1; // bad packet, discarded
        abuf->resend_request_number = 0;
        abuf->given_timestamp = 0;
        abuf->sequence_number = 0;
      }
    }

    int rc = pthread_cond_signal(&conn->flowcontrol);
    if (rc)
      debug(1, "Error signalling flowcontrol.");

    // resend checks
    {
      uint64_t minimum_wait_time =
          (uint64_t)(config.resend_control_first_check_time * (uint64_t)1000000000);
      uint64_t resend_repeat_interval =
          (uint64_t)(config.resend_control_check_interval_time * (uint64_t)1000000000);
      uint64_t minimum_remaining_time = (uint64_t)(
          (config.resend_control_last_check_time + config.audio_backend_buffer_desired_length) *
          (uint64_t)1000000000);
      uint64_t latency_time = (uint64_t)(conn->latency * (uint64_t)1000000000);
      latency_time = latency_time / (uint64_t)conn->input_rate;

      int x; // this is the first frame to be checked
      // if we detected a first empty frame before and if it's still in the buffer!
      if ((first_possibly_missing_frame >= 0) &&
          (position_in_modulo_uint16_t_buffer(first_possibly_missing_frame, conn->ab_read,
                                              conn->ab_write, NULL))) {
        x = first_possibly_missing_frame;
      } else {
        x = conn->ab_read;
      }

      first_possibly_missing_frame = -1; // has not been set

      int missing_frame_run_count = 0;
      int start_of_missing_frame_run = -1;
      int number_of_missing_frames = 0;
      while (x != conn->ab_write) {
        abuf_t *check_buf = conn->audio_buffer + BUFIDX(x);
        if (!check_buf->ready) {
          if (first_possibly_missing_frame < 0)
            first_possibly_missing_frame = x;
          number_of_missing_frames++;
          // debug(1, "frame %u's initialisation_time is 0x%" PRIx64 ", latency_time is 0x%"
          // PRIx64 ", time_now is 0x%" PRIx64 ", minimum_remaining_time is 0x%" PRIx64 ".", x,
          // check_buf->initialisation_time, latency_time, time_now, minimum_remaining_time);
          int too_late = ((check_buf->initialisation_time < (time_now - latency_time)) ||
                          ((check_buf->initialisation_time - (time_now - latency_time)) <
                           minimum_remaining_time));
          int too_early = ((time_now - check_buf->initialisation_time) < minimum_wait_time);
          int too_soon_after_last_request =
              ((check_buf->resend_time != 0) &&
               ((time_now - check_buf->resend_time) <
                resend_repeat_interval)); // time_now can never be less than the time_tag

          if (too_late)
            check_buf->status |= 1 << 2; // too late
          else
            check_buf->status &= 0xFF - (1 << 2); // not too late
          if (too_early)
            check_buf->status |= 1 << 3; // too early
          else
            check_buf->status &= 0xFF - (1 << 3); // not too early
          if (too_soon_after_last_request)
            check_buf->status |= 1 << 4; // too soon after last request
          else
            check_buf->status &= 0xFF - (1 << 4); // not too soon after last request

          if ((!too_soon_after_last_request) && (!too_late) && (!too_early)) {
            if (start_of_missing_frame_run == -1) {
              start_of_missing_frame_run = x;
              missing_frame_run_count = 1;
            } else {
              missing_frame_run_count++;
            }
            check_buf->resend_time = time_now; // setting the time to now because we are
                                               // definitely going to take action
            check_buf->resend_request_number++;
            debug(3, "Frame %d is missing with ab_read of %u and ab_write of %u.", x, conn->ab_read,
                  conn->ab_write);
          }
          // if (too_late) {
          //   debug(1,"too late to get missing frame %u.", x);
          // }
        }
        // if (number_of_missing_frames != 0)
        //  debug(1,"check with x = %u, ab_read = %u, ab_write = %u, first_possibly_missing_frame
        //  = %d.", x, conn->ab_read, conn->ab_write, first_possibly_missing_frame);
        x = (x + 1) & 0xffff;
        if (((check_buf->ready) || (x == conn->ab_write)) && (missing_frame_run_count > 0)) {
          // send a resend request
          if (missing_frame_run_count > 1)
            debug(3, "request resend of %d packets starting at seqno %u.", missing_frame_run_count,
                  start_of_missing_frame_run);
          if (config.disable_resend_requests == 0) {
            debug_mutex_unlock(&conn->ab_mutex, 3);
            rtp_request_resend(start_of_missing_frame_run, missing_frame_run_count, conn);
            debug_mutex_lock(&conn->ab_mutex, 20000, 1);
            conn->resend_requests++;
          }
          start_of_missing_frame_run = -1;
          missing_frame_run_count = 0;
        }
      }
      if (number_of_missing_frames == 0)
        first_possibly_missing_frame = conn->ab_write;
    }
  }
  debug_mutex_unlock(&conn->ab_mutex, 0);
}

int32_t rand_in_range(int32_t exclusive_range_limit) {
  static uint32_t lcg_prev = 12345;
  // returns a pseudo random integer in the range 0 to (exclusive_range_limit-1) inclusive
  int64_t sp = lcg_prev;
  int64_t rl = exclusive_range_limit;
  lcg_prev = lcg_prev * 69069 + 3; // crappy psrg
  sp = sp * rl; // 64 bit calculation. Interesting part is above the 32 rightmost bits;
  return sp >> 32;
}

int get_and_check_effective_latency(rtsp_conn_info *conn, uint32_t *effective_latency,
                                    double offset_time) {
  // check that the overall effective latency remains positive and is not greater than the capacity
  // of the buffers return 0 if okay, -1 if the latency would be negative, +1 if it would be too
  // large

  int result = 0;
  if (offset_time >= 0.0) {
    uint32_t latency_addition = (uint32_t)(offset_time * conn->input_rate);
    // keep about one second of buffers back
    if ((*effective_latency + latency_addition) <=
        (conn->max_frames_per_packet * (BUFFER_FRAMES - config.minimum_free_buffer_headroom)))
      *effective_latency += latency_addition;
    else
      result = 1;
  } else {
    uint32_t latency_reduction = (uint32_t)((-offset_time) * conn->input_rate);
    if (latency_reduction <= *effective_latency)
      *effective_latency -= latency_reduction;
    else
      result = -1;
  }
  return result;
}

static inline void process_sample(int32_t sample, char **outp, sps_format_t format, int volume,
                                  int dither, rtsp_conn_info *conn) {
  /*
  {
                static int old_volume = 0;
                if (volume != old_volume) {
                        debug(1,"Volume is now %d.",volume);
                        old_volume = volume;
                }
  }
  */

  int64_t hyper_sample = sample;
  int result = 0;

  if (config.loudness) {
    hyper_sample <<=
        32; // Do not apply volume as it has already been done with the Loudness DSP filter
  } else {
    int64_t hyper_volume = (int64_t)volume << 16;
    hyper_sample = hyper_sample * hyper_volume; // this is 64 bit bit multiplication -- we may need
                                                // to dither it down to its target resolution
  }

  // next, do dither, if necessary
  if (dither) {

    // add a TPDF dither -- see
    // http://educypedia.karadimov.info/library/DitherExplained.pdf
    // and the discussion around https://www.hydrogenaud.io/forums/index.php?showtopic=16963&st=25

    // I think, for a 32 --> 16 bits, the range of
    // random numbers needs to be from -2^16 to 2^16, i.e. from -65536 to 65536 inclusive, not from
    // -32768 to +32767

    // Actually, what would be generated here is from -65535 to 65535, i.e. one less on the limits.

    // See the original paper at
    // http://www.ece.rochester.edu/courses/ECE472/resources/Papers/Lipshitz_1992.pdf
    // by Lipshitz, Wannamaker and Vanderkooy, 1992.

    int64_t dither_mask = 0;
    switch (format) {
    case SPS_FORMAT_S32:
    case SPS_FORMAT_S32_LE:
    case SPS_FORMAT_S32_BE:
      dither_mask = (int64_t)1 << (64 - 32);
      break;
    case SPS_FORMAT_S24:
    case SPS_FORMAT_S24_LE:
    case SPS_FORMAT_S24_BE:
    case SPS_FORMAT_S24_3LE:
    case SPS_FORMAT_S24_3BE:
      dither_mask = (int64_t)1 << (64 - 24);
      break;
    case SPS_FORMAT_S16:
    case SPS_FORMAT_S16_LE:
    case SPS_FORMAT_S16_BE:
      dither_mask = (int64_t)1 << (64 - 16);
      break;
    case SPS_FORMAT_S8:
    case SPS_FORMAT_U8:
      dither_mask = (int64_t)1 << (64 - 8);
      break;
    case SPS_FORMAT_UNKNOWN:
      die("Unexpected SPS_FORMAT_UNKNOWN while calculating dither mask.");
      break;
    case SPS_FORMAT_AUTO:
      die("Unexpected SPS_FORMAT_AUTO while calculating dither mask.");
      break;
    case SPS_FORMAT_INVALID:
      die("Unexpected SPS_FORMAT_INVALID while calculating dither mask.");
      break;
    }
    dither_mask -= 1;
    int64_t r = r64i();

    int64_t tpdf = (r & dither_mask) - (conn->previous_random_number & dither_mask);
    conn->previous_random_number = r;
    // add dither, allowing for clipping
    if (tpdf >= 0) {
      if (INT64_MAX - tpdf >= hyper_sample)
        hyper_sample += tpdf;
      else
        hyper_sample = INT64_MAX;
    } else {
      if (INT64_MIN - tpdf <= hyper_sample)
        hyper_sample += tpdf;
      else
        hyper_sample = INT64_MIN;
    }
    // dither is complete here
  }

  // move the result to the desired position in the int64_t
  char *op = *outp;
  uint8_t byt;
  switch (format) {
  case SPS_FORMAT_S32_LE:
    hyper_sample >>= (64 - 32);
    byt = (uint8_t)hyper_sample;
    *op++ = byt;
    byt = (uint8_t)(hyper_sample >> 8);
    *op++ = byt;
    byt = (uint8_t)(hyper_sample >> 16);
    *op++ = byt;
    byt = (uint8_t)(hyper_sample >> 24);
    *op++ = byt;
    result = 4;
    break;
  case SPS_FORMAT_S32_BE:
    hyper_sample >>= (64 - 32);
    byt = (uint8_t)(hyper_sample >> 24);
    *op++ = byt;
    byt = (uint8_t)(hyper_sample >> 16);
    *op++ = byt;
    byt = (uint8_t)(hyper_sample >> 8);
    *op++ = byt;
    byt = (uint8_t)hyper_sample;
    *op++ = byt;
    result = 4;
    break;
  case SPS_FORMAT_S32:
    hyper_sample >>= (64 - 32);
    *(int32_t *)op = hyper_sample;
    result = 4;
    break;
  case SPS_FORMAT_S24_3LE:
    hyper_sample >>= (64 - 24);
    byt = (uint8_t)hyper_sample;
    *op++ = byt;
    byt = (uint8_t)(hyper_sample >> 8);
    *op++ = byt;
    byt = (uint8_t)(hyper_sample >> 16);
    *op++ = byt;
    result = 3;
    break;
  case SPS_FORMAT_S24_3BE:
    hyper_sample >>= (64 - 24);
    byt = (uint8_t)(hyper_sample >> 16);
    *op++ = byt;
    byt = (uint8_t)(hyper_sample >> 8);
    *op++ = byt;
    byt = (uint8_t)hyper_sample;
    *op++ = byt;
    result = 3;
    break;
  case SPS_FORMAT_S24_LE:
    hyper_sample >>= (64 - 24);
    byt = (uint8_t)hyper_sample;
    *op++ = byt;
    byt = (uint8_t)(hyper_sample >> 8);
    *op++ = byt;
    byt = (uint8_t)(hyper_sample >> 16);
    *op++ = byt;
    *op++ = 0;
    result = 4;
    break;
  case SPS_FORMAT_S24_BE:
    hyper_sample >>= (64 - 24);
    *op++ = 0;
    byt = (uint8_t)(hyper_sample >> 16);
    *op++ = byt;
    byt = (uint8_t)(hyper_sample >> 8);
    *op++ = byt;
    byt = (uint8_t)hyper_sample;
    *op++ = byt;
    result = 4;
    break;
  case SPS_FORMAT_S24:
    hyper_sample >>= (64 - 24);
    *(int32_t *)op = hyper_sample;
    result = 4;
    break;
  case SPS_FORMAT_S16_LE:
    hyper_sample >>= (64 - 16);
    byt = (uint8_t)hyper_sample;
    *op++ = byt;
    byt = (uint8_t)(hyper_sample >> 8);
    *op++ = byt;
    result = 2;
    break;
  case SPS_FORMAT_S16_BE:
    hyper_sample >>= (64 - 16);
    byt = (uint8_t)(hyper_sample >> 8);
    *op++ = byt;
    byt = (uint8_t)hyper_sample;
    *op++ = byt;
    result = 2;
    break;
  case SPS_FORMAT_S16:
    hyper_sample >>= (64 - 16);
    *(int16_t *)op = (int16_t)hyper_sample;
    result = 2;
    break;
  case SPS_FORMAT_S8:
    hyper_sample >>= (int8_t)(64 - 8);
    *op = hyper_sample;
    result = 1;
    break;
  case SPS_FORMAT_U8:
    hyper_sample >>= (uint8_t)(64 - 8);
    hyper_sample += 128;
    *op = hyper_sample;
    result = 1;
    break;
  case SPS_FORMAT_UNKNOWN:
    die("Unexpected SPS_FORMAT_UNKNOWN while outputting samples");
    break;
  case SPS_FORMAT_AUTO:
    die("Unexpected SPS_FORMAT_AUTO while outputting samples");
    break;
  case SPS_FORMAT_INVALID:
    die("Unexpected SPS_FORMAT_INVALID while outputting samples");
    break;
  }

  *outp += result;
}

void buffer_get_frame_cleanup_handler(void *arg) {
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  debug_mutex_unlock(&conn->ab_mutex, 0);
}

// get the next frame, when available. return 0 if underrun/stream reset.
static abuf_t *buffer_get_frame(rtsp_conn_info *conn) {
  // int16_t buf_fill;
  uint64_t local_time_now;
  // struct timespec tn;
  abuf_t *curframe = NULL;
  int notified_buffer_empty = 0; // diagnostic only

  debug_mutex_lock(&conn->ab_mutex, 30000, 0);

  int wait;
  long dac_delay = 0; // long because alsa returns a long

  int have_sent_prefiller_silence =
      0; // set to true when we have sent at least one silent frame to the DAC

  pthread_cleanup_push(buffer_get_frame_cleanup_handler,
                       (void *)conn); // undo what's been done so far
  do {
    // get the time
    local_time_now = get_absolute_time_in_ns(); // type okay
    // debug(3, "buffer_get_frame is iterating");

    int rco = get_requested_connection_state_to_output();

    if (conn->connection_state_to_output != rco) {
      conn->connection_state_to_output = rco;
      // change happening
      if (conn->connection_state_to_output == 0) { // going off
        debug(2, "request flush because connection_state_to_output is off");
        debug_mutex_lock(&conn->flush_mutex, 1000, 1);
        conn->flush_requested = 1;
        conn->flush_rtp_timestamp = 0;
        debug_mutex_unlock(&conn->flush_mutex, 3);
      }
    }

    if (config.output->is_running)
      if (config.output->is_running() != 0) { // if the back end isn't running for any reason
        debug(2, "request flush because back end is not running");
        debug_mutex_lock(&conn->flush_mutex, 1000, 0);
        conn->flush_requested = 1;
        conn->flush_rtp_timestamp = 0;
        debug_mutex_unlock(&conn->flush_mutex, 0);
      }

    debug_mutex_lock(&conn->flush_mutex, 1000, 0);
    if (conn->flush_requested == 1) {
      if (conn->flush_output_flushed == 0)
        if (config.output->flush) {
          config.output->flush(); // no cancellation points
          debug(2, "flush request: flush output device.");
        }
      conn->flush_output_flushed = 1;
    }
    // now check to see it the flush request is for frames in the buffer or not
    // if the first_packet_timestamp is zero, don't check
    int flush_needed = 0;
    int drop_request = 0;
    if ((conn->flush_requested == 1) && (conn->flush_rtp_timestamp == 0)) {
      debug(1, "flush request: flush frame 0 -- flush assumed to be needed.");
      flush_needed = 1;
      drop_request = 1;
    } else if (conn->flush_rtp_timestamp != 0) {
      if ((conn->ab_synced) && ((conn->ab_write - conn->ab_read) > 0)) {
        abuf_t *firstPacket = conn->audio_buffer + BUFIDX(conn->ab_read);
        abuf_t *lastPacket = conn->audio_buffer + BUFIDX(conn->ab_write - 1);
        if ((firstPacket != NULL) && (firstPacket->ready)) {
          // discard flushes more than 10 seconds into the future -- they are probably bogus
          uint32_t first_frame_in_buffer = firstPacket->given_timestamp;
          int32_t offset_from_first_frame =
              (int32_t)(conn->flush_rtp_timestamp - first_frame_in_buffer);
          if (offset_from_first_frame > (int)conn->input_rate * 10) {
            debug(1,
                  "flush request: sanity check -- flush frame %u is too far into the future from "
                  "the first frame %u -- discarded.",
                  conn->flush_rtp_timestamp, first_frame_in_buffer);
            drop_request = 1;
          } else {
            if ((lastPacket != NULL) && (lastPacket->ready)) {
              // we have enough information to check if the flush is needed or can be discarded
              uint32_t last_frame_in_buffer = lastPacket->given_timestamp + lastPacket->length - 1;
              // now we have to work out if the flush frame is in the buffer
              // if it is later than the end of the buffer, flush everything and keep the request
              // active. if it is in the buffer, we need to flush part of the buffer. Actually we
              // flush the entire buffer and drop the request. if it is before the buffer, no flush
              // is needed. Drop the request.
              if (offset_from_first_frame > 0) {
                int32_t offset_to_last_frame =
                    (int32_t)(last_frame_in_buffer - conn->flush_rtp_timestamp);
                if (offset_to_last_frame >= 0) {
                  debug(2,
                        "flush request: flush frame %u active -- buffer contains %u frames, from "
                        "%u to %u",
                        conn->flush_rtp_timestamp, last_frame_in_buffer - first_frame_in_buffer + 1,
                        first_frame_in_buffer, last_frame_in_buffer);
                  drop_request = 1;
                  flush_needed = 1;
                } else {
                  debug(2,
                        "flush request: flush frame %u pending -- buffer contains %u frames, from "
                        "%u to %u",
                        conn->flush_rtp_timestamp, last_frame_in_buffer - first_frame_in_buffer + 1,
                        first_frame_in_buffer, last_frame_in_buffer);
                  flush_needed = 1;
                }
              } else {
                debug(2,
                      "flush request: flush frame %u expired -- buffer contains %u frames, from %u "
                      "to %u",
                      conn->flush_rtp_timestamp, last_frame_in_buffer - first_frame_in_buffer + 1,
                      first_frame_in_buffer, last_frame_in_buffer);
                drop_request = 1;
              }
            }
          }
        }
      } else {
        debug(3,
              "flush request: flush frame %u  -- buffer not synced or empty: synced: %d, ab_read: "
              "%u, ab_write: %u",
              conn->flush_rtp_timestamp, conn->ab_synced, conn->ab_read, conn->ab_write);
        // leave flush request pending and don't do a buffer flush, because there isn't one
      }
    }
    if (flush_needed) {
      debug(2, "flush request: flush done.");
      ab_resync(conn); // no cancellation points
      conn->first_packet_timestamp = 0;
      conn->first_packet_time_to_play = 0;
      conn->time_since_play_started = 0;
      have_sent_prefiller_silence = 0;
      dac_delay = 0;
    }
    if (drop_request) {
      debug(2, "flush request: request dropped.");
      conn->flush_requested = 0;
      conn->flush_rtp_timestamp = 0;
      conn->flush_output_flushed = 0;
    }
    debug_mutex_unlock(&conn->flush_mutex, 0);
    if (conn->ab_synced) {
      curframe = conn->audio_buffer + BUFIDX(conn->ab_read);

      if ((conn->ab_read != conn->ab_write) &&
          (curframe->ready)) { // it could be synced and empty, under
                               // exceptional circumstances, with the
                               // frame unused, thus apparently ready

        if (curframe->sequence_number != conn->ab_read) {
          // some kind of sync problem has occurred.
          if (BUFIDX(curframe->sequence_number) == BUFIDX(conn->ab_read)) {
            // it looks like aliasing has happened
            // jump to the new incoming stuff...
            conn->ab_read = curframe->sequence_number;
            debug(1, "Aliasing of buffer index -- reset.");
          } else {
            debug(1, "Inconsistent sequence numbers detected");
          }
        }
      }

      if ((curframe) && (curframe->ready)) {
        notified_buffer_empty = 0; // at least one buffer now -- diagnostic only.
        if (conn->ab_buffering) {  // if we are getting packets but not yet forwarding them to the
                                   // player
          if (conn->first_packet_timestamp == 0) {         // if this is the very first packet
            if (have_timestamp_timing_information(conn)) { // if we have a reference time
              // debug(1,"First frame seen with timestamp...");
              conn->first_packet_timestamp =
                  curframe->given_timestamp; // we will keep buffering until we are
                                             // supposed to start playing this
#ifdef CONFIG_METADATA
              // say we have started receiving frames here
              debug(2, "pffr");
              send_ssnc_metadata(
                  'pffr', NULL, 0,
                  0); // "first frame received", but don't wait if the queue is locked
#endif
              // Here, calculate when we should start playing. We need to know when to allow the
              // packets to be sent to the player.

              // every second or so, we get a reference on when a particular packet should be
              // played.

              // It probably won't  be the timestamp of our first packet, however, so we might have
              // to do some calculations.

              // To calculate when the first packet will be played, we figure out the exact time the
              // packet should be played according to its timestamp and the reference time.
              // We then need to add the desired latency, typically 88200 frames.

              // Then we need to offset this by the backend latency offset. For example, if we knew
              // that the audio back end has a latency of 100 ms, we would
              // ask for the first packet to be emitted 100 ms earlier than it should, i.e. -4410
              // frames, so that when it got through the audio back end,

              // if would be in sync. To do this, we would give it a latency offset of -0.1 sec,
              // i.e. -4410 frames.

              uint64_t should_be_time;
              uint32_t effective_latency = conn->latency;

              get_and_check_effective_latency(conn, &effective_latency,
                                              config.audio_backend_latency_offset);
              // we are ignoring the returned status because it will be captured on subsequent
              // frames, below.

              // what we are asking for here is "what is the local time at which time the calculated
              // frame should be played"

              frame_to_local_time(conn->first_packet_timestamp +
                                      effective_latency, // this will go modulo 2^32
                                  &should_be_time, conn);

              conn->first_packet_time_to_play = should_be_time;
              debug(2, "first_packet_time set for frame %u.", conn->first_packet_timestamp);

              if (local_time_now > conn->first_packet_time_to_play) {
                uint64_t lateness = local_time_now - conn->first_packet_time_to_play;
                debug(2, "First packet is %" PRIu64 " nanoseconds late! Flushing 0.5 seconds",
                      lateness);
                do_flush(conn->first_packet_timestamp + 5 * 4410, conn);
              }
            }
          }

          if (conn->first_packet_time_to_play != 0) {
            // Now that we know the timing of the first packet...
            if (config.output->delay) {
              // and that the output device is capable of synchronization...

              // We may send packets of
              // silence from now until the time the first audio packet should be sent
              // and then we will send the first packet, which will be followed by
              // the subsequent packets.
              // here, we figure out whether and what silence to send.

              uint64_t should_be_time;
              uint32_t effective_latency = conn->latency;

              switch (get_and_check_effective_latency(conn, &effective_latency,
                                                      config.audio_backend_latency_offset)) {
              case -1:
                if (conn->unachievable_audio_backend_latency_offset_notified == 0) {
                  warn(
                      "Negative latency! A latency of %d frames requested by the player, when "
                      "combined with an audio_backend_latency_offset of %f seconds, would make the "
                      "overall latency negative. The audio_backend_latency_offset setting is "
                      "ignored.",
                      conn->latency, config.audio_backend_latency_offset);
                  config.audio_backend_latency_offset = 0; // set it to zero
                  conn->unachievable_audio_backend_latency_offset_notified = 1;
                };
                break;
              case 1:
                if (conn->unachievable_audio_backend_latency_offset_notified == 0) {
                  warn("An audio_backend_latency_offset of %f seconds may exceed the frame "
                       "buffering "
                       "capacity -- the setting is ignored.",
                       config.audio_backend_latency_offset);
                  config.audio_backend_latency_offset = 0; // set it to zero;
                  conn->unachievable_audio_backend_latency_offset_notified = 1;
                };
                break;
              default:
                break;
              }

              // readjust first packet time to play
              frame_to_local_time(conn->first_packet_timestamp +
                                      effective_latency, // this will go modulo 2^32
                                  &should_be_time, conn);

              int64_t change_in_should_be_time =
                  (int64_t)(should_be_time - conn->first_packet_time_to_play);

              if (fabs(0.000001 * change_in_should_be_time) >
                  0.001) // the clock drift estimation might be nudging the estimate, and we can
                         // ignore this unless if's more than a microsecond
                debug(2, "Change in estimated first_packet_time: %8.4f milliseconds.",
                      0.000001 * change_in_should_be_time);

              conn->first_packet_time_to_play = should_be_time;

              if (local_time_now > conn->first_packet_time_to_play) {
                uint64_t lateness = local_time_now - conn->first_packet_time_to_play;
                debug(2, "Gone past starting time by %" PRIu64 " nanoseconds.", lateness);
                conn->ab_buffering = 0;
              } else {
                // do some calculations
                int64_t lead_time = conn->first_packet_time_to_play - local_time_now;
                if ((config.audio_backend_silent_lead_in_time_auto == 1) ||
                    (lead_time <=
                     (int64_t)(config.audio_backend_silent_lead_in_time * (int64_t)1000000000))) {
                  // debug(1, "Lead time: %" PRId64 " nanoseconds.", lead_time);
                  int resp = 0;
                  dac_delay = 0;
                  if (have_sent_prefiller_silence != 0)
                    resp = config.output->delay(
                        &dac_delay); // we know the output device must have a delay function
                  if (resp == 0) {
                    int64_t gross_frame_gap =
                        ((conn->first_packet_time_to_play - local_time_now) * config.output_rate) /
                        1000000000;
                    int64_t exact_frame_gap = gross_frame_gap - dac_delay;
                    int64_t frames_needed_to_maintain_desired_buffer =
                        (int64_t)(config.audio_backend_buffer_desired_length * config.output_rate) -
                        dac_delay;
                    // below, remember that exact_frame_gap and
                    // frames_needed_to_maintain_desired_buffer could both be negative
                    int64_t fs = frames_needed_to_maintain_desired_buffer;

                    // if there isn't enough time to have the desired buffer size
                    if (exact_frame_gap <= frames_needed_to_maintain_desired_buffer) {
                      fs = conn->max_frames_per_packet * 2;
                    }

                    // if we are very close to the end of buffering, i.e. within two frame-lengths,
                    // add the remaining silence needed and end buffering
                    if (exact_frame_gap <= conn->max_frames_per_packet * 2) {
                      fs = exact_frame_gap;
                      if (fs > first_frame_early_bias)
                        fs = fs - first_frame_early_bias; // deliberately make the first packet a
                                                          // tiny bit early so that the player may
                                                          // compensate for it at the last minute
                      conn->ab_buffering = 0;
                    }
                    void *silence;
                    if (fs > 0) {
                      silence = malloc(conn->output_bytes_per_frame * fs);
                      if (silence == NULL)
                        debug(1, "Failed to allocate %d byte silence buffer.", fs);
                      else {
                        // generate frames of silence with dither if necessary
                        conn->previous_random_number =
                            generate_zero_frames(silence, fs, config.output_format,
                                                 conn->enable_dither, conn->previous_random_number);
                        config.output->play(silence, fs);
                        // debug(1, "Sent %" PRId64 " frames of silence", fs);
                        free(silence);
                        have_sent_prefiller_silence = 1;
                      }
                    }
                  } else {

                    if (resp == sps_extra_code_output_stalled) {
                      if (conn->unfixable_error_reported == 0) {
                        conn->unfixable_error_reported = 1;
                        if (config.cmd_unfixable) {
                          command_execute(config.cmd_unfixable, "output_device_stalled", 1);
                        } else {
                          die("an unrecoverable error, \"output_device_stalled\", has been "
                               "detected.",
                               conn->connection_number);
                        }
                      }
                    } else {
                      debug(2, "Unexpected response to getting dac delay: %d.", resp);
                    }
                  }
                }
              }
            } else {
              // if the output device doesn't have a delay, we simply send the lead-in
              int64_t lead_time =
                  conn->first_packet_time_to_play - local_time_now; // negative if we are late
              void *silence;
              int64_t frame_gap = (lead_time * config.output_rate) / 1000000000;
              // debug(1,"%d frames needed.",frame_gap);
              while (frame_gap > 0) {
                ssize_t fs = config.output_rate / 10;
                if (fs > frame_gap)
                  fs = frame_gap;

                silence = malloc(conn->output_bytes_per_frame * fs);
                if (silence == NULL)
                  debug(1, "Failed to allocate %d frame silence buffer.", fs);
                else {
                  // debug(1, "No delay function -- outputting %d frames of silence.", fs);
                  conn->previous_random_number =
                      generate_zero_frames(silence, fs, config.output_format, conn->enable_dither,
                                           conn->previous_random_number);
                  config.output->play(silence, fs);
                  free(silence);
                }
                frame_gap -= fs;
              }
              conn->ab_buffering = 0;
            }
          }
#ifdef CONFIG_METADATA
          if (conn->ab_buffering == 0) {
            debug(2, "prsm");
            send_ssnc_metadata('prsm', NULL, 0,
                               0); // "resume", but don't wait if the queue is locked
          }
#endif
        }
      }
    }

    // Here, we work out whether to release a packet or wait
    // We release a packet when the time is right.

    // To work out when the time is right, we need to take account of (1) the actual time the packet
    // should be released,
    // (2) the latency requested, (3) the audio backend latency offset and (4) the desired length of
    // the audio backend's buffer

    // The time is right if the current time is later or the same as
    // The packet time + (latency + latency offset - backend_buffer_length).
    // Note: the last three items are expressed in frames and must be converted to time.

    int do_wait = 0; // don't wait unless we can really prove we must
    if ((conn->ab_synced) && (curframe) && (curframe->ready) && (curframe->given_timestamp)) {
      do_wait =
          1; // if the current frame exists and is ready, then wait unless it's time to let it go...

      // here, get the time to play the current frame.

      if (have_timestamp_timing_information(conn)) { // if we have a reference time

        uint64_t time_to_play;
        uint32_t effective_latency = conn->latency;

        switch (get_and_check_effective_latency(conn, &effective_latency,
                                                config.audio_backend_latency_offset -
                                                    config.audio_backend_buffer_desired_length)) {
        case -1:
          // this means that the latency is negative, i.e. the packet must be played before its
          // time. This is clearly a mistake, and can arise if the combination of a large negative
          // latency offset and the desired buffer length are greater than the latency chosen by the
          // player. For example if the latency is 88200 frames (2 seconds), the
          // audio_backend_latency_offset is -1.9 and the audio_backend_buffer_desired_length is 0.2
          // the effective latency is -0.1, so the frame must be played 0.1 seconds before it is
          // time-tagged for, and in reality before it arrives at the player.

          // to deal with the problem, rather than block the packets, we'll just let 'em go...

          //
          if (conn->unachievable_audio_backend_latency_offset_notified == 0) {
            warn("Negative latency! A latency of %d frames requested by the player, when combined "
                 "with an audio_backend_latency_offset of %f seconds an "
                 "audio_backend_buffer_desired_length of %f seconds, would make the overall "
                 "latency negative. No latency is used. Synchronisation may fail.",
                 conn->latency, config.audio_backend_latency_offset,
                 config.audio_backend_buffer_desired_length);
            conn->unachievable_audio_backend_latency_offset_notified = 1;
          };
          time_to_play = local_time_now; // pretend the frame should be played now...
          break;
        case 1:
          if (conn->unachievable_audio_backend_latency_offset_notified == 0) {
            warn("Latency too long! An audio_backend_latency_offset of %f seconds combined with an "
                 "audio_backend_buffer_desired_length of %f seconds  may exceed the frame "
                 "buffering capacity.",
                 config.audio_backend_latency_offset, config.audio_backend_buffer_desired_length);
            conn->unachievable_audio_backend_latency_offset_notified = 1;
          };
          time_to_play = local_time_now; // pretend the frame should be played now...
          break;
        default:
          frame_to_local_time(curframe->given_timestamp +
                                  effective_latency, // this will go modulo 2^32
                              &time_to_play, conn);
          break;
        }

        if (local_time_now >= time_to_play) {
          do_wait = 0;
        }
      }
    }
    if (do_wait == 0)
      if ((conn->ab_synced != 0) && (conn->ab_read == conn->ab_write)) { // the buffer is empty!
        if (notified_buffer_empty == 0) {
          debug(3, "Buffers exhausted.");
          notified_buffer_empty = 1;
          // reset_input_flow_metrics(conn); // don't do a full flush parameters reset
          conn->initial_reference_time = 0;
          conn->initial_reference_timestamp = 0;
        }
        do_wait = 1;
      }
    wait = (conn->ab_buffering || (do_wait != 0) || (!conn->ab_synced));

    if (wait) {
      uint64_t time_to_wait_for_wakeup_ns =
          1000000000 / conn->input_rate;     // this is time period of one frame
      time_to_wait_for_wakeup_ns *= 2 * 352; // two full 352-frame packets
      time_to_wait_for_wakeup_ns /= 3;       // two thirds of a packet time

#ifdef COMPILE_FOR_LINUX_AND_FREEBSD_AND_CYGWIN_AND_OPENBSD
      uint64_t time_of_wakeup_ns = local_time_now + time_to_wait_for_wakeup_ns;
      uint64_t sec = time_of_wakeup_ns / 1000000000;
      uint64_t nsec = time_of_wakeup_ns % 1000000000;

      struct timespec time_of_wakeup;
      time_of_wakeup.tv_sec = sec;
      time_of_wakeup.tv_nsec = nsec;
      //      pthread_cond_timedwait(&conn->flowcontrol, &conn->ab_mutex, &time_of_wakeup);
      int rc = pthread_cond_timedwait(&conn->flowcontrol, &conn->ab_mutex,
                                      &time_of_wakeup); // this is a pthread cancellation point
      if ((rc != 0) && (rc != ETIMEDOUT))
        debug(3, "pthread_cond_timedwait returned error code %d.", rc);
#endif
#ifdef COMPILE_FOR_OSX
      uint64_t sec = time_to_wait_for_wakeup_ns / 1000000000;
      uint64_t nsec = time_to_wait_for_wakeup_ns % 1000000000;
      struct timespec time_to_wait;
      time_to_wait.tv_sec = sec;
      time_to_wait.tv_nsec = nsec;
      pthread_cond_timedwait_relative_np(&conn->flowcontrol, &conn->ab_mutex, &time_to_wait);
#endif
    }
  } while (wait);

  // seq_t read = conn->ab_read;
  if (curframe) {
    if (!curframe->ready) {
      // debug(1, "Supplying a silent frame for frame %u", read);
      conn->missing_packets++;
      curframe->given_timestamp = 0; // indicate a silent frame should be substituted
    }
    curframe->ready = 0;
  }
  conn->ab_read = SUCCESSOR(conn->ab_read);
  pthread_cleanup_pop(1);
  return curframe;
}

static inline int32_t mean_32(int32_t a, int32_t b) {
  int64_t al = a;
  int64_t bl = b;
  int64_t mean = (al + bl) / 2;
  int32_t r = (int32_t)mean;
  if (r != mean)
    debug(1, "Error calculating average of two int32_ts");
  return r;
}

// this takes an array of signed 32-bit integers and (a) removes or inserts a frame as specified in
// stuff,
// (b) multiplies each sample by the fixedvolume (a 16-bit quantity)
// (c) dithers the result to the output size 32/24/16/8 bits
// (d) outputs the result in the approprate format
// formats accepted so far include U8, S8, S16, S24, S24_3LE, S24_3BE and S32

// stuff: 1 means add 1; 0 means do nothing; -1 means remove 1
static int stuff_buffer_basic_32(int32_t *inptr, int length, sps_format_t l_output_format,
                                 char *outptr, int stuff, int dither, rtsp_conn_info *conn) {
  int tstuff = stuff;
  char *l_outptr = outptr;
  if ((stuff > 1) || (stuff < -1) || (length < 100)) {
    // debug(1, "Stuff argument to stuff_buffer must be from -1 to +1 and length >100.");
    tstuff = 0; // if any of these conditions hold, don't stuff anything/
  }

  int i;
  int stuffsamp = length;
  if (tstuff)
    //      stuffsamp = rand() % (length - 1);
    stuffsamp =
        (rand() % (length - 2)) + 1; // ensure there's always a sample before and after the item

  for (i = 0; i < stuffsamp; i++) { // the whole frame, if no stuffing
    process_sample(*inptr++, &l_outptr, l_output_format, conn->fix_volume, dither, conn);
    process_sample(*inptr++, &l_outptr, l_output_format, conn->fix_volume, dither, conn);
  };
  if (tstuff) {
    if (tstuff == 1) {
      // debug(3, "+++++++++");
      // interpolate one sample
      process_sample(mean_32(inptr[-2], inptr[0]), &l_outptr, l_output_format, conn->fix_volume,
                     dither, conn);
      process_sample(mean_32(inptr[-1], inptr[1]), &l_outptr, l_output_format, conn->fix_volume,
                     dither, conn);
    } else if (stuff == -1) {
      // debug(3, "---------");
      inptr++;
      inptr++;
    }

    // if you're removing, i.e. stuff < 0, copy that much less over. If you're adding, do all the
    // rest.
    int remainder = length;
    if (tstuff < 0)
      remainder = remainder + tstuff; // don't run over the correct end of the output buffer

    for (i = stuffsamp; i < remainder; i++) {
      process_sample(*inptr++, &l_outptr, l_output_format, conn->fix_volume, dither, conn);
      process_sample(*inptr++, &l_outptr, l_output_format, conn->fix_volume, dither, conn);
    }
  }
  conn->amountStuffed = tstuff;
  return length + tstuff;
}

#ifdef CONFIG_SOXR
// this takes an array of signed 32-bit integers and
// (a) uses libsoxr to
// resample the array to have one more or one less frame, as specified in
// stuff,
// (b) multiplies each sample by the fixedvolume (a 16-bit quantity)
// (c) dithers the result to the output size 32/24/16/8 bits
// (d) outputs the result in the approprate format
// formats accepted so far include U8, S8, S16, S24, S24_3LE, S24_3BE and S32

int32_t stat_n = 0;
double stat_mean = 0.0;
double stat_M2 = 0.0;
double longest_soxr_execution_time = 0.0;
int64_t packets_processed = 0;

int stuff_buffer_soxr_32(int32_t *inptr, int32_t *scratchBuffer, int length,
                         sps_format_t l_output_format, char *outptr, int stuff, int dither,
                         rtsp_conn_info *conn) {
  if (scratchBuffer == NULL) {
    die("soxr scratchBuffer not initialised.");
  }
  packets_processed++;
  int tstuff = stuff;
  if ((stuff > 1) || (stuff < -1) || (length < 100)) {
    // debug(1, "Stuff argument to stuff_buffer must be from -1 to +1 and length >100.");
    tstuff = 0; // if any of these conditions hold, don't stuff anything/
  }

  if (tstuff) {
    // debug(1,"Stuff %d.",stuff);

    soxr_io_spec_t io_spec;
    io_spec.itype = SOXR_INT32_I;
    io_spec.otype = SOXR_INT32_I;
    io_spec.scale = 1.0; // this seems to crash if not = 1.0
    io_spec.e = NULL;
    io_spec.flags = 0;

    size_t odone;

    uint64_t soxr_start_time = get_absolute_time_in_ns();

    soxr_error_t error = soxr_oneshot(length, length + tstuff, 2, // Rates and # of chans.
                                      inptr, length, NULL,        // Input.
                                      scratchBuffer, length + tstuff, &odone, // Output.
                                      &io_spec,    // Input, output and transfer spec.
                                      NULL, NULL); // Default configuration.

    if (error)
      die("soxr error: %s\n", "error: %s\n", soxr_strerror(error));

    if (odone > (size_t)(length + 1))
      die("odone = %u!\n", odone);

    // mean and variance calculations from "online_variance" algorithm at
    // https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance#Online_algorithm

    double soxr_execution_time = (get_absolute_time_in_ns() - soxr_start_time) * 0.000000001;
    // debug(1,"soxr_execution_time_us: %10.1f",soxr_execution_time_us);
    if (soxr_execution_time > longest_soxr_execution_time)
      longest_soxr_execution_time = soxr_execution_time;
    stat_n += 1;
    double stat_delta = soxr_execution_time - stat_mean;
    stat_mean += stat_delta / stat_n;
    stat_M2 += stat_delta * (soxr_execution_time - stat_mean);

    int i;
    int32_t *ip, *op;
    ip = inptr;
    op = scratchBuffer;

    const int gpm = 5;
    // keep the first (dpm) samples, to mitigate the Gibbs phenomenon
    for (i = 0; i < gpm; i++) {
      *op++ = *ip++;
      *op++ = *ip++;
    }

    // keep the last (dpm) samples, to mitigate the Gibbs phenomenon

    // pointer arithmetic, baby -- it's da bomb.
    op = scratchBuffer + (length + tstuff - gpm) * 2;
    ip = inptr + (length - gpm) * 2;
    for (i = 0; i < gpm; i++) {
      *op++ = *ip++;
      *op++ = *ip++;
    }

    // now, do the volume, dither and formatting processing
    ip = scratchBuffer;
    char *l_outptr = outptr;
    for (i = 0; i < length + tstuff; i++) {
      process_sample(*ip++, &l_outptr, l_output_format, conn->fix_volume, dither, conn);
      process_sample(*ip++, &l_outptr, l_output_format, conn->fix_volume, dither, conn);
    };

  } else { // the whole frame, if no stuffing

    // now, do the volume, dither and formatting processing
    int32_t *ip = inptr;
    char *l_outptr = outptr;
    int i;

    for (i = 0; i < length; i++) {
      process_sample(*ip++, &l_outptr, l_output_format, conn->fix_volume, dither, conn);
      process_sample(*ip++, &l_outptr, l_output_format, conn->fix_volume, dither, conn);
    };
  }

  if (packets_processed % 1250 == 0) {
    debug(3,
          "soxr_oneshot execution time in nanoseconds: mean, standard deviation and max "
          "for %" PRId32 " interpolations in the last "
          "1250 packets. %10.6f, %10.6f, %10.6f.",
          stat_n, stat_mean, stat_n <= 1 ? 0.0 : sqrtf(stat_M2 / (stat_n - 1)),
          longest_soxr_execution_time);
    stat_n = 0;
    stat_mean = 0.0;
    stat_M2 = 0.0;
    longest_soxr_execution_time = 0.0;
  }

  conn->amountStuffed = tstuff;
  return length + tstuff;
}
#endif

void player_thread_initial_cleanup_handler(__attribute__((unused)) void *arg) {
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  debug(3, "Connection %d: player thread main loop exit via player_thread_initial_cleanup_handler.",
        conn->connection_number);
}

void player_thread_cleanup_handler(void *arg) {
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  debug(3, "Connection %d: player thread main loop exit via player_thread_cleanup_handler.",
        conn->connection_number);

  if (config.output->stop)
    config.output->stop();

  if (config.statistics_requested) {
    int rawSeconds = (int)difftime(time(NULL), conn->playstart);
    int elapsedHours = rawSeconds / 3600;
    int elapsedMin = (rawSeconds / 60) % 60;
    int elapsedSec = rawSeconds % 60;
    if (conn->frame_rate_status)
      inform("Playback Stopped. Total playing time %02d:%02d:%02d. Input: %0.2f, output: %0.2f "
             "frames per second.",
             elapsedHours, elapsedMin, elapsedSec, conn->input_frame_rate, conn->frame_rate);
    else
      inform("Playback Stopped. Total playing time %02d:%02d:%02d. Input: %0.2f frames per second.",
             elapsedHours, elapsedMin, elapsedSec, conn->input_frame_rate);
  }

#ifdef CONFIG_DACP_CLIENT
  relinquish_dacp_server_information(
      conn); // say it doesn't belong to this conversation thread any more...
#else
  mdns_dacp_monitor_set_id(NULL); // say we're not interested in following that DACP id any more
#endif

  debug(3, "Cancelling timing, control and audio threads...");
  debug(3, "Cancel timing thread.");
  pthread_cancel(conn->rtp_timing_thread);
  debug(3, "Join timing thread.");
  pthread_join(conn->rtp_timing_thread, NULL);
  debug(3, "Timing thread terminated.");
  debug(3, "Cancel control thread.");
  pthread_cancel(conn->rtp_control_thread);
  debug(3, "Join control thread.");
  pthread_join(conn->rtp_control_thread, NULL);
  debug(3, "Control thread terminated.");
  debug(3, "Cancel audio thread.");
  pthread_cancel(conn->rtp_audio_thread);
  debug(3, "Join audio thread.");
  pthread_join(conn->rtp_audio_thread, NULL);
  debug(3, "Audio thread terminated.");

  if (conn->outbuf) {
    free(conn->outbuf);
    conn->outbuf = NULL;
  }
  if (conn->sbuf) {
    free(conn->sbuf);
    conn->sbuf = NULL;
  }
  if (conn->tbuf) {
    free(conn->tbuf);
    conn->tbuf = NULL;
  }

  if (conn->statistics) {
    free(conn->statistics);
    conn->statistics = NULL;
  }
  free_audio_buffers(conn);
  if (conn->stream.type == ast_apple_lossless)
    terminate_decoders(conn);

  clear_reference_timestamp(conn);
  conn->rtp_running = 0;
  pthread_setcancelstate(oldState, NULL);
}

void *player_thread_func(void *arg) {
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  // pthread_cleanup_push(player_thread_initial_cleanup_handler, arg);
  conn->packet_count = 0;
  conn->packet_count_since_flush = 0;
  conn->previous_random_number = 0;
  conn->input_bytes_per_frame = 4;
  conn->decoder_in_use = 0;
  conn->ab_buffering = 1;
  conn->ab_synced = 0;
  conn->first_packet_timestamp = 0;
  conn->flush_requested = 0;
  conn->flush_output_flushed = 0; // only send a flush command to the output device once
  conn->flush_rtp_timestamp = 0;  // it seems this number has a special significance -- it seems to
                                  // be used as a null operand, so we'll use it like that too
  conn->fix_volume = 0x10000;

  if (conn->latency == 0) {
    debug(3, "No latency has (yet) been specified. Setting 88,200 (2 seconds) frames "
             "as a default.");
    conn->latency = 88200;
  }

  if (conn->stream.type == ast_apple_lossless)
    init_alac_decoder((int32_t *)&conn->stream.fmtp,
                      conn); // this sets up incoming rate, bit depth, channels.
                             // No pthread cancellation point in here
  // This must be after init_alac_decoder
  init_buffer(conn); // will need a corresponding deallocation. No cancellation points in here

  if (conn->stream.encrypted) {
#ifdef CONFIG_MBEDTLS
    memset(&conn->dctx, 0, sizeof(mbedtls_aes_context));
    mbedtls_aes_setkey_dec(&conn->dctx, conn->stream.aeskey, 128);
#endif

#ifdef CONFIG_POLARSSL
    memset(&conn->dctx, 0, sizeof(aes_context));
    aes_setkey_dec(&conn->dctx, conn->stream.aeskey, 128);
#endif

#ifdef CONFIG_OPENSSL
    AES_set_decrypt_key(conn->stream.aeskey, 128, &conn->aes);
#endif
  }

  conn->timestamp_epoch = 0; // indicate that the next timestamp will be the first one.
  conn->maximum_timestamp_interval = conn->input_rate * 60; // actually there shouldn't be more than
                                                            // about 13 seconds of a gap between
                                                            // successive rtptimes, at worst

  conn->output_sample_ratio = config.output_rate / conn->input_rate;

  //  debug(1, "Output sample ratio is %d.", conn->output_sample_ratio);

  conn->max_frame_size_change =
      1 * conn->output_sample_ratio; // we add or subtract one frame at the nominal
                                     // rate, multiply it by the frame ratio.
                                     // but, on some occasions, more than one frame could be added

  switch (config.output_format) {
  case SPS_FORMAT_S24_3LE:
  case SPS_FORMAT_S24_3BE:
    conn->output_bytes_per_frame = 6;
    break;

  case SPS_FORMAT_S24:
  case SPS_FORMAT_S24_LE:
  case SPS_FORMAT_S24_BE:
    conn->output_bytes_per_frame = 8;
    break;
  case SPS_FORMAT_S32:
  case SPS_FORMAT_S32_LE:
  case SPS_FORMAT_S32_BE:
    conn->output_bytes_per_frame = 8;
    break;
  default:
    conn->output_bytes_per_frame = 4;
  }

  debug(3, "Output frame bytes is %d.", conn->output_bytes_per_frame);

  conn->dac_buffer_queue_minimum_length = (uint64_t)(
      config.audio_backend_buffer_interpolation_threshold_in_seconds * config.output_rate);
  debug(3, "dac_buffer_queue_minimum_length is %" PRIu64 " frames.",
        conn->dac_buffer_queue_minimum_length);

  conn->session_corrections = 0;
  // conn->play_segment_reference_frame = 0; // zero signals that we are not in a play segment

  // check that there are enough buffers to accommodate the desired latency and the latency offset

  int maximum_latency =
      conn->latency + (int)(config.audio_backend_latency_offset * config.output_rate);
  if ((maximum_latency + (352 - 1)) / 352 + 10 > BUFFER_FRAMES)
    die("Not enough buffers available for a total latency of %d frames. A maximum of %d 352-frame "
        "packets may be accommodated.",
        maximum_latency, BUFFER_FRAMES);
  conn->connection_state_to_output = get_requested_connection_state_to_output();
// this is about half a minute
//#define trend_interval 3758

// this is about 8 seconds
#define trend_interval 1003

  int number_of_statistics, oldest_statistic, newest_statistic;
  int at_least_one_frame_seen = 0;
  int at_least_one_frame_seen_this_session = 0;
  int64_t tsum_of_sync_errors, tsum_of_corrections, tsum_of_insertions_and_deletions,
      tsum_of_drifts;
  int64_t previous_sync_error = 0, previous_correction = 0;
  uint64_t minimum_dac_queue_size = UINT64_MAX;
  int32_t minimum_buffer_occupancy = INT32_MAX;
  int32_t maximum_buffer_occupancy = INT32_MIN;

  conn->playstart = time(NULL);

  conn->frame_rate = 0.0;
  conn->frame_rate_status = 0;

  conn->input_frame_rate = 0.0;
  conn->input_frame_rate_starting_point_is_valid = 0;

  conn->buffer_occupancy = 0;

  int play_samples = 0;
  uint64_t current_delay;
  int play_number = 0;
  conn->play_number_after_flush = 0;
  //  int last_timestamp = 0; // for debugging only
  conn->time_of_last_audio_packet = 0;
  // conn->shutdown_requested = 0;
  number_of_statistics = oldest_statistic = newest_statistic = 0;
  tsum_of_sync_errors = tsum_of_corrections = tsum_of_insertions_and_deletions = tsum_of_drifts = 0;

  const int print_interval = trend_interval; // don't ask...
  // I think it's useful to keep this prime to prevent it from falling into a pattern with some
  // other process.

  static char rnstate[256];
  initstate(time(NULL), rnstate, 256);

  signed short *inbuf;
  int inbuflength;

  unsigned int output_bit_depth = 16; // default;

  switch (config.output_format) {
  case SPS_FORMAT_S8:
  case SPS_FORMAT_U8:
    output_bit_depth = 8;
    break;
  case SPS_FORMAT_S16:
  case SPS_FORMAT_S16_LE:
  case SPS_FORMAT_S16_BE:
    output_bit_depth = 16;
    break;
  case SPS_FORMAT_S24:
  case SPS_FORMAT_S24_LE:
  case SPS_FORMAT_S24_BE:
  case SPS_FORMAT_S24_3LE:
  case SPS_FORMAT_S24_3BE:
    output_bit_depth = 24;
    break;
  case SPS_FORMAT_S32:
  case SPS_FORMAT_S32_LE:
  case SPS_FORMAT_S32_BE:
    output_bit_depth = 32;
    break;
  case SPS_FORMAT_UNKNOWN:
    die("Unknown format choosing output bit depth");
    break;
  case SPS_FORMAT_AUTO:
    die("Invalid format -- SPS_FORMAT_AUTO -- choosing output bit depth");
    break;
  case SPS_FORMAT_INVALID:
    die("Invalid format -- SPS_FORMAT_INVALID -- choosing output bit depth");
    break;
  }

  debug(3, "Output bit depth is %d.", output_bit_depth);

  if (conn->input_bit_depth > output_bit_depth) {
    debug(3, "Dithering will be enabled because the input bit depth is greater than the output bit "
             "depth");
  }
  if (config.output->parameters == NULL) {
    debug(3, "Dithering will be enabled because the output volume is being altered in software");
  }

  if ((config.output->parameters == NULL) || (conn->input_bit_depth > output_bit_depth) ||
      (config.playback_mode == ST_mono))
    conn->enable_dither = 1;

  // remember, the output device may never have been initialised prior to this call
  config.output->start(config.output_rate, config.output_format); // will need a corresponding stop

  // we need an intermediate "transition" buffer

  conn->tbuf = malloc(
      sizeof(int32_t) * 2 *
      (conn->max_frames_per_packet * conn->output_sample_ratio + conn->max_frame_size_change));
  if (conn->tbuf == NULL)
    die("Failed to allocate memory for the transition buffer.");

  // initialise this, because soxr stuffing might be chosen later

  conn->sbuf = malloc(
      sizeof(int32_t) * 2 *
      (conn->max_frames_per_packet * conn->output_sample_ratio + conn->max_frame_size_change));
  if (conn->sbuf == NULL)
    die("Failed to allocate memory for the sbuf buffer.");

  // The size of these dependents on the number of frames, the size of each frame and the maximum
  // size change
  conn->outbuf = malloc(
      conn->output_bytes_per_frame *
      (conn->max_frames_per_packet * conn->output_sample_ratio + conn->max_frame_size_change));
  if (conn->outbuf == NULL)
    die("Failed to allocate memory for an output buffer.");
  conn->first_packet_timestamp = 0;
  conn->missing_packets = conn->late_packets = conn->too_late_packets = conn->resend_requests = 0;
  int sync_error_out_of_bounds =
      0; // number of times in a row that there's been a serious sync error

  conn->statistics = malloc(sizeof(stats_t) * trend_interval);
  if (conn->statistics == NULL)
    die("Failed to allocate a statistics buffer");

  conn->framesProcessedInThisEpoch = 0;
  conn->framesGeneratedInThisEpoch = 0;
  conn->correctionsRequestedInThisEpoch = 0;

  if (config.statistics_requested) {
    if ((config.output->delay)) {
      if (config.no_sync == 0) {
        inform("sync error in milliseconds, "
               "net correction in ppm, "
               "corrections in ppm, "
               "total packets, "
               "missing packets, "
               "late packets, "
               "too late packets, "
               "resend requests, "
               "min DAC queue size, "
               "min buffer occupancy, "
               "max buffer occupancy, "
               "source nominal frames per second, "
               "source actual frames per second, "
               "output frames per second, "
               "source clock drift in ppm, "
               "source clock drift sample count, "
               "rough calculated correction in ppm");
      } else {
        inform("sync error in milliseconds, "
               "total packets, "
               "missing packets, "
               "late packets, "
               "too late packets, "
               "resend requests, "
               "min DAC queue size, "
               "min buffer occupancy, "
               "max buffer occupancy, "
               "source nominal frames per second, "
               "source actual frames per second, "
               "source clock drift in ppm, "
               "source clock drift sample count");
      }
    } else {
      inform("total packets, "
             "missing packets, "
             "late packets, "
             "too late packets, "
             "resend requests, "
             "min buffer occupancy, "
             "max buffer occupancy, "
             "source nominal frames per second, "
             "source actual frames per second, "
             "source clock drift in ppm, "
             "source clock drift sample count");
    }
  }

  // create and start the timing, control and audio receiver threads
  pthread_create(&conn->rtp_audio_thread, NULL, &rtp_audio_receiver, (void *)conn);
  pthread_create(&conn->rtp_control_thread, NULL, &rtp_control_receiver, (void *)conn);
  pthread_create(&conn->rtp_timing_thread, NULL, &rtp_timing_receiver, (void *)conn);

  pthread_cleanup_push(player_thread_cleanup_handler, arg); // undo what's been done so far

  // stop looking elsewhere for DACP stuff
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);

#ifdef CONFIG_DACP_CLIENT
  set_dacp_server_information(conn);
#else
  mdns_dacp_monitor_set_id(conn->dacp_id);
#endif

  pthread_setcancelstate(oldState, NULL);

  // set the default volume to whatever it was before, as stored in the config airplay_volume
  debug(2, "Set initial volume to %f.", config.airplay_volume);
  player_volume(config.airplay_volume, conn); // will contain a cancellation point if asked to wait

  debug(2, "Play begin");
  while (1) {
    pthread_testcancel();                     // allow a pthread_cancel request to take effect.
    abuf_t *inframe = buffer_get_frame(conn); // this has cancellation point(s), but it's not
                                              // guaranteed that they'll always be executed
    if (inframe) {
      inbuf = inframe->data;
      inbuflength = inframe->length;
      if (inbuf) {
        play_number++;
        //        if (play_number % 100 == 0)
        //          debug(3, "Play frame %d.", play_number);
        conn->play_number_after_flush++;
        if (inframe->given_timestamp == 0) {
          debug(2,
                "Player has supplied a silent frame, (possibly frame %u) for play number %d, "
                "status 0x%X after %u resend requests.",
                SUCCESSOR(conn->last_seqno_read), play_number, inframe->status,
                inframe->resend_request_number);
          conn->last_seqno_read =
              SUCCESSOR(conn->last_seqno_read); // manage the packet out of sequence minder

          void *silence = malloc(conn->output_bytes_per_frame * conn->max_frames_per_packet *
                                 conn->output_sample_ratio);
          if (silence == NULL) {
            debug(1, "Failed to allocate memory for a silent frame silence buffer.");
          } else {
            // the player may change the contents of the buffer, so it has to be zeroed each time;
            // might as well malloc and free it locally
            conn->previous_random_number = generate_zero_frames(
                silence, conn->max_frames_per_packet * conn->output_sample_ratio,
                config.output_format, conn->enable_dither, conn->previous_random_number);
            config.output->play(silence, conn->max_frames_per_packet * conn->output_sample_ratio);
            free(silence);
          }
        } else if (conn->play_number_after_flush < 10) {
          /*
          int64_t difference = 0;
          if (last_timestamp)
            difference = inframe->timestamp - last_timestamp;
          last_timestamp = inframe->timestamp;
          debug(1, "Play number %d, monotonic timestamp %llx, difference
          %lld.",conn->play_number_after_flush,inframe->timestamp,difference);
          */
          void *silence = malloc(conn->output_bytes_per_frame * conn->max_frames_per_packet *
                                 conn->output_sample_ratio);
          if (silence == NULL) {
            debug(1, "Failed to allocate memory for a flush silence buffer.");
          } else {
            // the player may change the contents of the buffer, so it has to be zeroed each time;
            // might as well malloc and free it locally
            conn->previous_random_number = generate_zero_frames(
                silence, conn->max_frames_per_packet * conn->output_sample_ratio,
                config.output_format, conn->enable_dither, conn->previous_random_number);
            config.output->play(silence, conn->max_frames_per_packet * conn->output_sample_ratio);
            free(silence);
          }
        } else {

          if (((config.output->parameters == NULL) && (config.ignore_volume_control == 0) &&
               (config.airplay_volume != 0.0)) ||
              (conn->input_bit_depth > output_bit_depth) || (config.playback_mode == ST_mono))
            conn->enable_dither = 1;
          else
            conn->enable_dither = 0;

          // here, let's transform the frame of data, if necessary

          switch (conn->input_bit_depth) {
          case 16: {
            int i, j;
            int16_t ls, rs;
            int32_t ll = 0, rl = 0;
            int16_t *inps = inbuf;
            // int16_t *outps = tbuf;
            int32_t *outpl = (int32_t *)conn->tbuf;
            for (i = 0; i < inbuflength; i++) {
              ls = *inps++;
              rs = *inps++;

              // here, do the mode stuff -- mono / reverse stereo / leftonly / rightonly
              // also, raise the 16-bit samples to 32 bits.

              switch (config.playback_mode) {
              case ST_mono: {
                int32_t both = ls + rs;
                both = both << (16 - 1); // keep all 17 bits of the sum of the 16bit left and right
                                         // -- the 17th bit will influence dithering later
                ll = both;
                rl = both;
              } break;
              case ST_reverse_stereo: {
                ll = rs;
                rl = ls;
                ll = ll << 16;
                rl = rl << 16;
              } break;
              case ST_left_only:
                rl = ls;
                ll = ls;
                ll = ll << 16;
                rl = rl << 16;
                break;
              case ST_right_only:
                ll = rs;
                rl = rs;
                ll = ll << 16;
                rl = rl << 16;
                break;
              case ST_stereo:
                ll = ls;
                rl = rs;
                ll = ll << 16;
                rl = rl << 16;
                break; // nothing extra to do
              }

              // here, replicate the samples if you're upsampling

              for (j = 0; j < conn->output_sample_ratio; j++) {
                *outpl++ = ll;
                *outpl++ = rl;
              }
            }

          } break;
          default:
            die("Shairport Sync only supports 16 bit input");
          }

          at_least_one_frame_seen = 1;

          // We have a frame of data. We need to see if we want to add or remove a frame from it to
          // keep in sync.
          // So we calculate the timing error for the first frame in the DAC.
          // If it's ahead of time, we add one audio frame to this frame to delay a subsequent frame
          // If it's late, we remove an audio frame from this frame to bring a subsequent frame
          // forward in time

          // now, go back as far as the total latency less, say, 100 ms, and check the presence of
          // frames from then onwards

          inbuflength *= conn->output_sample_ratio;
          /*
          uint32_t reference_timestamp;
          uint64_t reference_timestamp_time, remote_reference_timestamp_time;
          get_reference_timestamp_stuff(&reference_timestamp, &reference_timestamp_time,
                                        &remote_reference_timestamp_time, conn); // types okay
          */

          // nt is the rtp timestamp of the first frame of the current packet of
          // frames from the source.
          // multiply it by the output frame ratio to get, effectively, the rtp timestamp
          // of the first frame of the corresponding packet of output frames.

          uint64_t nt;
          nt = inframe->given_timestamp; // uint32_t to int64_t
          nt = nt * conn->output_sample_ratio;

          uint64_t local_time_now = get_absolute_time_in_ns(); // types okay

          // This is the timing error for the next audio frame in the DAC, if applicable
          int64_t sync_error = 0;

          int amount_to_stuff = 0;

          // check sequencing
          if (conn->last_seqno_read == -1)
            conn->last_seqno_read =
                inframe->sequence_number; // int32_t from seq_t, i.e. uint16_t, so okay.
          else {
            conn->last_seqno_read =
                SUCCESSOR(conn->last_seqno_read); // int32_t from seq_t, i.e. uint16_t, so okay.
            if (inframe->sequence_number !=
                conn->last_seqno_read) { // seq_t, ei.e. uint16_t and int32_t, so okay
              debug(2,
                    "Player: packets out of sequence: expected: %u, got: %u, with ab_read: %u "
                    "and ab_write: %u.",
                    conn->last_seqno_read, inframe->sequence_number, conn->ab_read, conn->ab_write);
              conn->last_seqno_read = inframe->sequence_number; // reset warning...
            }
          }

          conn->buffer_occupancy = seq_diff(conn->ab_write, conn->ab_read); // int32_t from int16_t

          if (conn->buffer_occupancy < minimum_buffer_occupancy)
            minimum_buffer_occupancy = conn->buffer_occupancy;

          if (conn->buffer_occupancy > maximum_buffer_occupancy)
            maximum_buffer_occupancy = conn->buffer_occupancy;

          // here, we want to check (a) if we are meant to do synchronisation,
          // (b) if we have a delay procedure, (c) if we can get the delay.

          // If any of these are false, we don't do any synchronisation stuff

          int resp = -1; // use this as a flag -- if negative, we can't rely on a real known delay

          if (config.output->delay) {
            long l_delay;
            resp = config.output->delay(&l_delay);
            if (resp == 0) { // no error
              current_delay = l_delay;
              if (l_delay >= 0)
                current_delay = l_delay;
              else {
                debug(2, "Underrun of %ld frames reported, but ignored.", l_delay);
                current_delay =
                    0; // could get a negative value if there was underrun, but ignore it.
              }
              if (current_delay < minimum_dac_queue_size) {
                minimum_dac_queue_size = current_delay; // update for display later
              }
            } else {
              current_delay = 0;
              if ((resp == sps_extra_code_output_stalled) &&
                  (conn->unfixable_error_reported == 0)) {
                conn->unfixable_error_reported = 1;
                if (config.cmd_unfixable) {
                  warn("Connection %d: An unfixable error has been detected -- output device is "
                       "stalled. Executing the "
                       "\"run_this_if_an_unfixable_error_is_detected\" command.",
                       conn->connection_number);
                  command_execute(config.cmd_unfixable, "output_device_stalled", 1);
                } else {
                  warn("Connection %d: An unfixable error has been detected -- output device is "
                       "stalled. \"No "
                       "run_this_if_an_unfixable_error_is_detected\" command provided -- nothing "
                       "done.",
                       conn->connection_number);
                }
              } else
                debug(3, "Delay error %d when checking running latency.", resp);
            }
          }

          if (resp == 0) {

            uint32_t should_be_frame_32;
            local_time_to_frame(local_time_now, &should_be_frame_32, conn);
            // int64_t should_be_frame = ((int64_t)should_be_frame_32) * conn->output_sample_ratio;

            int64_t delay =
                int64_mod_difference(should_be_frame_32 * conn->output_sample_ratio,
                                     nt - current_delay, UINT32_MAX * conn->output_sample_ratio);

            // int64_t delay = should_be_frame - (nt - current_delay); // all int64_t

            // the original frame numbers are unsigned 32-bit integers that roll over modulo 2^32
            // hence these delay figures will be unsigned numbers that roll over modulo 2^32 *
            // conn->output_sample_ratio; therefore, calculating the delay must be done in the light
            // of possible rollover

            sync_error =
                delay - ((int64_t)conn->latency * conn->output_sample_ratio +
                         (int64_t)(config.audio_backend_latency_offset *
                                   config.output_rate)); // int64_t from int64_t - int32_t, so okay

            if (at_least_one_frame_seen_this_session == 0) {
              at_least_one_frame_seen_this_session = 1;

              // debug(2,"first frame real sync error (positive --> late): %" PRId64 " frames.",
              // sync_error);

              // this is a sneaky attempt to make a final adjustment to the timing of the first
              // packet

              // the very first packet generally has a first_frame_early_bias subtracted from its
              // timing to make it more likely that it will be early than late, making it possible
              // to compensate for it be adding a few frames of silence.

              // debug(2,"first frame real sync error (positive --> late): %" PRId64 " frames.",
              // sync_error);

              // remove the bias when reporting the error to make it the true error

              debug(2,
                    "first frame sync error (positive --> late): %" PRId64
                    " frames, %.3f mS at %d frames per second output.",
                    sync_error + first_frame_early_bias,
                    (1000.0 * (sync_error + first_frame_early_bias)) / config.output_rate,
                    config.output_rate);

              // if the packet is early, add the frames needed to put it in sync.
              if (sync_error < 0) {
                size_t final_adjustment_length_sized = -sync_error;
                char *final_adjustment_silence =
                    malloc(conn->output_bytes_per_frame * final_adjustment_length_sized);
                if (final_adjustment_silence) {

                  conn->previous_random_number = generate_zero_frames(
                      final_adjustment_silence, final_adjustment_length_sized, config.output_format,
                      conn->enable_dither, conn->previous_random_number);
                  int final_adjustment = -sync_error;
                  final_adjustment = final_adjustment - first_frame_early_bias;
                  debug(2,
                        "final sync adjustment: %" PRId64
                        " silent frames added with a bias of %" PRId64 " frames.",
                        -sync_error, first_frame_early_bias);
                  config.output->play(final_adjustment_silence, final_adjustment_length_sized);
                  free(final_adjustment_silence);
                } else {
                  warn("Failed to allocate memory for a final_adjustment_silence buffer of %d "
                       "frames for a "
                       "sync error of %d frames.",
                       final_adjustment_length_sized, sync_error);
                }
                sync_error = 0; // say the error was fixed!
              }
            }
            // not too sure if abs() is implemented for int64_t, so we'll do it manually
            int64_t abs_sync_error = sync_error;
            if (abs_sync_error < 0)
              abs_sync_error = -abs_sync_error;

            if ((config.no_sync == 0) && (inframe->given_timestamp != 0) &&
                (config.resyncthreshold > 0.0) &&
                (abs_sync_error > config.resyncthreshold * config.output_rate)) {
              /*
              if (abs_sync_error > 3 * config.output_rate) {

                warn("Very large sync error: %" PRId64 " frames, with should_be_frame: %" PRId64
                     ",  nt: %" PRId64 ", current_delay: %" PRId64 ", given timestamp %" PRIX32
                     ", reference timestamp %" PRIX32 ", should_be_frame %" PRIX32 ".",
                     sync_error, should_be_frame, nt, current_delay, inframe->given_timestamp,
                     reference_timestamp, should_be_frame_32);
              }
              */
              sync_error_out_of_bounds++;
            } else {
              sync_error_out_of_bounds = 0;
            }

            if (sync_error_out_of_bounds > 3) {
              // debug(1, "New lost sync with source for %d consecutive packets -- flushing and "
              //          "resyncing. Error: %lld.",
              //        sync_error_out_of_bounds, sync_error);
              sync_error_out_of_bounds = 0;

              int64_t filler_length =
                  (int64_t)(config.resyncthreshold * config.output_rate); // number of samples
              if ((sync_error > 0) && (sync_error > filler_length)) {
                debug(2, "Large positive sync error: %" PRId64 ".", sync_error);
                int64_t local_frames_to_drop = sync_error / conn->output_sample_ratio;
                uint32_t frames_to_drop_sized = local_frames_to_drop;

                debug_mutex_lock(&conn->flush_mutex, 1000, 1);
                conn->flush_rtp_timestamp =
                    inframe->given_timestamp +
                    frames_to_drop_sized; // flush all packets up to (and including?) this
                reset_input_flow_metrics(conn);
                debug_mutex_unlock(&conn->flush_mutex, 3);

              } else if ((sync_error < 0) && ((-sync_error) > filler_length)) {
                debug(2,
                      "Large negative sync error: %" PRId64 " with should_be_frame_32 of %" PRIu32
                      ", nt of %" PRId64 " and current_delay of %" PRId64 ".",
                      sync_error, should_be_frame_32, nt, current_delay);
                int64_t silence_length = -sync_error;
                if (silence_length > (filler_length * 5))
                  silence_length = filler_length * 5;
                size_t silence_length_sized = silence_length;
                char *long_silence = malloc(conn->output_bytes_per_frame * silence_length_sized);
                if (long_silence) {

                  conn->previous_random_number =
                      generate_zero_frames(long_silence, silence_length_sized, config.output_format,
                                           conn->enable_dither, conn->previous_random_number);

                  debug(2, "Play a silence of %d frames.", silence_length_sized);
                  config.output->play(long_silence, silence_length_sized);
                  free(long_silence);
                } else {
                  warn("Failed to allocate memory for a long_silence buffer of %d frames for a "
                       "sync error of %d frames.",
                       silence_length_sized, sync_error);
                }
                reset_input_flow_metrics(conn);
              }
            } else {

              /*
              // before we finally commit to this frame, check its sequencing and timing
              // require a certain error before bothering to fix it...
              if (sync_error > config.tolerance * config.output_rate) { // int64_t > int, okay
                amount_to_stuff = -1;
              }
              if (sync_error < -config.tolerance * config.output_rate) {
                amount_to_stuff = 1;
              }
              */

              if (amount_to_stuff == 0) {
                // use a "V" shaped function to decide if stuffing should occur
                int64_t s = r64i();
                s = s >> 31;
                s = s * config.tolerance * config.output_rate;
                s = (s >> 32) + config.tolerance * config.output_rate; // should be a number from 0
                                                                       // to config.tolerance *
                                                                       // config.output_rate;
                if ((sync_error > 0) && (sync_error > s)) {
                  // debug(1,"Extra stuff -1");
                  amount_to_stuff = -1;
                }
                if ((sync_error < 0) && (sync_error < (-s))) {
                  // debug(1,"Extra stuff +1");
                  amount_to_stuff = 1;
                }
              }

              // try to keep the corrections definitely below 1 in 1000 audio frames

              // calculate the time elapsed since the play session started.

              if (amount_to_stuff) {
                if ((local_time_now) && (conn->first_packet_time_to_play) &&
                    (local_time_now >= conn->first_packet_time_to_play)) {

                  int64_t tp =
                      (local_time_now - conn->first_packet_time_to_play) /
                      1000000000; // seconds int64_t from uint64_t which is always positive, so ok

                  if (tp < 5)
                    amount_to_stuff = 0; // wait at least five seconds
                  /*
                  else if (tp < 30) {
                    if ((random() % 1000) >
                        352) // keep it to about 1:1000 for the first thirty seconds
                      amount_to_stuff = 0;
                  }
                  */
                }
              }

              if (config.no_sync != 0)
                amount_to_stuff = 0; // no stuffing if it's been disabled

              // Apply DSP here

              // check the state of loudness and convolution flags here and don't change them for
              // the frame

              int do_loudness = config.loudness;

#ifdef CONFIG_CONVOLUTION
              int do_convolution = 0;
              if ((config.convolution) && (config.convolver_valid))
                do_convolution = 1;

              // we will apply the convolution gain if convolution is enabled, even if there is no
              // valid convolution happening

              int convolution_is_enabled = 0;
              if (config.convolution)
                convolution_is_enabled = 1;
#endif

              if (do_loudness
#ifdef CONFIG_CONVOLUTION
                  || convolution_is_enabled
#endif
              ) {
                int32_t *tbuf32 = (int32_t *)conn->tbuf;
                float fbuf_l[inbuflength];
                float fbuf_r[inbuflength];

                // Deinterleave, and convert to float
                int i;
                for (i = 0; i < inbuflength; ++i) {
                  fbuf_l[i] = tbuf32[2 * i];
                  fbuf_r[i] = tbuf32[2 * i + 1];
                }

#ifdef CONFIG_CONVOLUTION
                // Apply convolution
                if (do_convolution) {
                  convolver_process_l(fbuf_l, inbuflength);
                  convolver_process_r(fbuf_r, inbuflength);
                }
                if (convolution_is_enabled) {
                  float gain = pow(10.0, config.convolution_gain / 20.0);
                  for (i = 0; i < inbuflength; ++i) {
                    fbuf_l[i] *= gain;
                    fbuf_r[i] *= gain;
                  }
                }
#endif

                if (do_loudness) {
                  // Apply volume and loudness
                  // Volume must be applied here because the loudness filter will increase the
                  // signal level and it would saturate the int32_t otherwise
                  float gain = conn->fix_volume / 65536.0f;
                  // float gain_db = 20 * log10(gain);
                  // debug(1, "Applying soft volume dB: %f k: %f", gain_db, gain);

                  for (i = 0; i < inbuflength; ++i) {
                    fbuf_l[i] = loudness_process(&loudness_l, fbuf_l[i] * gain);
                    fbuf_r[i] = loudness_process(&loudness_r, fbuf_r[i] * gain);
                  }
                }

                // Interleave and convert back to int32_t
                for (i = 0; i < inbuflength; ++i) {
                  tbuf32[2 * i] = fbuf_l[i];
                  tbuf32[2 * i + 1] = fbuf_r[i];
                }
              }

#ifdef CONFIG_SOXR
              if ((current_delay < conn->dac_buffer_queue_minimum_length) ||
                  (config.packet_stuffing == ST_basic) ||
                  (config.soxr_delay_index == 0) || // not computed yet
                  ((config.packet_stuffing == ST_auto) &&
                   (config.soxr_delay_index >
                    config.soxr_delay_threshold)) // if the CPU is deemed too slow
              ) {
#endif
                play_samples =
                    stuff_buffer_basic_32((int32_t *)conn->tbuf, inbuflength, config.output_format,
                                          conn->outbuf, amount_to_stuff, conn->enable_dither, conn);
#ifdef CONFIG_SOXR
              } else { // soxr requested or auto requested with the index less or equal to the
                       // threshold
                play_samples = stuff_buffer_soxr_32((int32_t *)conn->tbuf, (int32_t *)conn->sbuf,
                                                    inbuflength, config.output_format, conn->outbuf,
                                                    amount_to_stuff, conn->enable_dither, conn);
              }
#endif

              /*
              {
                int co;
                int is_silent=1;
                short *p = outbuf;
                for (co=0;co<play_samples;co++) {
                  if (*p!=0)
                    is_silent=0;
                  p++;
                }
                if (is_silent)
                  debug(1,"Silence!");
              }
              */

              if (conn->outbuf == NULL)
                debug(1, "NULL outbuf to play -- skipping it.");
              else {
                if (play_samples == 0)
                  debug(1, "play_samples==0 skipping it (1).");
                else {
                  if (conn->software_mute_enabled) {
                    generate_zero_frames(conn->outbuf, play_samples, config.output_format,
                                         conn->enable_dither, conn->previous_random_number);
                  }
                  config.output->play(conn->outbuf, play_samples);
                }
              }

              // check for loss of sync
              // timestamp of zero means an inserted silent frame in place of a missing frame
              /*
              if ((config.no_sync == 0) && (inframe->timestamp != 0) &&
                  && (config.resyncthreshold > 0.0) &&
                  (abs_sync_error > config.resyncthreshold * config.output_rate)) {
                sync_error_out_of_bounds++;
                // debug(1,"Sync error out of bounds: Error: %lld; previous error: %lld; DAC: %lld;
                // timestamp: %llx, time now
                //
              %llx",sync_error,previous_sync_error,current_delay,inframe->timestamp,local_time_now);
                if (sync_error_out_of_bounds > 3) {
                  debug(1, "Lost sync with source for %d consecutive packets -- flushing and "
                           "resyncing. Error: %lld.",
                        sync_error_out_of_bounds, sync_error);
                  sync_error_out_of_bounds = 0;
                  player_flush(nt, conn);
                }
              } else {
                sync_error_out_of_bounds = 0;
              }
              */
            }
          } else {

            // if there is no delay procedure, then we should be sending the packet
            // to the output at the time determined by
            // the packet's time to play + requested latency + requested offset.
            /*
                                                            // This is just for checking during
               development

                        uint32_t should_be_frame_32;
                        local_time_to_frame(local_time_now, &should_be_frame_32, conn);
                        // int64_t should_be_frame = ((int64_t)should_be_frame_32) *
               conn->output_sample_ratio;

                        int32_t ilatency = (int32_t)((config.audio_backend_latency_offset -
               config.audio_backend_buffer_desired_length) * conn->input_rate) + conn->latency; if
               (ilatency < 0) debug(1,"incorrect latency %d.", ilatency);

                        int32_t idelay = (int32_t)(should_be_frame_32 - inframe->given_timestamp);

                        idelay = idelay - ilatency;

                        debug(2,"delay is %d input frames.", idelay);
            */

            // if this is the first frame, see if it's close to when it's supposed to be
            // release, which will be its time plus latency and any offset_time
            if (at_least_one_frame_seen_this_session == 0) {
              at_least_one_frame_seen_this_session = 1;
            }

            play_samples =
                stuff_buffer_basic_32((int32_t *)conn->tbuf, inbuflength, config.output_format,
                                      conn->outbuf, 0, conn->enable_dither, conn);
            if (conn->outbuf == NULL)
              debug(1, "NULL outbuf to play -- skipping it.");
            else {
              if (conn->software_mute_enabled) {
                generate_zero_frames(conn->outbuf, play_samples, config.output_format,
                                     conn->enable_dither, conn->previous_random_number);
              }
              config.output->play(conn->outbuf, play_samples); // remove the (short*)!
            }
          }

          // mark the frame as finished
          inframe->given_timestamp = 0;
          inframe->sequence_number = 0;
          inframe->resend_time = 0;
          inframe->initialisation_time = 0;

          // update the watchdog
          if ((config.dont_check_timeout == 0) && (config.timeout != 0)) {
            uint64_t time_now = get_absolute_time_in_ns();
            debug_mutex_lock(&conn->watchdog_mutex, 1000, 0);
            conn->watchdog_bark_time = time_now;
            debug_mutex_unlock(&conn->watchdog_mutex, 0);
          }

          // debug(1,"Sync error %lld frames. Amount to stuff %d." ,sync_error,amount_to_stuff);

          // new stats calculation. We want a running average of sync error, drift, adjustment,
          // number of additions+subtractions

          // this is a misleading hack -- the statistics should include some data on the number of
          // valid samples and the number of times sync wasn't checked due to non availability of a
          // delay figure.
          // for the present, stats are only updated when sync has been checked
          if (config.output->delay != NULL) {
            if (number_of_statistics == trend_interval) {
              // here we remove the oldest statistical data and take it from the summaries as well
              tsum_of_sync_errors -= conn->statistics[oldest_statistic].sync_error;
              tsum_of_drifts -= conn->statistics[oldest_statistic].drift;
              if (conn->statistics[oldest_statistic].correction > 0)
                tsum_of_insertions_and_deletions -= conn->statistics[oldest_statistic].correction;
              else
                tsum_of_insertions_and_deletions += conn->statistics[oldest_statistic].correction;
              tsum_of_corrections -= conn->statistics[oldest_statistic].correction;
              oldest_statistic = (oldest_statistic + 1) % trend_interval;
              number_of_statistics--;
            }

            conn->statistics[newest_statistic].sync_error = sync_error;
            conn->statistics[newest_statistic].correction = conn->amountStuffed;

            if (number_of_statistics == 0)
              conn->statistics[newest_statistic].drift = 0;
            else
              conn->statistics[newest_statistic].drift =
                  sync_error - previous_sync_error - previous_correction;

            previous_sync_error = sync_error;
            previous_correction = conn->amountStuffed;

            tsum_of_sync_errors += sync_error;
            tsum_of_drifts += conn->statistics[newest_statistic].drift;
            if (conn->amountStuffed > 0) {
              tsum_of_insertions_and_deletions += conn->amountStuffed;
            } else {
              tsum_of_insertions_and_deletions -= conn->amountStuffed;
            }
            tsum_of_corrections += conn->amountStuffed;
            conn->session_corrections += conn->amountStuffed;

            newest_statistic = (newest_statistic + 1) % trend_interval;
            number_of_statistics++;
          }
        }
        if (play_number % print_interval == 0) {

          // here, calculate the input and output frame rates, where possible, even if statistics
          // have not been requested
          // this is to calculate them in case they are needed by the D-Bus interface or elsewhere.

          if (conn->input_frame_rate_starting_point_is_valid) {
            uint64_t elapsed_reception_time, frames_received;
            elapsed_reception_time =
                conn->frames_inward_measurement_time - conn->frames_inward_measurement_start_time;
            frames_received = conn->frames_inward_frames_received_at_measurement_time -
                              conn->frames_inward_frames_received_at_measurement_start_time;
            conn->input_frame_rate =
                (1.0E9 * frames_received) /
                elapsed_reception_time; // an IEEE double calculation with two 64-bit integers
          } else {
            conn->input_frame_rate = 0.0;
          }

          if ((config.output->delay) && (config.no_sync == 0) && (config.output->rate_info)) {
            uint64_t elapsed_play_time, frames_played;
            if (config.output->rate_info(&elapsed_play_time, &frames_played) == 0)
              conn->frame_rate_status = 1;
            else
              conn->frame_rate_status = 0;
            if (conn->frame_rate_status) {
              conn->frame_rate =
                  (1.0E9 * frames_played) /
                  elapsed_play_time; // an IEEE double calculation with two 64-bit integers
            } else {
              conn->frame_rate = 0.0;
            }
          }

          // we can now calculate running averages for sync error (frames), corrections (ppm),
          // insertions plus deletions (ppm), drift (ppm)
          double moving_average_sync_error = (1.0 * tsum_of_sync_errors) / number_of_statistics;
          double moving_average_correction = (1.0 * tsum_of_corrections) / number_of_statistics;
          double moving_average_insertions_plus_deletions =
              (1.0 * tsum_of_insertions_and_deletions) / number_of_statistics;
          // double moving_average_drift = (1.0 * tsum_of_drifts) / number_of_statistics;
          // if ((play_number/print_interval)%20==0)
          if (config.statistics_requested) {
            if (at_least_one_frame_seen) {

              if ((config.output->delay)) {
                if (config.no_sync == 0) {
                  inform(
                      "%*.2f,"        /* Sync error in milliseconds */
                      "%*.1f,"        /* net correction in ppm */
                      "%*.1f,"        /* corrections in ppm */
                      "%*d,"          /* total packets */
                      "%*" PRIu64 "," /* missing packets */
                      "%*" PRIu64 "," /* late packets */
                      "%*" PRIu64 "," /* too late packets */
                      "%*" PRIu64 "," /* resend requests */
                      "%*" PRIu64 "," /* min DAC queue size */
                      "%*" PRId32 "," /* min buffer occupancy */
                      "%*" PRId32 "," /* max buffer occupancy */
                      "%*.2f,"        /* source nominal frame rate */
                      "%*.2f,"        /* source actual (average) frame rate */
                      "%*.2f,"        /* output frame rate */
                      "%*.2f,"        /* source clock drift */
                      "%*d,"          /* source clock drift sample count */
                      "%*.2f",        /* rough calculated correction in ppm */
                      10, 1000 * moving_average_sync_error / config.output_rate, 10,
                      moving_average_correction * 1000000 / (352 * conn->output_sample_ratio), 10,
                      moving_average_insertions_plus_deletions * 1000000 /
                          (352 * conn->output_sample_ratio),
                      12, play_number, 7, conn->missing_packets, 7, conn->late_packets, 7,
                      conn->too_late_packets, 7, conn->resend_requests, 7, minimum_dac_queue_size,
                      5, minimum_buffer_occupancy, 5, maximum_buffer_occupancy, 11,
                      conn->remote_frame_rate, 11, conn->input_frame_rate, 11, conn->frame_rate, 10,
                      (conn->local_to_remote_time_gradient - 1.0) * 1000000, 6,
                      conn->local_to_remote_time_gradient_sample_count, 10,
                      (conn->frame_rate > 0.0)
                          ? ((conn->frame_rate - conn->remote_frame_rate *
                                                     conn->output_sample_ratio *
                                                     conn->local_to_remote_time_gradient) *
                             1000000) /
                                conn->frame_rate
                          : 0.0);
                } else {
                  inform("%*.2f,"        /* Sync error in milliseconds */
                         "%*d,"          /* total packets */
                         "%*" PRIu64 "," /* missing packets */
                         "%*" PRIu64 "," /* late packets */
                         "%*" PRIu64 "," /* too late packets */
                         "%*" PRIu64 "," /* resend requests */
                         "%*" PRIu64 "," /* min DAC queue size */
                         "%*" PRId32 "," /* min buffer occupancy */
                         "%*" PRId32 "," /* max buffer occupancy */
                         "%*.2f,"        /* source nominal frame rate */
                         "%*.2f,"        /* source actual (average) frame rate */
                         "%*.2f,"        /* source clock drift */
                         "%*d",          /* source clock drift sample count */
                         10, 1000 * moving_average_sync_error / config.output_rate, 12, play_number,
                         7, conn->missing_packets, 7, conn->late_packets, 7, conn->too_late_packets,
                         7, conn->resend_requests, 7, minimum_dac_queue_size, 5,
                         minimum_buffer_occupancy, 5, maximum_buffer_occupancy, 11,
                         conn->remote_frame_rate, 11, conn->input_frame_rate, 10,
                         (conn->local_to_remote_time_gradient - 1.0) * 1000000, 6,
                         conn->local_to_remote_time_gradient_sample_count);
                }
              } else {
                inform("%*d,"          /* total packets */
                       "%*" PRIu64 "," /* missing packets */
                       "%*" PRIu64 "," /* late packets */
                       "%*" PRIu64 "," /* too late packets */
                       "%*" PRIu64 "," /* resend requests */
                       "%*" PRId32 "," /* min buffer occupancy */
                       "%*" PRId32 "," /* max buffer occupancy */
                       "%*.2f,"        /* source nominal frame rate */
                       "%*.2f,"        /* source actual (average) frame rate */
                       "%*.2f,"        /* source clock drift */
                       "%*d",          /* source clock drift sample count */
                       12, play_number, 7, conn->missing_packets, 7, conn->late_packets, 7,
                       conn->too_late_packets, 7, conn->resend_requests, 5,
                       minimum_buffer_occupancy, 5, maximum_buffer_occupancy, 11,
                       conn->remote_frame_rate, 11, conn->input_frame_rate, 10,
                       (conn->local_to_remote_time_gradient - 1.0) * 1000000, 6,
                       conn->local_to_remote_time_gradient_sample_count);
              }
            } else {
              inform("No frames received in the last sampling interval.");
            }
          }
          minimum_dac_queue_size = UINT64_MAX;  // hack reset
          maximum_buffer_occupancy = INT32_MIN; // can't be less than this
          minimum_buffer_occupancy = INT32_MAX; // can't be more than this
          at_least_one_frame_seen = 0;
        }
      }
    }
  }

  debug(1, "This should never be called.");
  pthread_cleanup_pop(1); // pop the cleanup handler
                          //  debug(1, "This should never be called either.");
                          //  pthread_cleanup_pop(1); // pop the initial cleanup handler
  pthread_exit(NULL);
}

void player_volume_without_notification(double airplay_volume, rtsp_conn_info *conn) {
  debug_mutex_lock(&conn->volume_control_mutex, 5000, 1);
  // first, see if we are hw only, sw only, both with hw attenuation on the top or both with sw
  // attenuation on top

  enum volume_mode_type { vol_sw_only, vol_hw_only, vol_both } volume_mode;

  // take account of whether there is a hardware mixer, if a max volume has been specified and if a
  // range has been specified
  // the range might imply that both hw and software mixers are needed, so calculate this

  int32_t hw_max_db = 0, hw_min_db = 0; // zeroed to quieten an incorrect uninitialised warning
  int32_t sw_max_db = 0, sw_min_db = -9630;

  if (config.output->parameters) {
    volume_mode = vol_hw_only;
    audio_parameters audio_information;
    config.output->parameters(&audio_information);
    hw_max_db = audio_information.maximum_volume_dB;
    hw_min_db = audio_information.minimum_volume_dB;
    if (config.volume_max_db_set) {
      if (((config.volume_max_db * 100) <= hw_max_db) &&
          ((config.volume_max_db * 100) >= hw_min_db))
        hw_max_db = (int32_t)config.volume_max_db * 100;
      else if (config.volume_range_db) {
        hw_max_db = hw_min_db;
        sw_max_db = (config.volume_max_db * 100) - hw_min_db;
      } else {
        warn("The maximum output level is outside the range of the hardware mixer -- ignored");
      }
    }

    // here, we have set limits on the hw_max_db and the sw_max_db
    // but we haven't actually decided whether we need both hw and software attenuation
    // only if a range is specified could we need both
    if (config.volume_range_db) {
      // see if the range requested exceeds the hardware range available
      int32_t desired_range_db = (int32_t)trunc(config.volume_range_db * 100);
      if ((desired_range_db) > (hw_max_db - hw_min_db)) {
        volume_mode = vol_both;
        int32_t desired_sw_range = desired_range_db - (hw_max_db - hw_min_db);
        if ((sw_max_db - desired_sw_range) < sw_min_db)
          warn("The range requested is too large to accommodate -- ignored.");
        else
          sw_min_db = (sw_max_db - desired_sw_range);
      }
    }
  } else {
    // debug(1,"has no hardware mixer");
    volume_mode = vol_sw_only;
    if (config.volume_max_db_set) {
      if (((config.volume_max_db * 100) <= sw_max_db) &&
          ((config.volume_max_db * 100) >= sw_min_db))
        sw_max_db = (int32_t)config.volume_max_db * 100;
    }
    if (config.volume_range_db) {
      // see if the range requested exceeds the software range available
      int32_t desired_range_db = (int32_t)trunc(config.volume_range_db * 100);
      if ((desired_range_db) > (sw_max_db - sw_min_db))
        warn("The range requested is too large to accommodate -- ignored.");
      else
        sw_min_db = (sw_max_db - desired_range_db);
    }
  }

  // here, we know whether it's hw volume control only, sw only or both, and we have the hw and sw
  // limits.
  // if it's both, we haven't decided whether hw or sw should be on top
  // we have to consider the settings ignore_volume_control and mute.

  if (config.ignore_volume_control == 0) {
    if (airplay_volume == -144.0) {

      if ((config.output->mute) && (config.output->mute(1) == 0))
        debug(2,
              "player_volume_without_notification: volume mode is %d, airplay_volume is %f, "
              "hardware mute is enabled.",
              volume_mode, airplay_volume);
      else {
        conn->software_mute_enabled = 1;
        debug(2,
              "player_volume_without_notification: volume mode is %d, airplay_volume is %f, "
              "software mute is enabled.",
              volume_mode, airplay_volume);
      }

    } else {
      int32_t max_db = 0, min_db = 0;
      switch (volume_mode) {
      case vol_hw_only:
        max_db = hw_max_db;
        min_db = hw_min_db;
        break;
      case vol_sw_only:
        max_db = sw_max_db;
        min_db = sw_min_db;
        break;
      case vol_both:
        // debug(1, "dB range passed is hw: %d, sw: %d, total: %d", hw_max_db - hw_min_db,
        //      sw_max_db - sw_min_db, (hw_max_db - hw_min_db) + (sw_max_db - sw_min_db));
        max_db =
            (hw_max_db - hw_min_db) + (sw_max_db - sw_min_db); // this should be the range requested
        min_db = 0;
        break;
      default:
        debug(1, "player_volume_without_notification: error: not in a volume mode");
        break;
      }
      double scaled_attenuation = 0.0;
      if (config.volume_control_profile == VCP_standard)
        scaled_attenuation = vol2attn(airplay_volume, max_db, min_db); // no cancellation points
      else if (config.volume_control_profile == VCP_flat)
        scaled_attenuation =
            flat_vol2attn(airplay_volume, max_db, min_db); // no cancellation points
      else
        debug(1, "player_volume_without_notification: unrecognised volume control profile");

      // so here we have the scaled attenuation. If it's for hw or sw only, it's straightforward.
      double hardware_attenuation = 0.0;
      double software_attenuation = 0.0;

      switch (volume_mode) {
      case vol_hw_only:
        hardware_attenuation = scaled_attenuation;
        break;
      case vol_sw_only:
        software_attenuation = scaled_attenuation;
        break;
      case vol_both:
        // here, we now the attenuation required, so we have to apportion it to the sw and hw mixers
        // if we give the hw priority, that means when lowering the volume, set the hw volume to its
        // lowest
        // before using the sw attenuation.
        // similarly, if we give the sw priority, that means when lowering the volume, set the sw
        // volume to its lowest
        // before using the hw attenuation.
        // one imagines that hw priority is likely to be much better
        // if (config.volume_range_hw_priority) {
        if (config.volume_range_hw_priority != 0) {
          // hw priority
          if ((sw_max_db - sw_min_db) > scaled_attenuation) {
            software_attenuation = sw_min_db + scaled_attenuation;
            hardware_attenuation = hw_min_db;
          } else {
            software_attenuation = sw_max_db;
            hardware_attenuation = hw_min_db + scaled_attenuation - (sw_max_db - sw_min_db);
          }
        } else {
          // sw priority
          if ((hw_max_db - hw_min_db) > scaled_attenuation) {
            hardware_attenuation = hw_min_db + scaled_attenuation;
            software_attenuation = sw_min_db;
          } else {
            hardware_attenuation = hw_max_db;
            software_attenuation = sw_min_db + scaled_attenuation - (hw_max_db - hw_min_db);
          }
        }
        break;
      default:
        debug(1, "player_volume_without_notification: error: not in a volume mode");
        break;
      }

      if (((volume_mode == vol_hw_only) || (volume_mode == vol_both)) && (config.output->volume)) {
        config.output->volume(hardware_attenuation); // otherwise set the output to the lowest value
        // debug(1,"Hardware attenuation set to %f for airplay volume of
        // %f.",hardware_attenuation,airplay_volume);
        if (volume_mode == vol_hw_only)
          conn->fix_volume = 0x10000;
      }

      if ((volume_mode == vol_sw_only) || (volume_mode == vol_both)) {
        double temp_fix_volume = 65536.0 * pow(10, software_attenuation / 2000);
        // debug(1,"Software attenuation set to %f, i.e %f out of 65,536, for airplay volume of
        // %f",software_attenuation,temp_fix_volume,airplay_volume);

        conn->fix_volume = temp_fix_volume;

        // if (config.loudness)
        loudness_set_volume(software_attenuation / 100);
      }

      if (config.logOutputLevel) {
        inform("Output Level set to: %.2f dB.", scaled_attenuation / 100.0);
      }

#ifdef CONFIG_METADATA
      // here, send the 'pvol' metadata message when the airplay volume information
      // is being used by shairport sync to control the output volume
      char dv[128];
      memset(dv, 0, 128);
      if (volume_mode == vol_both) {
        // normalise the maximum output to the hardware device's max output
        snprintf(dv, 127, "%.2f,%.2f,%.2f,%.2f", airplay_volume,
                 (scaled_attenuation - max_db + hw_max_db) / 100.0,
                 (min_db - max_db + hw_max_db) / 100.0, (max_db - max_db + hw_max_db) / 100.0);
      } else {
        snprintf(dv, 127, "%.2f,%.2f,%.2f,%.2f", airplay_volume, scaled_attenuation / 100.0,
                 min_db / 100.0, max_db / 100.0);
      }
      send_ssnc_metadata('pvol', dv, strlen(dv), 1);
#endif

      if (config.output->mute)
        config.output->mute(0);
      conn->software_mute_enabled = 0;

      debug(2,
            "player_volume_without_notification: volume mode is %d, airplay volume is %f, "
            "software_attenuation: %f, hardware_attenuation: %f, muting "
            "is disabled.",
            volume_mode, airplay_volume, software_attenuation, hardware_attenuation);
    }
  }

#ifdef CONFIG_METADATA
  else {
    // here, send the 'pvol' metadata message when the airplay volume information
    // is being used by shairport sync to control the output volume
    char dv[128];
    memset(dv, 0, 128);
    snprintf(dv, 127, "%.2f,%.2f,%.2f,%.2f", airplay_volume, 0.0, 0.0, 0.0);
    send_ssnc_metadata('pvol', dv, strlen(dv), 1);
  }
#endif

  // here, store the volume for possible use in the future
  config.airplay_volume = airplay_volume;
  debug_mutex_unlock(&conn->volume_control_mutex, 3);
}

void player_volume(double airplay_volume, rtsp_conn_info *conn) {
  command_set_volume(airplay_volume);
  player_volume_without_notification(airplay_volume, conn);
}

void do_flush(uint32_t timestamp, rtsp_conn_info *conn) {

  debug(2, "do_flush: flush to %u.", timestamp);
  debug_mutex_lock(&conn->flush_mutex, 1000, 1);
  conn->flush_requested = 1;
  conn->flush_rtp_timestamp = timestamp; // flush all packets up to, but not including, this one.
  reset_input_flow_metrics(conn);
  debug_mutex_unlock(&conn->flush_mutex, 3);
}

void player_flush(uint32_t timestamp, rtsp_conn_info *conn) {
  debug(3, "player_flush");
  do_flush(timestamp, conn);
#ifdef CONFIG_METADATA
  debug(2, "pfls");
  char numbuf[32];
  snprintf(numbuf, sizeof(numbuf), "%u", timestamp);
  send_ssnc_metadata('pfls', numbuf, strlen(numbuf), 1); // contains cancellation points
#endif
}

int player_play(rtsp_conn_info *conn) {
  // need to use conn in place of stream below. Need to put the stream as a parameter to he
  if (conn->player_thread != NULL)
    die("Trying to create a second player thread for this RTSP session");
  if (config.buffer_start_fill > BUFFER_FRAMES)
    die("specified buffer starting fill %d > buffer size %d", config.buffer_start_fill,
        BUFFER_FRAMES);
  activity_monitor_signify_activity(
      1); // active, and should be before play's command hook, command_start()
  command_start();
  // call on the output device to prepare itself
  if ((config.output) && (config.output->prepare))
    config.output->prepare();

  pthread_t *pt = malloc(sizeof(pthread_t));
  if (pt == NULL)
    die("Couldn't allocate space for pthread_t");
  conn->player_thread = pt;
  int rc = pthread_create(pt, NULL, player_thread_func, (void *)conn);
  if (rc)
    debug(1, "Error creating player_thread: %s", strerror(errno));

#ifdef CONFIG_METADATA
  debug(2, "pbeg");
  send_ssnc_metadata('pbeg', NULL, 0, 1); // contains cancellation points
#endif
  return 0;
}

int player_stop(rtsp_conn_info *conn) {
  // note -- this may be called from another connection thread.
  // int dl = debuglev;
  // debuglev = 3;
  debug(3, "player_stop");
  if (conn->player_thread) {
    debug(3, "player_thread cancel...");
    pthread_cancel(*conn->player_thread);
    debug(3, "player_thread join...");
    if (pthread_join(*conn->player_thread, NULL) == -1) {
      char errorstring[1024];
      strerror_r(errno, (char *)errorstring, sizeof(errorstring));
      debug(1, "Connection %d: error %d joining player thread: \"%s\".", conn->connection_number,
            errno, (char *)errorstring);
    } else {
      debug(3, "player_thread joined.");
    }
    free(conn->player_thread);
    conn->player_thread = NULL;
#ifdef CONFIG_METADATA
    debug(2, "pend");
    send_ssnc_metadata('pend', NULL, 0, 1); // contains cancellation points
#endif
    // debuglev = dl;
    command_stop();
    activity_monitor_signify_activity(0); // inactive, and should be after command_stop()
    return 0;
  } else {
    debug(3, "Connection %d: player thread already deleted.", conn->connection_number);
    // debuglev = dl;
    return -1;
  }
}
