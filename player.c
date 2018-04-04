/*
 * Slave-clocked ALAC stream player. This file is part of Shairport.
 * Copyright (c) James Laird 2011, 2013
 * All rights reserved.
 *
 * Modifications for audio synchronisation
 * and related work, copyright (c) Mike Brady 2014 -- 2018
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
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syslog.h>
#include <sys/types.h>
#include <unistd.h>

#include "config.h"

#ifdef HAVE_LIBMBEDTLS
#include <mbedtls/aes.h>
#include <mbedtls/havege.h>
#endif

#ifdef HAVE_LIBPOLARSSL
#include <polarssl/aes.h>
#include <polarssl/havege.h>
#endif

#ifdef HAVE_LIBSSL
#include <openssl/aes.h>
#endif

#ifdef HAVE_LIBSOXR
#include <soxr.h>
#endif

#ifdef CONFIG_CONVOLUTION
#include <FFTConvolver/convolver.h>
#endif

#ifdef HAVE_METADATA_HUB
#include "metadata_hub.h"
#endif

#ifdef HAVE_DACP_CLIENT
#include "dacp.h"
#include <glib.h>
#endif

#ifdef HAVE_DBUS
#include "dbus-interface.h"
#include "dbus-service.h"
#endif

#include "common.h"
#include "player.h"
#include "rtp.h"
#include "rtsp.h"

#include "alac.h"

#ifdef HAVE_APPLE_ALAC
#include "apple_alac.h"
#endif

#include "loudness.h"

// default buffer size
// needs to be a power of 2 because of the way BUFIDX(seqno) works
//#define BUFFER_FRAMES 512
#define MAX_PACKET 2048

// DAC buffer occupancy stuff
#define DAC_BUFFER_QUEUE_MINIMUM_LENGTH 600

// static abuf_t audio_buffer[BUFFER_FRAMES];
#define BUFIDX(seqno) ((seq_t)(seqno) % BUFFER_FRAMES)

// make timestamps and seqnos definitely monotonic

// add an epoch to the timestamp. The monotonic timestamp guaranteed to start between 2^32 and 2^33
// frames and continue up to 2^63-1 frames
// if should never get into the negative range
// which is about 2*10^8 * 1,000 seconds at 384,000 frames per second -- about 2 trillion seconds or
// over 50,000 years.
// also, it won't reach zero until then, if ever, so we can safely say that a null monotonic
// timestamp can mean something special
int64_t monotonic_timestamp(uint32_t timestamp, rtsp_conn_info *conn) {
  int64_t previous_value;
  int64_t return_value;
  if (conn->timestamp_epoch == 0) {
    if (timestamp > conn->maximum_timestamp_interval)
      conn->timestamp_epoch = 1;
    else
      conn->timestamp_epoch = 2;
    previous_value = conn->timestamp_epoch;
    previous_value <<= 32;
    previous_value += timestamp;
  } else {
    previous_value = conn->timestamp_epoch;
    previous_value <<= 32;
    previous_value += conn->last_timestamp;
    if (timestamp < conn->last_timestamp) {
      // the incoming timestamp is less than the last one.
      // if the difference is more than a minute, assume it's really from the next epoch
      if ((conn->last_timestamp - timestamp) > conn->maximum_timestamp_interval)
        conn->timestamp_epoch++;
    } else {
      // the incoming timestamp is greater than the last one.
      // if the difference is more than a minute, assume it's really from the previous epoch
      if ((timestamp - conn->last_timestamp) > conn->maximum_timestamp_interval)
        conn->timestamp_epoch--;
    }
  }
  return_value = conn->timestamp_epoch;
  return_value <<= 32;
  return_value += timestamp;
  if (previous_value > return_value) {
    if ((previous_value - return_value) > conn->maximum_timestamp_interval)
      debug(2, "interval between successive rtptimes greater than allowed!");
  } else {
    if ((return_value - previous_value) > conn->maximum_timestamp_interval)
      debug(2, "interval between successive rtptimes greater than allowed!");
  }
  if (return_value < 0)
    debug(1, "monotonic rtptime is negative!");
  conn->last_timestamp = timestamp;
  return return_value;
}

static void ab_resync(rtsp_conn_info *conn) {
  int i;
  for (i = 0; i < BUFFER_FRAMES; i++) {
    conn->audio_buffer[i].ready = 0;
    conn->audio_buffer[i].sequence_number = 0;
  }
  conn->ab_synced = 0;
  conn->last_seqno_read = -1;
  conn->ab_buffering = 1;
}

// the sequence number is a 16-bit unsigned number which wraps pretty often
// to work out if one seqno is 'after' another therefore depends whether wrap has occurred
// this function works out the actual ordinate of the seqno, i.e. the distance up from
// the zeroth element, at ab_read, taking due account of wrap.

static inline seq_t SUCCESSOR(seq_t x) {
  uint32_t p = x & 0xffff;
  p += 1;
  p = p & 0xffff;
  return p;
}

static inline seq_t PREDECESSOR(seq_t x) {
  uint32_t p = (x & 0xffff) + 0x10000;
  p -= 1;
  p = p & 0xffff;
  return p;
}

// anything with ORDINATE in it must be proctected by the ab_mutex
static inline int32_t ORDINATE(seq_t x, seq_t base) {
  int32_t p = x;    // int32_t from seq_t, i.e. uint16_t, so okay
  int32_t q = base; // int32_t from seq_t, i.e. uint16_t, so okay
  int32_t t = (p + 0x10000 - q) & 0xffff;
  // we definitely will get a positive number in t at this point, but it might be a
  // positive alias of a negative number, i.e. x might actually be "before" ab_read
  // So, if the result is greater than 32767, we will assume its an
  // alias and subtract 65536 from it
  if (t >= 32767) {
    // debug(1,"OOB: %u, ab_r: %u, ab_w: %u",x,ab_read,ab_write);
    t -= 65536;
  }
  return t;
}

// wrapped number between two seq_t.
int32_t seq_diff(seq_t a, seq_t b, seq_t base) {
  int32_t diff = ORDINATE(b, base) - ORDINATE(a, base);
  return diff;
}

// the sequence numbers will wrap pretty often.
// this returns true if the second arg is after the first
static inline int seq_order(seq_t a, seq_t b, seq_t base) {
  int32_t d = ORDINATE(b, base) - ORDINATE(a, base);
  return d > 0;
}

static inline seq_t seq_sum(seq_t a, seq_t b) {
  //  uint32_t p = a & 0xffff;
  //  uint32_t q = b & 0x0ffff;
  uint32_t r = (a + b) & 0xffff;
  return r;
}

// now for 32-bit wrapping in timestamps

// this returns true if the second arg is strictly after the first
// on the assumption that the gap between them is never greater than (2^31)-1
// Represent a and b in 64 bits
static inline int seq32_order(uint32_t a, uint32_t b) {
  if (a == b)
    return 0;
  int64_t A = a & 0xffffffff;
  int64_t B = b & 0xffffffff;
  int64_t C = B - A;
  // if bit 31 is set, it means either b is before (i.e. less than) a or
  // b is (2^31)-1 ahead of a.

  // If we assume the gap between b and a should never reach 2 billion, then
  // bit 31 == 0 means b is strictly after a
  return (C & 0x80000000) == 0;
}

static int alac_decode(short *dest, int *destlen, uint8_t *buf, int len, rtsp_conn_info *conn) {
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
  int toutsize = outsize;

  if (conn->stream.encrypted) {
    unsigned char iv[16];
    int aeslen = len & ~0xf;
    memcpy(iv, conn->stream.aesiv, sizeof(iv));
#ifdef HAVE_LIBMBEDTLS
    mbedtls_aes_crypt_cbc(&conn->dctx, MBEDTLS_AES_DECRYPT, aeslen, iv, buf, packet);
#endif
#ifdef HAVE_LIBPOLARSSL
    aes_crypt_cbc(&conn->dctx, AES_DECRYPT, aeslen, iv, buf, packet);
#endif
#ifdef HAVE_LIBSSL
    AES_cbc_encrypt(buf, packet, aeslen, &conn->aes, iv, AES_DECRYPT);
#endif
    memcpy(packet + aeslen, buf + aeslen, len - aeslen);
#ifdef HAVE_APPLE_ALAC
    if (config.use_apple_decoder) {
      if (conn->decoder_in_use != 1 << decoder_apple_alac) {
        debug(2, "Apple ALAC Decoder used on encrypted audio.");
        conn->decoder_in_use = 1 << decoder_apple_alac;
      }
      apple_alac_decode_frame(packet, len, (unsigned char *)dest, &outsize);
      outsize = outsize * 4; // bring the size to bytes
    } else
#endif
    {
      if (conn->decoder_in_use != 1 << decoder_hammerton) {
        debug(2, "Hammerton Decoder used on encrypted audio.");
        conn->decoder_in_use = 1 << decoder_hammerton;
      }
      alac_decode_frame(conn->decoder_info, packet, (unsigned char *)dest, &outsize);
    }
  } else {
// not encrypted
#ifdef HAVE_APPLE_ALAC
    if (config.use_apple_decoder) {
      if (conn->decoder_in_use != 1 << decoder_apple_alac) {
        debug(2, "Apple ALAC Decoder used on unencrypted audio.");
        conn->decoder_in_use = 1 << decoder_apple_alac;
      }
      apple_alac_decode_frame(buf, len, (unsigned char *)dest, &outsize);
      outsize = outsize * 4; // bring the size to bytes
    } else
#endif
    {
      if (conn->decoder_in_use != 1 << decoder_hammerton) {
        debug(2, "Hammerton Decoder used on unencrypted audio.");
        conn->decoder_in_use = 1 << decoder_hammerton;
      }
      alac_decode_frame(conn->decoder_info, buf, dest, &outsize);
    }
  }

  if (outsize > toutsize) {
    debug(2, "Output from alac_decode larger (%d bytes, not frames) than expected (%d bytes) -- "
             "truncated, but buffer overflow possible! Encrypted = %d.",
          outsize, toutsize, conn->stream.encrypted);
    reply = -1; // output packet is the wrong size
  }

  *destlen = outsize / conn->input_bytes_per_frame;
  if ((outsize % conn->input_bytes_per_frame) != 0)
    debug(1, "Number of audio frames (%d) does not correspond exactly to the number of bytes (%d) "
             "and the audio frame size (%d).",
          *destlen, outsize, conn->input_bytes_per_frame);
  return reply;
}

static int init_decoder(int32_t fmtp[12], rtsp_conn_info *conn) {

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

  conn->max_frames_per_packet = fmtp[1]; // number of audio frames per packet.

  conn->input_rate = fmtp[11];
  conn->input_num_channels = fmtp[7];
  conn->input_bit_depth = fmtp[3];

  conn->input_bytes_per_frame = conn->input_num_channels * ((conn->input_bit_depth + 7) / 8);

  alac = alac_create(conn->input_bit_depth, conn->input_num_channels);
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
  alac_allocate_buffers(alac);

#ifdef HAVE_APPLE_ALAC
  apple_alac_init(fmtp);
#endif

  return 0;
}

static void terminate_decoders(rtsp_conn_info *conn) {
  alac_free(conn->decoder_info);
#ifdef HAVE_APPLE_ALAC
  apple_alac_terminate();
#endif
}

static void init_buffer(rtsp_conn_info *conn) {
  int i;
  for (i = 0; i < BUFFER_FRAMES; i++)
    conn->audio_buffer[i].data = malloc(
        conn->input_bytes_per_frame * (conn->max_frames_per_packet + conn->max_frame_size_change));
  ab_resync(conn);
}

static void free_audio_buffers(rtsp_conn_info *conn) {
  int i;
  for (i = 0; i < BUFFER_FRAMES; i++)
    free(conn->audio_buffer[i].data);
}

void player_put_packet(seq_t seqno, int64_t timestamp, uint8_t *data, int len,
                       rtsp_conn_info *conn) {

  // all timestamps are done at the output rate

  int64_t ltimestamp = timestamp * conn->output_sample_ratio;

  // ignore a request to flush that has been made before the first packet...
  if (conn->packet_count == 0) {
    pthread_mutex_lock(&conn->flush_mutex);
    conn->flush_requested = 0;
    conn->flush_rtp_timestamp = 0;
    pthread_mutex_unlock(&conn->flush_mutex);
  }

  pthread_mutex_lock(&conn->ab_mutex);
  conn->packet_count++;
  conn->time_of_last_audio_packet = get_absolute_time_in_fp();
  if (conn->connection_state_to_output) { // if we are supposed to be processing these packets

    //    if (flush_rtp_timestamp != 0)
    //    	debug(1,"Flush_rtp_timestamp is %u",flush_rtp_timestamp);

    if ((conn->flush_rtp_timestamp != 0) && (ltimestamp <= conn->flush_rtp_timestamp)) {
      debug(3,
            "Dropping flushed packet in player_put_packet, seqno %u, timestamp %lld, flushing to "
            "timestamp: %lld.",
            seqno, ltimestamp, conn->flush_rtp_timestamp);
    } else {
      if ((conn->flush_rtp_timestamp != 0x0) &&
          (ltimestamp > conn->flush_rtp_timestamp)) // if we have gone past the flush boundary time
        conn->flush_rtp_timestamp = 0x0;

      abuf_t *abuf = 0;

      if (!conn->ab_synced) {
        debug(3, "syncing to seqno %u.", seqno);
        conn->ab_write = seqno;
        conn->ab_read = seqno;
        conn->ab_synced = 1;
      }
      if (conn->ab_write == seqno) { // expected packet
        abuf = conn->audio_buffer + BUFIDX(seqno);
        conn->ab_write = SUCCESSOR(seqno);
      } else if (seq_order(conn->ab_write, seqno, conn->ab_read)) { // newer than expected
        // if (ORDINATE(seqno)>(BUFFER_FRAMES*7)/8)
        // debug(1,"An interval of %u frames has opened, with ab_read: %u, ab_write: %u and seqno:
        // %u.",seq_diff(ab_read,seqno),ab_read,ab_write,seqno);
        int32_t gap = seq_diff(conn->ab_write, seqno, conn->ab_read);
        if (gap <= 0)
          debug(1, "Unexpected gap size: %d.", gap);
        int i;
        for (i = 0; i < gap; i++) {
          abuf = conn->audio_buffer + BUFIDX(seq_sum(conn->ab_write, i));
          abuf->ready = 0; // to be sure, to be sure
          abuf->timestamp = 0;
          abuf->sequence_number = 0;
        }
        // debug(1,"N %d s %u.",seq_diff(ab_write,PREDECESSOR(seqno))+1,ab_write);
        abuf = conn->audio_buffer + BUFIDX(seqno);
        //        rtp_request_resend(ab_write, gap);
        //        resend_requests++;
        conn->ab_write = SUCCESSOR(seqno);
      } else if (seq_order(conn->ab_read, seqno, conn->ab_read)) { // late but not yet played
        conn->late_packets++;
        abuf = conn->audio_buffer + BUFIDX(seqno);
        /*
        if (abuf->ready)
                debug(1,"Late apparently duplicate packet received that is %d packets
        late.",seq_diff(seqno, conn->ab_write, conn->ab_read));
        else
                debug(1,"Late packet received that is %d packets late.",seq_diff(seqno,
        conn->ab_write, conn->ab_read));
              */
      } else { // too late.

        // debug(1,"Too late packet received that is %d packets late.",seq_diff(seqno,
        // conn->ab_write, conn->ab_read));
        conn->too_late_packets++;
      }
      // pthread_mutex_unlock(&ab_mutex);

      if (abuf) {
        int datalen = conn->max_frames_per_packet;
        if (alac_decode(abuf->data, &datalen, data, len, conn) == 0) {
          abuf->ready = 1;
          abuf->length = datalen;
          abuf->timestamp = ltimestamp;
          abuf->sequence_number = seqno;
        } else {
          debug(1, "Bad audio packet detected and discarded.");
          abuf->ready = 0;
          abuf->timestamp = 0;
          abuf->sequence_number = 0;
        }
      }

      // pthread_mutex_lock(&ab_mutex);
    }
    int rc = pthread_cond_signal(&conn->flowcontrol);
    if (rc)
      debug(1, "Error signalling flowcontrol.");
  }
  pthread_mutex_unlock(&conn->ab_mutex);
}

int32_t rand_in_range(int32_t exclusive_range_limit) {
  static uint32_t lcg_prev = 12345;
  // returns a pseudo random integer in the range 0 to (exclusive_range_limit-1) inclusive
  int64_t sp = lcg_prev;
  int64_t rl = exclusive_range_limit;
  lcg_prev = lcg_prev * 69069 + 3; // crappy psrg
  sp = sp * rl; // 64 bit calculation. INtersting part if above the 32 rightmost bits;
  return sp >> 32;
}

static inline void process_sample(int32_t sample, char **outp, enum sps_format_t format, int volume,
                                  int dither, rtsp_conn_info *conn) {
  int64_t hyper_sample = sample;
  int result = 0;

  if (config.loudness) {
    hyper_sample <<=
        32; // Do not apply volume as it has already been done with the Loudness DSP filter
  } else {
    int64_t hyper_volume = (int64_t)volume << 16;
    hyper_sample =
        hyper_sample * hyper_volume; // this is 64 bit bit multiplication -- we may need to
                                     // dither it down to its
                                     // target resolution
  }

  // next, do dither, if necessary
  if (dither) {

    // add a TPDF dither -- see
    // http://www.users.qwest.net/%7Evolt42/cadenzarecording/DitherExplained.pdf
    // and the discussion around https://www.hydrogenaud.io/forums/index.php?showtopic=16963&st=25

    // I think, for a 32 --> 16 bits, the range of
    // random numbers needs to be from -2^16 to 2^16, i.e. from -65536 to 65536 inclusive, not from
    // -32768 to +32767

    // See the original paper at
    // http://www.ece.rochester.edu/courses/ECE472/resources/Papers/Lipshitz_1992.pdf
    // by Lipshitz, Wannamaker and Vanderkooy, 1992.

    int64_t dither_mask = 0;
    switch (format) {
    case SPS_FORMAT_S32:
      dither_mask = (int64_t)1 << (64 + 1 - 32);
      break;
    case SPS_FORMAT_S24:
    case SPS_FORMAT_S24_3LE:
    case SPS_FORMAT_S24_3BE:
      dither_mask = (int64_t)1 << (64 + 1 - 24);
      break;
    case SPS_FORMAT_S16:
      dither_mask = (int64_t)1 << (64 + 1 - 16);
      break;
    case SPS_FORMAT_S8:
    case SPS_FORMAT_U8:
      dither_mask = (int64_t)1 << (64 + 1 - 8);
      break;
    case SPS_FORMAT_UNKNOWN:
      die("Unexpected SPS_FORMAT_UNKNOWN while calculating dither mask.");
    }
    dither_mask -= 1;
    // int64_t r = r64i();
    int64_t r = ranarray64i(); // use an array of precalculated pseudorandom numbers rather than
                               // calculating them on the fly. Should be easier on low-powered
                               // processors

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
  case SPS_FORMAT_S24:
    hyper_sample >>= (64 - 24);
    *(int32_t *)op = hyper_sample;
    result = 4;
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
  }

  *outp += result;
}

// get the next frame, when available. return 0 if underrun/stream reset.
static abuf_t *buffer_get_frame(rtsp_conn_info *conn) {
  // int16_t buf_fill;
  uint64_t local_time_now;
  // struct timespec tn;
  abuf_t *abuf = 0;
  int i;
  abuf_t *curframe = 0;
  int notified_buffer_empty = 0; // diagnostic only

  pthread_mutex_lock(&conn->ab_mutex);
  int wait;
  long dac_delay = 0; // long because alsa returns a long
  do {
    // get the time
    local_time_now = get_absolute_time_in_fp(); // type okay

    // if config.timeout (default 120) seconds have elapsed since the last audio packet was
    // received, then we should stop.
    // config.timeout of zero means don't check..., but iTunes may be confused by a long gap
    // followed by a resumption...

    if ((conn->time_of_last_audio_packet != 0) && (conn->stop == 0) &&
        (config.dont_check_timeout == 0)) {
      uint64_t ct = config.timeout; // go from int to 64-bit int
      //      if (conn->packet_count>500) { //for testing -- about 4 seconds of play first
      if ((local_time_now > conn->time_of_last_audio_packet) &&
          (local_time_now - conn->time_of_last_audio_packet >= ct << 32)) {
        debug(1, "As Yeats almost said, \"Too long a silence / can make a stone of the heart\" "
                 "from RTSP conversation %d.",
              conn->connection_number);
        conn->stop = 1;
        pthread_kill(conn->thread, SIGUSR1);
      }
    }
    int rco = get_requested_connection_state_to_output();

    if (conn->connection_state_to_output != rco) {
      conn->connection_state_to_output = rco;
      // change happening
      if (conn->connection_state_to_output == 0) { // going off
        pthread_mutex_lock(&conn->flush_mutex);
        conn->flush_requested = 1;
        pthread_mutex_unlock(&conn->flush_mutex);
      }
    }

    pthread_mutex_lock(&conn->flush_mutex);
    if (conn->flush_requested == 1) {
      if (config.output->flush)
        config.output->flush();
      ab_resync(conn);
      conn->first_packet_timestamp = 0;
      conn->first_packet_time_to_play = 0;
      conn->time_since_play_started = 0;
      conn->flush_requested = 0;
    }
    pthread_mutex_unlock(&conn->flush_mutex);

    uint32_t flush_limit = 0;
    if (conn->ab_synced) {
      do {
        curframe = conn->audio_buffer + BUFIDX(conn->ab_read);
        if ((conn->ab_read != conn->ab_write) &&
            (curframe->ready)) { // it could be synced and empty, under
                                 // exceptional circumstances, with the
                                 // frame unused, thus apparently ready

          if (curframe->sequence_number != conn->ab_read) {
            // some kind of sync problem has occurred.
            if (BUFIDX(curframe->sequence_number) == BUFIDX(conn->ab_read)) {
              // it looks like some kind of aliasing has happened
              if (seq_order(conn->ab_read, curframe->sequence_number, conn->ab_read)) {
                conn->ab_read = curframe->sequence_number;
                debug(1, "Aliasing of buffer index -- reset.");
              }
            } else {
              debug(1, "Inconsistent sequence numbers detected");
            }
          }

          if ((conn->flush_rtp_timestamp != 0) &&
              (curframe->timestamp <= conn->flush_rtp_timestamp)) {
            debug(1, "Dropping flushed packet seqno %u, timestamp %lld", curframe->sequence_number,
                  curframe->timestamp);
            curframe->ready = 0;
            flush_limit++;
            conn->ab_read = SUCCESSOR(conn->ab_read);
          }
          if (curframe->timestamp > conn->flush_rtp_timestamp)
            conn->flush_rtp_timestamp = 0;
        }
      } while ((conn->flush_rtp_timestamp != 0) && (flush_limit <= 8820) && (curframe->ready == 0));

      if (flush_limit == 8820) {
        debug(1, "Flush hit the 8820 frame limit!");
        flush_limit = 0;
      }

      curframe = conn->audio_buffer + BUFIDX(conn->ab_read);

      if (curframe->ready) {
        notified_buffer_empty = 0; // at least one buffer now -- diagnostic only.
        if (conn->ab_buffering) {  // if we are getting packets but not yet forwarding them to the
                                   // player
          int have_sent_prefiller_silence =
              1; // set true when we have sent some silent frames to the
                 // DAC
          int64_t reference_timestamp;
          uint64_t reference_timestamp_time, remote_reference_timestamp_time;
          get_reference_timestamp_stuff(&reference_timestamp, &reference_timestamp_time,
                                        &remote_reference_timestamp_time, conn);
          reference_timestamp *= conn->output_sample_ratio;
          if (conn->first_packet_timestamp == 0) { // if this is the very first packet
                                                   // debug(1,"First frame seen, time %u, with %d
            // frames...",curframe->timestamp,seq_diff(ab_read, ab_write));

            if (reference_timestamp) { // if we have a reference time
              // debug(1,"First frame seen with timestamp...");
              conn->first_packet_timestamp =
                  curframe->timestamp; // we will keep buffering until we are
                                       // supposed to start playing this
              have_sent_prefiller_silence = 0;

// debug(1, "First packet timestamp is %" PRId64 ".", conn->first_packet_timestamp);

// say we have started playing here
#ifdef CONFIG_METADATA
              debug(2, "pffr");
              send_ssnc_metadata(
                  'pffr', NULL, 0,
                  0); // "first frame received", but don't wait if the queue is locked
#endif
              // Here, calculate when we should start playing. We need to know when to allow the
              // packets to be sent to the player.
              // We will send packets of silence from now until that time and then we will send the
              // first packet, which will be followed by the subsequent packets.

              // we will get a fix every second or so, which will be stored as a pair consisting of
              // the time when the packet with a particular timestamp should be played, neglecting
              // latencies, etc.

              // It probably won't  be the timestamp of our first packet, however, so we might have
              // to do some calculations.

              // To calculate when the first packet will be played, we figure out the exact time the
              // packet should be played according to its timestamp and the reference time.
              // We then need to add the desired latency, typically 88200 frames.

              // Then we need to offset this by the backend latency offset. For example, if we knew
              // that the audio back end has a latency of 100 ms, we would
              // ask for the first packet to be emitted 100 ms earlier than it should, i.e. -4410
              // frames, so that when it got through the audio back end,
              // if would be in sync. To do this, we would give it a latency offset of -100 ms, i.e.
              // -4410 frames.

              // debug(1, "Output sample ratio is %d", conn->output_sample_ratio);

              int64_t delta = (conn->first_packet_timestamp - reference_timestamp) +
                              conn->latency * conn->output_sample_ratio +
                              (int64_t)(config.audio_backend_latency_offset * config.output_rate);

              if (delta >= 0) {
                int64_t delta_fp_sec =
                    (delta << 32) / config.output_rate; // int64_t which is positive
                conn->first_packet_time_to_play = reference_timestamp_time + delta_fp_sec;
              } else {
                int64_t abs_delta = -delta;
                int64_t delta_fp_sec =
                    (abs_delta << 32) / config.output_rate; // int64_t which is positive
                conn->first_packet_time_to_play = reference_timestamp_time - delta_fp_sec;
              }

              if (local_time_now >= conn->first_packet_time_to_play) {
                debug(
                    1,
                    "First packet is late! It should have played before now. Flushing 0.5 seconds");
                player_flush(conn->first_packet_timestamp + 5 * 4410 * conn->output_sample_ratio,
                             conn);
              }
            }
          }

          if (conn->first_packet_time_to_play != 0) {
            // recalculate conn->first_packet_time_to_play -- the latency might change
            int64_t delta = (conn->first_packet_timestamp - reference_timestamp) +
                            conn->latency * conn->output_sample_ratio +
                            (int64_t)(config.audio_backend_latency_offset * config.output_rate);

            if (delta >= 0) {
              int64_t delta_fp_sec =
                  (delta << 32) / config.output_rate; // int64_t which is positive
              conn->first_packet_time_to_play = reference_timestamp_time + delta_fp_sec;
            } else {
              int64_t abs_delta = -delta;
              int64_t delta_fp_sec =
                  (abs_delta << 32) / config.output_rate; // int64_t which is positive
              conn->first_packet_time_to_play = reference_timestamp_time - delta_fp_sec;
            }

            // now, the size of the initial silence must be affected by the lead-in time.
            // it must be somewhat less than the lead-in time so that dynamic adjustments can be
            // made
            // to compensate for delays due to paging, etc.
            // The suggestion is that it should be at least 100 ms less than the lead-in time.

            int64_t max_dac_delay = config.output_rate / 10; // so the lead-in time must be greater
                                                             // than this, say 0.2 sec, to allow for
                                                             // dynamic adjustment
            int64_t filler_size = max_dac_delay; // 0.1 second -- the maximum we'll add to the DAC

            if (local_time_now >= conn->first_packet_time_to_play) {
              // debug(1,"Gone past starting time");
              have_sent_prefiller_silence = 1;
              conn->ab_buffering = 0;

              // we've gone past the time...
              // debug(1,"Run past the exact start time by %llu frames, with time now of %llx, fpttp
              // of %llx and dac_delay of %d and %d packets;
              // flush.",(((tn-conn->first_packet_time_to_play)*config.output_rate)>>32)+dac_delay,tn,conn->first_packet_time_to_play,dac_delay,seq_diff(ab_read,
              // ab_write));

              /*
                            if (config.output->flush)
                              config.output->flush();
                            ab_resync(conn);
                            conn->first_packet_timestamp = 0;
                            conn->first_packet_time_to_play = 0;
                            conn->time_since_play_started = 0;
              */
            } else {
              // do some calculations
              int64_t lead_time = conn->first_packet_time_to_play - local_time_now;
              int64_t lead_in_time =
                  (int64_t)(config.audio_backend_silent_lead_in_time * (int64_t)0x100000000);
              // debug(1,"Lead time is %llx at fpttp
              // %llx.",lead_time,conn->first_packet_time_to_play);
              // an audio_backend_silent_lead_in_time of less than zero means start filling ASAP
              if ((lead_in_time < 0) || (lead_time <= lead_in_time)) {
                // debug(1,"Checking");
                if (config.output->delay) {
                  // conn->first_packet_time_to_play is definitely later than local_time_now
                  if (have_sent_prefiller_silence != 0) {
                    int resp = config.output->delay(&dac_delay);

                    if (resp != 0) {
                      debug(1, "Error %d getting dac_delay in buffer_get_frame.", resp);
                      dac_delay = 0;
                    }
                  } else {
                    dac_delay = 0;
                  }
                  int64_t gross_frame_gap =
                      ((conn->first_packet_time_to_play - local_time_now) * config.output_rate) >>
                      32;
                  int64_t exact_frame_gap = gross_frame_gap - dac_delay;
                  if (exact_frame_gap < 0) {
                    // we've gone past the time...
                    // debug(1,"Run past time.");
                    // debug(1,"Run a bit past the exact start time by %lld frames, with time now of
                    // %llx, fpttp of %llx and dac_delay of %d and %d packets;
                    // flush.",-exact_frame_gap,tn,conn->first_packet_time_to_play,dac_delay,seq_diff(ab_read,
                    // ab_write));
                    if (config.output->flush)
                      config.output->flush();
                    ab_resync(conn);
                    conn->first_packet_timestamp = 0;
                    conn->first_packet_time_to_play = 0;
                  } else {
                    int64_t fs = filler_size;
                    if (fs > (max_dac_delay - dac_delay))
                      fs = max_dac_delay - dac_delay;
                    if (fs < 0) {
                      debug(2,
                            "frame size (fs) < 0 with max_dac_delay of %lld and dac_delay of %ld",
                            max_dac_delay, dac_delay);
                      fs = 0;
                    }
                    if ((exact_frame_gap <= fs) ||
                        (exact_frame_gap <= conn->max_frames_per_packet * 2)) {
                      fs = exact_frame_gap;
                      // debug(1,"Exact frame gap is %llu; play %d frames of silence. Dac_delay is
                      // %d,
                      // with %d packets, ab_read is %04x, ab_write is
                      // %04x.",exact_frame_gap,fs,dac_delay,seq_diff(ab_read,
                      // ab_write),ab_read,ab_write);
                      conn->ab_buffering = 0;
                    }
                    signed short *silence;
                    // if (fs==0)
                    //  debug(2,"Zero length silence buffer needed with gross_frame_gap of %lld and
                    //  dac_delay of %lld.",gross_frame_gap,dac_delay);
                    // the fs (number of frames of silence to play) can be zero in the DAC doesn't
                    // start
                    // ouotputting frames for a while -- it could get loaded up but not start
                    // responding
                    // for many milliseconds.
                    if (fs > 0) {
                      silence = malloc(conn->output_bytes_per_frame * fs);
                      if (silence == NULL)
                        debug(1, "Failed to allocate %d byte silence buffer.", fs);
                      else {
                        memset(silence, 0, conn->output_bytes_per_frame * fs);
                        // debug(1,"Frames to start: %llu, DAC delay %d, buffer: %d
                        // packets.",exact_frame_gap,dac_delay,seq_diff(conn->ab_read,
                        // conn->ab_write, conn->ab_read));
                        config.output->play(silence, fs);
                        free(silence);
                      }
                    }
                    have_sent_prefiller_silence =
                        1; // even if we haven't sent silence because it's zero frames long...
                  }
                } else {
                  // no delay function on back end -- just send the prefiller silence
                  // debug(1,"Back end has no delay function.");
                  // send the appropriate prefiller here...

                  signed short *silence;
                  if (lead_time != 0) {
                    int64_t frame_gap = (lead_time * config.output_rate) >> 32;
                    // debug(1,"%d frames needed.",frame_gap);
                    while (frame_gap > 0) {
                      size_t fs = config.output_rate / 10;
                      if (fs > frame_gap)
                        fs = frame_gap;

                      silence = malloc(conn->output_bytes_per_frame * fs);
                      if (silence == NULL)
                        debug(1, "Failed to allocate %d frame silence buffer.", fs);
                      else {
                        // debug(1, "Outputting %d frames of silence.", fs);
                        memset(silence, 0, conn->output_bytes_per_frame * fs);
                        config.output->play(silence, fs);
                        free(silence);
                      }
                      frame_gap -= fs;
                    }
                  }
                  have_sent_prefiller_silence = 1;
                  conn->ab_buffering = 0;
                }
              }
            }
          }
          if (conn->ab_buffering == 0) {
            // note the time of the playing of the first frame
            uint64_t reference_timestamp_time; // don't need this...
            get_reference_timestamp_stuff(&conn->play_segment_reference_frame,
                                          &reference_timestamp_time,
                                          &conn->play_segment_reference_frame_remote_time, conn);
            conn->play_segment_reference_frame *= conn->output_sample_ratio;
#ifdef CONFIG_METADATA
            debug(2, "prsm");
            send_ssnc_metadata('prsm', NULL, 0,
                               0); // "resume", but don't wait if the queue is locked
#endif
          }
        }
      }
    }

    // Here, we work out whether to release a packet or wait
    // We release a buffer when the time is right.

    // To work out when the time is right, we need to take account of (1) the actual time the packet
    // should be released,
    // (2) the latency requested, (3) the audio backend latency offset and (4) the desired length of
    // the audio backend's buffer

    // The time is right if the current time is later or the same as
    // The packet time + (latency + latency offset - backend_buffer_length).
    // Note: the last three items are expressed in frames and must be converted to time.

    int do_wait = 0; // don't wait unless we can really prove we must
    if ((conn->ab_synced) && (curframe) && (curframe->ready) && (curframe->timestamp)) {
      do_wait =
          1; // if the current frame exists and is ready, then wait unless it's time to let it go...
      int64_t reference_timestamp;
      uint64_t reference_timestamp_time, remote_reference_timestamp_time;
      get_reference_timestamp_stuff(&reference_timestamp, &reference_timestamp_time,
                                    &remote_reference_timestamp_time, conn); // all types okay
      reference_timestamp *= conn->output_sample_ratio;
      if (reference_timestamp) {                        // if we have a reference time
        int64_t packet_timestamp = curframe->timestamp; // types okay
        int64_t delta = packet_timestamp - reference_timestamp;
        int64_t offset =
            conn->latency * conn->output_sample_ratio +
            (int64_t)(config.audio_backend_latency_offset * config.output_rate) -
            config.audio_backend_buffer_desired_length *
                config.output_rate; // all arguments are int32_t, so expression promotion okay
        int64_t net_offset = delta + offset;              // okay
        uint64_t time_to_play = reference_timestamp_time; // type okay
        if (net_offset >= 0) {
          uint64_t net_offset_fp_sec =
              (net_offset << 32) / config.output_rate; // int64_t which is positive
          time_to_play += net_offset_fp_sec;           // using the latency requested...
          // debug(2,"Net Offset: %lld, adjusted: %lld.",net_offset,net_offset_fp_sec);
        } else {
          int64_t abs_net_offset = -net_offset;
          uint64_t net_offset_fp_sec =
              (abs_net_offset << 32) / config.output_rate; // int64_t which is positive
          time_to_play -= net_offset_fp_sec;
          // debug(2,"Net Offset: %lld, adjusted: -%lld.",net_offset,net_offset_fp_sec);
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
        }
        do_wait = 1;
      }
    wait = (conn->ab_buffering || (do_wait != 0) || (!conn->ab_synced)) &&
           (!conn->player_thread_please_stop);

    if (wait) {
      uint64_t time_to_wait_for_wakeup_fp =
          ((uint64_t)1 << 32) / conn->input_rate; // this is time period of one frame
      time_to_wait_for_wakeup_fp *= 4 * 352;      // four full 352-frame packets
      time_to_wait_for_wakeup_fp /= 3;            // four thirds of a packet time

#ifdef COMPILE_FOR_LINUX_AND_FREEBSD_AND_CYGWIN_AND_OPENBSD
      uint64_t time_of_wakeup_fp = local_time_now + time_to_wait_for_wakeup_fp;
      uint64_t sec = time_of_wakeup_fp >> 32;
      uint64_t nsec = ((time_of_wakeup_fp & 0xffffffff) * 1000000000) >> 32;

      struct timespec time_of_wakeup;
      time_of_wakeup.tv_sec = sec;
      time_of_wakeup.tv_nsec = nsec;
      pthread_cond_timedwait(&conn->flowcontrol, &conn->ab_mutex, &time_of_wakeup);
// int rc = pthread_cond_timedwait(&flowcontrol,&ab_mutex,&time_of_wakeup);
// if (rc!=0)
//  debug(1,"pthread_cond_timedwait returned error code %d.",rc);
#endif
#ifdef COMPILE_FOR_OSX
      uint64_t sec = time_to_wait_for_wakeup_fp >> 32;
      uint64_t nsec = ((time_to_wait_for_wakeup_fp & 0xffffffff) * 1000000000) >> 32;
      struct timespec time_to_wait;
      time_to_wait.tv_sec = sec;
      time_to_wait.tv_nsec = nsec;
      pthread_cond_timedwait_relative_np(&conn->flowcontrol, &conn->ab_mutex, &time_to_wait);
#endif
    }
  } while (wait);

  if (conn->player_thread_please_stop) {
    pthread_mutex_unlock(&conn->ab_mutex);
    return 0;
  }

  // seq_t read = conn->ab_read;

  // check if t+8, t+16, t+32, t+64, t+128, ... (buffer_start_fill / 2)
  // packets have arrived... last-chance resend

  if (!conn->ab_buffering) {
    // check once, after a short period of has elapsed, assuming 352 frames per packet
    i = ((250 * 44100) / 352) / 1000; // approx 250 ms
    if (i < seq_diff(conn->ab_read, conn->ab_write, conn->ab_read)) {
      seq_t next = seq_sum(conn->ab_read, i);
      abuf = conn->audio_buffer + BUFIDX(next);
      if (!abuf->ready) {
        if (config.disable_resend_requests == 0) {
          rtp_request_resend(next, 1, conn);
          // debug(1,"Resend request for packet %u.",next);
          conn->resend_requests++;
        }
      }
    }
  }

  if (!curframe->ready) {
    // debug(1, "Supplying a silent frame for frame %u", read);
    conn->missing_packets++;
    curframe->timestamp = 0; // indicate a silent frame should be substituted
  }
  curframe->ready = 0;
  conn->ab_read = SUCCESSOR(conn->ab_read);
  pthread_mutex_unlock(&conn->ab_mutex);
  return curframe;
}

static inline short shortmean(short a, short b) {
  long al = (long)a;
  long bl = (long)b;
  long longmean = (al + bl) / 2;
  short r = (short)longmean;
  if (r != longmean)
    debug(1, "Error calculating average of two shorts");
  return r;
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
static int stuff_buffer_basic_32(int32_t *inptr, int length, enum sps_format_t l_output_format,
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

  pthread_mutex_lock(&conn->vol_mutex);
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
  pthread_mutex_unlock(&conn->vol_mutex);
  conn->amountStuffed = tstuff;
  return length + tstuff;
}

#ifdef HAVE_LIBSOXR
// this takes an array of signed 32-bit integers and
// (a) uses libsoxr to
// resample the array to have one more or one less frame, as specified in
// stuff,
// (b) multiplies each sample by the fixedvolume (a 16-bit quantity)
// (c) dithers the result to the output size 32/24/16/8 bits
// (d) outputs the result in the approprate format
// formats accepted so far include U8, S8, S16, S24, S24_3LE, S24_3BE and S32

static int stuff_buffer_soxr_32(int32_t *inptr, int32_t *scratchBuffer, int length,
                                enum sps_format_t l_output_format, char *outptr, int stuff,
                                int dither, rtsp_conn_info *conn) {
  if (scratchBuffer == NULL) {
    die("soxr scratchBuffer not initialised.");
  }
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

    soxr_error_t error = soxr_oneshot(length, length + tstuff, 2, /* Rates and # of chans. */
                                      inptr, length, NULL,        /* Input. */
                                      scratchBuffer, length + tstuff, &odone, /* Output. */
                                      &io_spec,    /* Input, output and transfer spec. */
                                      NULL, NULL); /* Default configuration.*/

    if (error)
      die("soxr error: %s\n", "error: %s\n", soxr_strerror(error));

    if (odone > length + 1)
      die("odone = %d!\n", odone);

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
    op = scratchBuffer + (length + tstuff - gpm) * sizeof(int32_t);
    ip = inptr + (length - gpm) * sizeof(int32_t);
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
  conn->amountStuffed = tstuff;
  return length + tstuff;
}
#endif

typedef struct stats { // statistics for running averages
  int64_t sync_error, correction, drift;
} stats_t;

static void *player_thread_func(void *arg) {

  rtsp_conn_info *conn = (rtsp_conn_info *)arg;

  conn->please_stop = 0;
  conn->packet_count = 0;
  conn->previous_random_number = 0;
  conn->input_bytes_per_frame = 4;
  conn->player_thread_please_stop = 0;
  conn->decoder_in_use = 0;
  conn->ab_buffering = 1;
  conn->ab_synced = 0;
  conn->first_packet_timestamp = 0;
  conn->flush_requested = 0;
  conn->fix_volume = 0x10000;

  if (conn->latency == 0) {
    debug(3, "No latency has (yet) been specified. Setting 88,200 (2 seconds) frames "
             "as a default.");
    conn->latency = 88200;
  }

  int rc = pthread_mutex_init(&conn->ab_mutex, NULL);
  if (rc)
    debug(1, "Error initialising ab_mutex.");
  rc = pthread_mutex_init(&conn->flush_mutex, NULL);
  if (rc)
    debug(1, "Error initialising flush_mutex.");
  rc = pthread_mutex_init(&conn->vol_mutex, NULL);
  if (rc)
    debug(1, "Error initialising vol_mutex.");
// set the flowcontrol condition variable to wait on a monotonic clock
#ifdef COMPILE_FOR_LINUX_AND_FREEBSD_AND_CYGWIN_AND_OPENBSD
  pthread_condattr_t attr;
  pthread_condattr_init(&attr);
  pthread_condattr_setclock(&attr, CLOCK_MONOTONIC); // can't do this in OS X, and don't need it.
  rc = pthread_cond_init(&conn->flowcontrol, &attr);
#endif
#ifdef COMPILE_FOR_OSX
  rc = pthread_cond_init(&conn->flowcontrol, NULL);
#endif
  if (rc)
    debug(1, "Error initialising flowcontrol condition variable.");
  config.output->start(config.output_rate, config.output_format);

  init_decoder((int32_t *)&conn->stream.fmtp,
               conn); // this sets up incoming rate, bit depth, channels
  // must be after decoder init
  init_buffer(conn);

  if (conn->stream.encrypted) {
#ifdef HAVE_LIBMBEDTLS
    memset(&conn->dctx, 0, sizeof(mbedtls_aes_context));
    mbedtls_aes_setkey_dec(&conn->dctx, conn->stream.aeskey, 128);
#endif

#ifdef HAVE_LIBPOLARSSL
    memset(&conn->dctx, 0, sizeof(aes_context));
    aes_setkey_dec(&conn->dctx, conn->stream.aeskey, 128);
#endif

#ifdef HAVE_LIBSSL
    AES_set_decrypt_key(conn->stream.aeskey, 128, &conn->aes);
#endif
  }

  conn->timestamp_epoch = 0; // indicate that the next timestamp will be the first one.
  conn->maximum_timestamp_interval =
      conn->input_rate * 60; // actually there shouldn't be more than about 13
                             // seconds of a gap between successive rtptimes, at
                             // worst

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
    conn->output_bytes_per_frame = 8;
    break;
  case SPS_FORMAT_S32:
    conn->output_bytes_per_frame = 8;
    break;
  default:
    conn->output_bytes_per_frame = 4;
  }

  debug(3, "Output frame bytes is %d.", conn->output_bytes_per_frame);

  // create and start the timing, control and audio receiver threads
  pthread_t rtp_audio_thread, rtp_control_thread, rtp_timing_thread;
  pthread_create(&rtp_audio_thread, NULL, &rtp_audio_receiver, (void *)conn);
  pthread_create(&rtp_control_thread, NULL, &rtp_control_receiver, (void *)conn);
  pthread_create(&rtp_timing_thread, NULL, &rtp_timing_receiver, (void *)conn);

  conn->session_corrections = 0;
  conn->play_segment_reference_frame = 0; // zero signals that we are not in a play segment

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
#define trend_interval 1003

  stats_t statistics[trend_interval];
  int number_of_statistics, oldest_statistic, newest_statistic;
  int at_least_one_frame_seen = 0;
  int64_t tsum_of_sync_errors, tsum_of_corrections, tsum_of_insertions_and_deletions,
      tsum_of_drifts;
  int64_t previous_sync_error, previous_correction;
  int64_t minimum_dac_queue_size = INT64_MAX;
  int32_t minimum_buffer_occupancy = INT32_MAX;
  int32_t maximum_buffer_occupancy = INT32_MIN;

  time_t playstart = time(NULL);

  conn->buffer_occupancy = 0;

  int play_samples = 0;
  int64_t current_delay;
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

  signed short *inbuf, *tbuf, *silence;

  int32_t *sbuf;

  char *outbuf;

  int inbuflength;

  int output_bit_depth = 16; // default;

  switch (config.output_format) {
  case SPS_FORMAT_S8:
  case SPS_FORMAT_U8:
    output_bit_depth = 8;
    break;
  case SPS_FORMAT_S16:
    output_bit_depth = 16;
    break;
  case SPS_FORMAT_S24:
  case SPS_FORMAT_S24_3LE:
  case SPS_FORMAT_S24_3BE:
    output_bit_depth = 24;
    break;
  case SPS_FORMAT_S32:
    output_bit_depth = 32;
    break;
  case SPS_FORMAT_UNKNOWN:
    die("Unknown format choosing output bit depth");
  }

  debug(3, "Output bit depth is %d.", output_bit_depth);

  if (conn->input_bit_depth > output_bit_depth) {
    debug(1, "Dithering will be enabled because the input bit depth is greater than the output bit "
             "depth");
  }
  if (conn->fix_volume != 0x10000) {
    debug(1, "Dithering will be enabled because the output volume is being altered in software");
  }

  // we need an intermediate "transition" buffer

  // if ((input_rate!=config.output_rate) || (input_bit_depth!=output_bit_depth)) {
  // debug(1,"Define tbuf of length
  // %d.",output_bytes_per_frame*(max_frames_per_packet*output_sample_ratio+max_frame_size_change));
  tbuf = malloc(sizeof(int32_t) * 2 * (conn->max_frames_per_packet * conn->output_sample_ratio +
                                       conn->max_frame_size_change));
  if (tbuf == NULL)
    die("Failed to allocate memory for the transition buffer.");
  sbuf = 0;
  // initialise this, because soxr stuffing might be chosen later
  // if (config.packet_stuffing == ST_soxr) { // needed for stuffing
  sbuf = malloc(sizeof(int32_t) * 2 * (conn->max_frames_per_packet * conn->output_sample_ratio +
                                       conn->max_frame_size_change));
  if (sbuf == NULL)
    debug(1, "Failed to allocate memory for the sbuf buffer.");
  // }
  // We might need an output buffer and a buffer of silence.
  // The size of these dependents on the number of frames, the size of each frame and the maximum
  // size change
  outbuf = malloc(
      conn->output_bytes_per_frame *
      (conn->max_frames_per_packet * conn->output_sample_ratio + conn->max_frame_size_change));
  if (outbuf == NULL)
    die("Failed to allocate memory for an output buffer.");
  silence = malloc(conn->output_bytes_per_frame * conn->max_frames_per_packet *
                   conn->output_sample_ratio);
  if (silence == NULL)
    die("Failed to allocate memory for a silence buffer.");
  memset(silence, 0,
         conn->output_bytes_per_frame * conn->max_frames_per_packet * conn->output_sample_ratio);
  conn->first_packet_timestamp = 0;
  conn->missing_packets = conn->late_packets = conn->too_late_packets = conn->resend_requests = 0;
  conn->flush_rtp_timestamp =
      0; // it seems this number has a special significance -- it seems to be used
         // as a null operand, so we'll use it like that too
  int sync_error_out_of_bounds =
      0; // number of times in a row that there's been a serious sync error

// stop looking elsewhere for DACP stuff
#ifdef HAVE_DACP_CLIENT
  set_dacp_server_information(conn); // this will start scanning when a port is registered by the
                                     // code initiated by the mdns_dacp_monitor
#else
  // this is only used for compatability, if dacp stuff isn't enabled.
  // start an mdns/zeroconf thread to look for DACP messages containing our DACP_ID and getting the
  // port number
  conn->dapo_private_storage = mdns_dacp_monitor(conn->dacp_id);
#endif

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
               "max buffer occupancy");
      } else {
        inform("sync error in milliseconds, "
               "total packets, "
               "missing packets, "
               "late packets, "
               "too late packets, "
               "resend requests, "
               "min DAC queue size, "
               "min buffer occupancy, "
               "max buffer occupancy");
      }
    } else {
      inform("sync error in milliseconds, "
             "total packets, "
             "missing packets, "
             "late packets, "
             "too late packets, "
             "resend requests, "
             "min buffer occupancy, "
             "max buffer occupancy");
    }
  }

  // set the default volume to whaterver it was before, as stored in the config airplay_volume
  debug(3, "Set initial volume to %f.", config.airplay_volume);

  player_volume(config.airplay_volume, conn);
  int64_t frames_to_drop = 0;
  // debug(1, "Play begin");
  if (play_number % 100 == 0)
    debug(3, "Play frame %d.", play_number);
  while (!conn->player_thread_please_stop) {
    abuf_t *inframe = buffer_get_frame(conn);
    if (inframe) {
      inbuf = inframe->data;
      inbuflength = inframe->length;
      if (inbuf) {
        play_number++;
        conn->play_number_after_flush++;
        if (inframe->timestamp == 0) {
          // debug(1,"Player has a supplied silent frame.");
          conn->last_seqno_read = (SUCCESSOR(conn->last_seqno_read) &
                                   0xffff); // manage the packet out of sequence minder
          config.output->play(silence, conn->max_frames_per_packet * conn->output_sample_ratio);
        } else if (conn->play_number_after_flush < 10) {
          /*
          int64_t difference = 0;
          if (last_timestamp)
            difference = inframe->timestamp - last_timestamp;
          last_timestamp = inframe->timestamp;
          debug(1, "Play number %d, monotonic timestamp %llx, difference
          %lld.",conn->play_number_after_flush,inframe->timestamp,difference);
          */
          config.output->play(silence, conn->max_frames_per_packet * conn->output_sample_ratio);
        } else if (frames_to_drop) {
          // debug(2,"%lld frames to drop.",frames_to_drop);
          frames_to_drop -= inframe->length;
          if (frames_to_drop < 0)
            frames_to_drop = 0;
        } else {
          int enable_dither = 0;
          if ((conn->fix_volume != 0x10000) || (conn->input_bit_depth > output_bit_depth) ||
              (config.playback_mode == ST_mono))
            enable_dither = 1;

          // here, let's transform the frame of data, if necessary

          switch (conn->input_bit_depth) {
          case 16: {
            int i, j;
            int16_t ls, rs;
            int32_t ll = 0, rl = 0;
            int16_t *inps = inbuf;
            // int16_t *outps = tbuf;
            int32_t *outpl = (int32_t *)tbuf;
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

          inbuflength *= conn->output_sample_ratio;

          // We have a frame of data. We need to see if we want to add or remove a frame from it to
          // keep in sync.
          // So we calculate the timing error for the first frame in the DAC.
          // If it's ahead of time, we add one audio frame to this frame to delay a subsequent frame
          // If it's late, we remove an audio frame from this frame to bring a subsequent frame
          // forward in time

          at_least_one_frame_seen = 1;

          int64_t reference_timestamp;
          uint64_t reference_timestamp_time, remote_reference_timestamp_time;
          get_reference_timestamp_stuff(&reference_timestamp, &reference_timestamp_time,
                                        &remote_reference_timestamp_time, conn); // types okay
          reference_timestamp *= conn->output_sample_ratio;
          int64_t rt, nt;
          rt = reference_timestamp; // uint32_t to int64_t
          nt = inframe->timestamp;  // uint32_t to int64_t

          uint64_t local_time_now = get_absolute_time_in_fp(); // types okay
          // struct timespec tn;
          // clock_gettime(CLOCK_MONOTONIC,&tn);
          // uint64_t
          // local_time_now=((uint64_t)tn.tv_sec<<32)+((uint64_t)tn.tv_nsec<<32)/1000000000;

          int64_t td = 0;
          int64_t td_in_frames = 0;
          if (local_time_now >= reference_timestamp_time) {
            // debug(1,"td is %lld.",td);
            td = local_time_now - reference_timestamp_time; // this is the positive value.
                                                            // Conversion is positive uint64_t to
                                                            // int64_t, thus okay
            td_in_frames = (td * config.output_rate) >> 32;
          } else {
            td = reference_timestamp_time - local_time_now; // this is the absolute value, which
                                                            // should be negated. Conversion is
                                                            // positive uint64_t to int64_t, thus
                                                            // okay.
            td_in_frames = (td * config.output_rate) >>
                           32; // use the absolute td value for the present. Types okay
            td_in_frames = -td_in_frames;
            td = -td; // should be okay, as the range of values should be very small w.r.t 64 bits
          }

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
              debug(1, "Player: packets out of sequence: expected: %u, got: %u, with ab_read: %u "
                       "and ab_write: %u.",
                    conn->last_seqno_read, inframe->sequence_number, conn->ab_read, conn->ab_write);
              conn->last_seqno_read = inframe->sequence_number; // reset warning...
            }
          }

          conn->buffer_occupancy =
              seq_diff(conn->ab_read, conn->ab_write, conn->ab_read); // int32_t from int32

          if (conn->buffer_occupancy < minimum_buffer_occupancy)
            minimum_buffer_occupancy = conn->buffer_occupancy;

          if (conn->buffer_occupancy > maximum_buffer_occupancy)
            maximum_buffer_occupancy = conn->buffer_occupancy;

          // here, we want to check (a) if we are meant to do synchronisation,
          // (b) if we have a delay procedure, (c) if we can get the delay.

          // If any of these are false, we don't do any synchronisation stuff

          int resp = -1; // use this as a flag -- if negative, we can't rely on a real known delay
          current_delay = -1; // use this as a failure flag

          if (config.output->delay) {
            long l_delay;
            resp = config.output->delay(&l_delay);
            current_delay = l_delay;
            if (resp == 0) { // no error
              if (current_delay < 0) {
                debug(1, "Underrun of %lld frames reported, but ignored.", current_delay);
                current_delay =
                    0; // could get a negative value if there was underrun, but ignore it.
              }
              if (current_delay < minimum_dac_queue_size) {
                minimum_dac_queue_size = current_delay; // update for display later
              }
            } else {
              debug(2, "Delay error %d when checking running latency.", resp);
            }
          }

          if (resp >= 0) {

            // this is the actual delay, including the latency we actually want, which will
            // fluctuate a good bit about a potentially rising or falling trend.
            int64_t delay = td_in_frames + rt - (nt - current_delay); // all int64_t

            // This is the timing error for the next audio frame in the DAC.

            // if positive, it means that the packet will be late -- the delay is longer than
            // requested
            // if negative, the packet will be early -- the delay is less than expected.

            sync_error =
                delay - (conn->latency * conn->output_sample_ratio +
                         (int64_t)(config.audio_backend_latency_offset *
                                   config.output_rate)); // int64_t from int64_t - int32_t, so okay

            // not too sure if abs() is implemented for int64_t, so we'll do it manually
            int64_t abs_sync_error = sync_error;
            if (abs_sync_error < 0)
              abs_sync_error = -abs_sync_error;

            if ((config.no_sync == 0) && (inframe->timestamp != 0) &&
                (!conn->player_thread_please_stop) && (config.resyncthreshold > 0.0) &&
                (abs_sync_error > config.resyncthreshold * config.output_rate)) {
              sync_error_out_of_bounds++;
            } else {
              sync_error_out_of_bounds = 0;
            }

            if (sync_error_out_of_bounds > 3) {
              // debug(1, "New lost sync with source for %d consecutive packets -- flushing and "
              //          "resyncing. Error: %lld.",
              //        sync_error_out_of_bounds, sync_error);
              sync_error_out_of_bounds = 0;

              int filler_length = config.resyncthreshold * config.output_rate; // number of samples
              if ((sync_error > 0) && (sync_error > filler_length)) {
                // debug(1, "Large positive sync error: %lld.", sync_error);
                frames_to_drop = sync_error / conn->output_sample_ratio;
              } else if ((sync_error < 0) && ((-sync_error) > filler_length)) {
                // debug(1, "Large negative sync error: %lld. Inserting silence.", sync_error);
                int silence_length = -sync_error;
                if (silence_length > (filler_length * 5))
                  silence_length = filler_length * 5;

                char *long_silence = malloc(conn->output_bytes_per_frame * silence_length);
                if (long_silence == NULL)
                  die("Failed to allocate memory for a long_silence buffer of %d frames.",
                      silence_length);
                memset(long_silence, 0, conn->output_bytes_per_frame * silence_length);
                config.output->play((short *)long_silence, silence_length);
                free(long_silence);
              }
            } else {

              // before we finally commit to this frame, check its sequencing and timing

              // require a certain error before bothering to fix it...
              if (sync_error > config.tolerance * config.output_rate) { // int64_t > int, okay
                amount_to_stuff = -1;
              }
              if (sync_error < -config.tolerance * config.output_rate) {
                amount_to_stuff = 1;
              }

              // only allow stuffing if there is enough time to do it -- check DAC buffer...
              if (current_delay < DAC_BUFFER_QUEUE_MINIMUM_LENGTH) {
                // debug(2,"DAC buffer too short(at %lld frames) to allow stuffing.",current_delay);
                amount_to_stuff = 0;
              }

              // try to keep the corrections definitely below 1 in 1000 audio frames

              // calculate the time elapsed since the play session started.

              if (amount_to_stuff) {
                if ((local_time_now) && (conn->first_packet_time_to_play) &&
                    (local_time_now >= conn->first_packet_time_to_play)) {

                  int64_t tp = (local_time_now - conn->first_packet_time_to_play) >>
                               32; // seconds int64_t from uint64_t which is always positive, so ok

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
              if (config.loudness
#ifdef CONFIG_CONVOLUTION
                  || config.convolution
#endif
                  ) {
                int32_t *tbuf32 = (int32_t *)tbuf;
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
                if (config.convolution) {
                  convolver_process_l(fbuf_l, inbuflength);
                  convolver_process_r(fbuf_r, inbuflength);

                  float gain = pow(10.0, config.convolution_gain / 20.0);
                  for (i = 0; i < inbuflength; ++i) {
                    fbuf_l[i] *= gain;
                    fbuf_r[i] *= gain;
                  }
                }
#endif

                if (config.loudness) {
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

              switch (config.packet_stuffing) {
              case ST_basic:
                //                if (amount_to_stuff) debug(1,"Basic stuff...");
                play_samples =
                    stuff_buffer_basic_32((int32_t *)tbuf, inbuflength, config.output_format,
                                          outbuf, amount_to_stuff, enable_dither, conn);
                break;
              case ST_soxr:
#ifdef HAVE_LIBSOXR
                //                if (amount_to_stuff) debug(1,"Soxr stuff...");
                play_samples = stuff_buffer_soxr_32((int32_t *)tbuf, (int32_t *)sbuf, inbuflength,
                                                    config.output_format, outbuf, amount_to_stuff,
                                                    enable_dither, conn);
#endif
                break;
              }

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

              if (outbuf == NULL)
                debug(1, "NULL outbuf to play -- skipping it.");
              else {
                if (play_samples == 0)
                  debug(1, "play_samples==0 skipping it (1).");
                else
                  config.output->play((short *)outbuf, play_samples); // remove the (short*)!
              }

              // check for loss of sync
              // timestamp of zero means an inserted silent frame in place of a missing frame
              /*
              if ((config.no_sync == 0) && (inframe->timestamp != 0) &&
                  (!conn->player_thread_please_stop) && (config.resyncthreshold > 0.0) &&
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
            // if there is no delay procedure, or it's not working or not allowed, there can be no
            // synchronising
            play_samples = stuff_buffer_basic_32((int32_t *)tbuf, inbuflength, config.output_format,
                                                 outbuf, 0, enable_dither, conn);
            if (outbuf == NULL)
              debug(1, "NULL outbuf to play -- skipping it.");
            else
              config.output->play((short *)outbuf, play_samples); // remove the (short*)!
          }

          // mark the frame as finished
          inframe->timestamp = 0;
          inframe->sequence_number = 0;

          // debug(1,"Sync error %lld frames. Amount to stuff %d." ,sync_error,amount_to_stuff);

          // new stats calculation. We want a running average of sync error, drift, adjustment,
          // number of additions+subtractions

          // this is a misleading hack -- the statistics should include some data on the number of
          // valid samples and the number of times sync wasn't checked due to non availability of a
          // delay figure.
          // for the present, stats are only updated when sync has been checked
          if (sync_error != -1) {
            if (number_of_statistics == trend_interval) {
              // here we remove the oldest statistical data and take it from the summaries as well
              tsum_of_sync_errors -= statistics[oldest_statistic].sync_error;
              tsum_of_drifts -= statistics[oldest_statistic].drift;
              if (statistics[oldest_statistic].correction > 0)
                tsum_of_insertions_and_deletions -= statistics[oldest_statistic].correction;
              else
                tsum_of_insertions_and_deletions += statistics[oldest_statistic].correction;
              tsum_of_corrections -= statistics[oldest_statistic].correction;
              oldest_statistic = (oldest_statistic + 1) % trend_interval;
              number_of_statistics--;
            }

            statistics[newest_statistic].sync_error = sync_error;
            statistics[newest_statistic].correction = conn->amountStuffed;

            if (number_of_statistics == 0)
              statistics[newest_statistic].drift = 0;
            else
              statistics[newest_statistic].drift =
                  sync_error - previous_sync_error - previous_correction;

            previous_sync_error = sync_error;
            previous_correction = conn->amountStuffed;

            tsum_of_sync_errors += sync_error;
            tsum_of_drifts += statistics[newest_statistic].drift;
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
                      " %*.1f," /* Sync error in milliseconds */
                      "%*.1f,"  /* net correction in ppm */
                      "%*.1f,"  /* corrections in ppm */
                      "%*d,"    /* total packets */
                      "%*llu,"  /* missing packets */
                      "%*llu,"  /* late packets */
                      "%*llu,"  /* too late packets */
                      "%*llu,"  /* resend requests */
                      "%*lli,"  /* min DAC queue size */
                      "%*d,"    /* min buffer occupancy */
                      "%*d",    /* max buffer occupancy */
                      9, /* should be 10, but there's an explicit space at the start to ensure
                            alignment */
                      1000 * moving_average_sync_error / config.output_rate,
                      10, moving_average_correction * 1000000 / (352 * conn->output_sample_ratio),
                      10, moving_average_insertions_plus_deletions * 1000000 /
                              (352 * conn->output_sample_ratio),
                      12, play_number, 7, conn->missing_packets, 7, conn->late_packets, 7,
                      conn->too_late_packets, 7, conn->resend_requests, 7, minimum_dac_queue_size,
                      5, minimum_buffer_occupancy, 5, maximum_buffer_occupancy);
                } else {
                  inform(" %*.1f," /* Sync error in milliseconds */
                         "%*d,"    /* total packets */
                         "%*llu,"  /* missing packets */
                         "%*llu,"  /* late packets */
                         "%*llu,"  /* too late packets */
                         "%*llu,"  /* resend requests */
                         "%*lli,"  /* min DAC queue size */
                         "%*d,"    /* min buffer occupancy */
                         "%*d",    /* max buffer occupancy */
                         9, /* should be 10, but there's an explicit space at the start to ensure
                               alignment */
                         1000 * moving_average_sync_error / config.output_rate,
                         12, play_number, 7, conn->missing_packets, 7, conn->late_packets, 7,
                         conn->too_late_packets, 7, conn->resend_requests, 7,
                         minimum_dac_queue_size, 5, minimum_buffer_occupancy, 5,
                         maximum_buffer_occupancy);
                }
              } else {
                inform(" %*.1f," /* Sync error in milliseconds */
                       "%*d,"    /* total packets */
                       "%*llu,"  /* missing packets */
                       "%*llu,"  /* late packets */
                       "%*llu,"  /* too late packets */
                       "%*llu,"  /* resend requests */
                       "%*d,"    /* min buffer occupancy */
                       "%*d",    /* max buffer occupancy */
                       9, /* should be 10, but there's an explicit space at the start to ensure
                             alignment */
                       1000 * moving_average_sync_error / config.output_rate,
                       12, play_number, 7, conn->missing_packets, 7, conn->late_packets, 7,
                       conn->too_late_packets, 7, conn->resend_requests, 5,
                       minimum_buffer_occupancy, 5, maximum_buffer_occupancy);
              }
            } else {
              inform("No frames received in the last sampling interval.");
            }
          }
          minimum_dac_queue_size = INT64_MAX;   // hack reset
          maximum_buffer_occupancy = INT32_MIN; // can't be less than this
          minimum_buffer_occupancy = INT32_MAX; // can't be more than this
          at_least_one_frame_seen = 0;
        }
      }
    }
  }

  if (config.statistics_requested) {
    int rawSeconds = (int)difftime(time(NULL), playstart);
    int elapsedHours = rawSeconds / 3600;
    int elapsedMin = (rawSeconds / 60) % 60;
    int elapsedSec = rawSeconds % 60;
    inform("Playback Stopped. Total playing time %02d:%02d:%02d.", elapsedHours, elapsedMin,
           elapsedSec);
  }

#ifndef HAVE_DACP_CLIENT
  // stop watching for DACP port number stuff
  // this is only used for compatability, if dacp stuff isn't enabled.
  mdns_dacp_dont_monitor(conn->dapo_private_storage);
#endif

  if (config.output->stop)
    config.output->stop();

  debug(2, "Shut down audio, control and timing threads");
  conn->please_stop = 1;
  pthread_kill(rtp_audio_thread, SIGUSR1);
  pthread_kill(rtp_control_thread, SIGUSR1);
  pthread_kill(rtp_timing_thread, SIGUSR1);
  pthread_join(rtp_timing_thread, NULL);
  debug(2, "timing thread joined");
  pthread_join(rtp_audio_thread, NULL);
  debug(2, "audio thread joined");
  pthread_join(rtp_control_thread, NULL);
  debug(2, "control thread joined");
  clear_reference_timestamp(conn);
  conn->rtp_running = 0;

  // usleep(100000); // allow this time to (?) allow the alsa subsystem to finish cleaning up after
  // itself. 50 ms seems too short

  free_audio_buffers(conn);
  terminate_decoders(conn);
  // remove flow control and mutexes
  rc = pthread_cond_destroy(&conn->flowcontrol);
  if (rc)
    debug(1, "Error destroying flowcontrol condition variable.");
  rc = pthread_mutex_destroy(&conn->flush_mutex);
  if (rc)
    debug(1, "Error destroying flush_mutex variable.");
  rc = pthread_mutex_destroy(&conn->ab_mutex);
  if (rc)
    debug(1, "Error destroying ab_mutex variable.");
  rc = pthread_mutex_destroy(&conn->vol_mutex);
  if (rc)
    debug(1, "Error destroying vol_mutex variable.");

  debug(2, "Player thread exit on RTSP conversation thread %d.", conn->connection_number);
  if (conn->dacp_id) {
    free(conn->dacp_id);
    conn->dacp_id = NULL;
  }
  if (outbuf)
    free(outbuf);
  if (silence)
    free(silence);
  if (tbuf)
    free(tbuf);
  if (sbuf)
    free(sbuf);
  return 0;
}

// takes the volume as specified by the airplay protocol
void player_volume_without_notification(double airplay_volume, rtsp_conn_info *conn) {

  // The volume ranges -144.0 (mute) or -30 -- 0. See
  // http://git.zx2c4.com/Airtunes2/about/#setting-volume
  // By examination, the -30 -- 0 range is linear on the slider; i.e. the slider is calibrated in 30
  // equal increments. Since the human ear's response is roughly logarithmic, we imagine these to
  // be power dB, i.e. from -30dB to 0dB.

  // We may have a hardware mixer, and if so, we will give it priority.
  // If a desired volume range is given, then we will try to accommodate it from
  // the top of the hardware mixer's range downwards.

  // If no desired volume range is given, we will use the native resolution of the hardware mixer,
  // if any,
  // or failing that, the software mixer. The software mixer has a range of from -96.3 dB up to 0
  // dB,
  // corresponding to a multiplier of 1 to 65535.

  // Otherwise, we will accommodate the desired volume range in the combination of the software and
  // hardware mixer
  // Intuitively (!), it seems best to give the hardware mixer as big a role as possible, so
  // we will use its full range and then accommodate the rest of the attenuation in software.
  // A problem is that we don't know whether the lowest hardware volume actually mutes the output
  // so we must assume that it does, and for this reason, the volume control goes at the "bottom" of
  // the adjustment range

  // The dB range of a value from 1 to 65536 is about 96.3 dB (log10 of 65536 is 4.8164).
  // Since the levels correspond with amplitude, they correspond to voltage, hence voltage dB,
  // or 20 times the log of the ratio. Then multiplied by 100 for convenience.
  // Thus, we ask our vol2attn function for an appropriate dB between -96.3 and 0 dB and translate
  // it back to a number.

  int32_t hw_min_db, hw_max_db, hw_range_db, min_db,
      max_db; // hw_range_db is a flag; if 0 means no mixer

  if (config.output->parameters) {
    audio_parameters audio_information;
    // have a hardware mixer
    config.output->parameters(&audio_information);
    hw_max_db = audio_information.maximum_volume_dB;
    hw_min_db = audio_information.minimum_volume_dB;
    hw_range_db = hw_max_db - hw_min_db;
  } else {
    // don't have a hardware mixer
    hw_max_db = hw_min_db = hw_range_db = 0;
  }

  int32_t sw_min_db = -9630;
  int32_t sw_max_db = 0;
  int32_t sw_range_db = sw_max_db - sw_min_db;
  int32_t desired_range_db = 0; // this is also used as a flag; if 0 means no desired range

  if (config.volume_range_db)
    desired_range_db = (int32_t)trunc(config.volume_range_db * 100);

  // This is wrong, I think -- it doesn't work properly if the volume range is composite and you
  // want to set a maximum value which should affect the hardware mixer.

  if (config.volume_max_db_set) {
    if (hw_range_db) {
      if (((config.volume_max_db * 100) < hw_max_db) &&
          ((config.volume_max_db * 100) > hw_min_db)) {
        hw_max_db = (int)config.volume_max_db * 100;
        hw_range_db = hw_max_db - hw_min_db;
      } else {
        inform("The volume_max_db setting is out of range of the hardware mixers's limits of %d dB "
               "to %d dB. It will be ignored.",
               (int)(hw_max_db / 100), (int)(hw_min_db / 100));
      }
    } else {
      if (((config.volume_max_db * 100) < sw_max_db) &&
          ((config.volume_max_db * 100) > sw_min_db)) {
        sw_max_db = (int)config.volume_max_db * 100;
        sw_range_db = sw_max_db - sw_min_db;
      } else {
        inform("The volume_max_db setting is out of range of the software attenuation's limits of "
               "0 dB to -96.3 dB. It will be ignored.");
      }
    }
  }

  if (desired_range_db) {
    // debug(1,"An attenuation range of %d is requested.",desired_range_db);
    // we have a desired volume range.
    if (hw_range_db) {
      // we have a hardware mixer
      if (hw_range_db >= desired_range_db) {
        // the hardware mixer can accommodate the desired range
        max_db = hw_max_db;
        min_db = max_db - desired_range_db;
      } else {
        // we have a hardware mixer and a desired range greater than the mixer's range.
        if ((hw_range_db + sw_range_db) < desired_range_db) {
          inform("The volume attenuation range %f is greater than can be accommodated by the "
                 "hardware and software -- set to %f.",
                 config.volume_range_db, hw_range_db + sw_range_db);
          desired_range_db = hw_range_db + sw_range_db;
        }
        min_db = hw_min_db;
        max_db = min_db + desired_range_db;
      }
    } else {
      // we have a desired volume range and no hardware mixer
      if (sw_range_db < desired_range_db) {
        inform("The volume attenuation range %f is greater than can be accommodated by the "
               "software -- set to %f.",
               config.volume_range_db, sw_range_db);
        desired_range_db = sw_range_db;
      }
      max_db = sw_max_db;
      min_db = max_db - desired_range_db;
    }
  } else {
    // we do not have a desired volume range, so use the mixer's volume range, if there is one.
    // debug(1,"No attenuation range requested.");
    if (hw_range_db) {
      min_db = hw_min_db;
      max_db = hw_max_db;
    } else {
      min_db = sw_min_db;
      max_db = sw_max_db;
    }
  }

  /*
    if (config.volume_max_db_set) {
      if ((config.volume_max_db*100<=max_db) && (config.volume_max_db*100>=min_db)) {
        debug(1,"Reducing the maximum volume from %d to %d.",max_db/100,config.volume_max_db);
        max_db = (int)(config.volume_max_db*100);
      } else {
        inform("The value of volume_max_db is invalid. It must be in the range %d to
    %d.",max_db,min_db);
      }
    }
  */
  double hardware_attenuation, software_attenuation = 0.0;
  double scaled_attenuation = hw_min_db + sw_min_db;

  // now, we can map the input to the desired output volume
  if ((airplay_volume == -144.0) && (config.ignore_volume_control == 0)) {
    // do a mute
    // needed even with hardware mute, as when sound is unmuted it might otherwise be very loud.
    hardware_attenuation = hw_min_db;
    if (config.output->mute) {
      // allow the audio material to reach the mixer, but mute the mixer
      // it the mute is removed externally, the material with be there
      software_attenuation = sw_max_db - (max_db - hw_max_db); // e.g. if the hw_max_db  is +4 and
                                                               // the max is +40, this will be -36
                                                               // (all by 100, of course)
      // debug(1,"Mute, with hardware mute and software_attenuation set to
      // %d.",software_attenuation);
      config.output->mute(1); // use real mute if it's there
    } else {
      if (config.output->volume == NULL) { // if there is also no hardware volume control
        software_attenuation = sw_min_db;  // set any software output to zero too
        // debug(1,"Mute, with no hardware mute and software_attenuation set to
        // %d.",software_attenuation);
      }
    }
  } else {
    if (config.output->mute)
      config.output->mute(0); // unmute mute if it's there
    if (config.ignore_volume_control == 1)
      scaled_attenuation = max_db;
    else if (config.volume_control_profile == VCP_standard)
      scaled_attenuation = vol2attn(airplay_volume, max_db, min_db);
    else if (config.volume_control_profile == VCP_flat)
      scaled_attenuation = flat_vol2attn(airplay_volume, max_db, min_db);
    else
      debug(1, "Unrecognised volume control profile");

    if (hw_range_db) {
      // if there is a hardware mixer
      if (scaled_attenuation <= hw_max_db) {
        // the attenuation is so low that's it's in the hardware mixer's range
        // debug(1,"Attenuation all taken care of by the hardware mixer.");
        hardware_attenuation = scaled_attenuation;
        software_attenuation = sw_max_db - (max_db - hw_max_db); // e.g. if the hw_max_db  is +4 and
                                                                 // the max is +40, this will be -36
                                                                 // (all by 100, of course)
      } else {
        // debug(1,"Attenuation taken care of by hardware and software mixer.");
        hardware_attenuation = hw_max_db; // the hardware mixer is turned up full
        software_attenuation = sw_max_db - (max_db - scaled_attenuation);
      }
    } else {
      // if there is no hardware mixer, the scaled_volume is the software volume
      // debug(1,"Attenuation all taken care of by the software mixer.");
      software_attenuation = scaled_attenuation;
    }
  }

  if ((config.output->volume) && (hw_range_db)) {
    config.output->volume(hardware_attenuation); // otherwise set the output to the lowest value
    // debug(1,"Hardware attenuation set to %f for airplay volume of
    // %f.",hardware_attenuation,airplay_volume);
  }

  if (config.ignore_volume_control == 0) {
    double temp_fix_volume = 65536.0 * pow(10, software_attenuation / 2000);
  // debug(1,"Software attenuation set to %f, i.e %f out of 65,536, for airplay volume of
  // %f",software_attenuation,temp_fix_volume,airplay_volume);

    pthread_mutex_lock(&conn->vol_mutex);
    conn->fix_volume = temp_fix_volume;
    pthread_mutex_unlock(&conn->vol_mutex);
  }

  if (config.loudness)
    loudness_set_volume(software_attenuation / 100);

  if (config.logOutputLevel) {
    inform("Output Level set to: %.2f dB.", scaled_attenuation / 100.0);
  }

#ifdef CONFIG_METADATA
  char *dv = malloc(128); // will be freed in the metadata thread
  if (dv) {
    memset(dv, 0, 128);
    if (config.ignore_volume_control == 1)
      snprintf(dv, 127, "%.2f,%.2f,%.2f,%.2f", airplay_volume, 0.0, 0.0, 0.0);
    else
      snprintf(dv, 127, "%.2f,%.2f,%.2f,%.2f", airplay_volume, scaled_attenuation / 100.0,
               min_db / 100.0, max_db / 100.0);
    send_ssnc_metadata('pvol', dv, strlen(dv), 1);
  }
#endif

  // here, store the volume for possible use in the future
  config.airplay_volume = airplay_volume;
}

void player_volume(double airplay_volume, rtsp_conn_info *conn) {
  command_set_volume(airplay_volume);
  player_volume_without_notification(airplay_volume, conn);
}

void player_flush(int64_t timestamp, rtsp_conn_info *conn) {
  debug(3, "Flush requested up to %u. It seems as if 0 is special.", timestamp);
  pthread_mutex_lock(&conn->flush_mutex);
  conn->flush_requested = 1;
  // if (timestamp!=0)
  conn->flush_rtp_timestamp = timestamp; // flush all packets up to (and including?) this
  pthread_mutex_unlock(&conn->flush_mutex);
  conn->play_segment_reference_frame = 0;
  conn->play_number_after_flush = 0;
#ifdef CONFIG_METADATA
  // only send a flush metadata message if the first packet has been seen -- it's a bogus message
  // otherwise
  if (conn->first_packet_timestamp) {
    debug(2, "pfls");
    send_ssnc_metadata('pfls', NULL, 0, 1);
  }
#endif
}

int player_play(rtsp_conn_info *conn) {
  // need to use conn in place of streram below. Need to put the stream as a parameter to he
  if (conn->player_thread != NULL)
    die("Trying to create a second player thread for this RTSP session");
  if (config.buffer_start_fill > BUFFER_FRAMES)
    die("specified buffer starting fill %d > buffer size %d", config.buffer_start_fill,
        BUFFER_FRAMES);
  command_start();
#ifdef CONFIG_METADATA
  debug(2, "pbeg");
  send_ssnc_metadata('pbeg', NULL, 0, 1);
#endif
  pthread_t *pt = malloc(sizeof(pthread_t));
  if (pt == NULL)
    die("Couldn't allocate space for pthread_t");
  conn->player_thread = pt;
  size_t size = (PTHREAD_STACK_MIN + 256 * 1024);
  pthread_attr_t tattr;
  pthread_attr_init(&tattr);
  int rc = pthread_attr_setstacksize(&tattr, size);
  if (rc)
    debug(1, "Error setting stack size for player_thread: %s", strerror(errno));
  pthread_create(pt, &tattr, player_thread_func, (void *)conn);
  pthread_attr_destroy(&tattr);
  return 0;
}

void player_stop(rtsp_conn_info *conn) {
  if (conn->player_thread) {
    conn->player_thread_please_stop = 1;
    pthread_cond_signal(&conn->flowcontrol); // tell it to give up
    pthread_kill(*conn->player_thread, SIGUSR1);
    pthread_join(*conn->player_thread, NULL);
#ifdef CONFIG_METADATA
    debug(2, "pend");
    send_ssnc_metadata('pend', NULL, 0, 1);
#endif
    command_stop();
    free(conn->player_thread);
    conn->player_thread = NULL;
  } else {
    debug(3, "player thread of RTSP conversation %d is already deleted.", conn->connection_number);
  }
}
