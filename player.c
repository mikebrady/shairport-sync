/*
 * Slave-clocked ALAC stream player. This file is part of Shairport.
 * Copyright (c) James Laird 2011, 2013
 * All rights reserved.
 *
 * Modifications for audio synchronisation, AirPlay 2
 * and related work, copyright (c) Mike Brady 2014--2025
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
#include <stdarg.h>
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
#endif

#ifdef CONFIG_POLARSSL
#include <polarssl/aes.h>
#include <polarssl/havege.h>
#endif

#ifdef CONFIG_OPENSSL
#include <openssl/aes.h> // needed for older AES stuff
#include <openssl/bio.h> // needed for BIO_new_mem_buf
#include <openssl/err.h> // needed for ERR_error_string, ERR_get_error
#include <openssl/evp.h> // needed for EVP_PKEY_CTX_new, EVP_PKEY_sign_init, EVP_PKEY_sign
#include <openssl/pem.h> // needed for PEM_read_bio_RSAPrivateKey, EVP_PKEY_CTX_set_rsa_padding
#include <openssl/rsa.h> // needed for EVP_PKEY_CTX_set_rsa_padding
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

#ifdef CONFIG_AIRPLAY_2
#include "ptp-utilities.h"
#endif

#ifdef CONFIG_FFMPEG
#include <libavutil/version.h>
#endif

#include "loudness.h"

#include "activity_monitor.h"

const unsigned int silent_channel_index = 65;
const unsigned int front_mono_channel_index = 66;

// default buffer size
// needs to be a power of 2 because of the way BUFIDX(seqno) works
// #define BUFFER_FRAMES 512

// DAC buffer occupancy stuff
#define DAC_BUFFER_QUEUE_MINIMUM_LENGTH 2500

// static abuf_t audio_buffer[BUFFER_FRAMES];
#define BUFIDX(seqno) ((seq_t)(seqno) % BUFFER_FRAMES)

void do_flush(uint32_t timestamp, rtsp_conn_info *conn);

#ifdef CONFIG_FFMPEG
size_t avflush(rtsp_conn_info *conn);
#endif

int free_audio_buffer_payload(abuf_t *abuf) {
  int items_freed = 0;
  if (abuf) {
    if (abuf->data != NULL) {
      free(abuf->data);
      items_freed++;
      abuf->data = NULL;
    }
#ifdef CONFIG_FFMPEG
    if (abuf->avframe != NULL) {
      av_frame_free(&abuf->avframe);
      items_freed++;
      abuf->avframe = NULL;
      abuf->ssrc = SSRC_NONE;
    }
#endif
  } else {
    debug(1, "null buffer pointer!");
  }
  return items_freed;
}

void ab_resync(rtsp_conn_info *conn) {
  int i;
  for (i = 0; i < BUFFER_FRAMES; i++) {
    free_audio_buffer_payload(&conn->audio_buffer[i]);
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
  conn->last_seqno_valid = 0;
  conn->ab_buffering = 1;
}

void reset_input_flow_metrics(rtsp_conn_info *conn) {
  conn->play_number_after_flush = 0;
  conn->packet_count_since_flush = 0;
  conn->input_frame_rate_starting_point_is_valid = 0;
  conn->initial_reference_time = 0;
  conn->initial_reference_timestamp = 0;
}

void unencrypted_packet_decode(rtsp_conn_info *conn, unsigned char *packet, int length,
                               short *dest) {
  if (conn->stream.type == ast_apple_lossless) {
#ifdef CONFIG_APPLE_ALAC
    if (config.decoder_in_use == 1 << decoder_apple_alac) {
      int frames_decoded;
      apple_alac_decode_frame(packet, length, (unsigned char *)dest, &frames_decoded);
    } else
#endif
#ifdef CONFIG_HAMMERTON
        if (config.decoder_in_use == 1 << decoder_hammerton) {
      int buffer_size = conn->frames_per_packet * conn->input_bytes_per_frame;
      alac_decode_frame(conn->decoder_info, packet, (unsigned char *)dest, &buffer_size);
    } else
#endif
    {
      die("No ALAC decoder included!");
    }
  } else if (conn->stream.type == ast_uncompressed) {
    int i;
    short *source = (short *)packet;
    for (i = 0; i < length / 2; i++) {
      // assuming each input sample is 16 bits.
      *dest = ntohs(*source);
      dest++;
      source++;
    }
  }
}

#ifdef CONFIG_HAMMERTON
static int init_alac_decoder(int32_t fmtp[12], rtsp_conn_info *conn) {

  // clang-format off

  // This is a guess, but the format of the fmtp looks identical to the format of an
  // ALACSpecificCOnfig which is detailed in the file ALACMagicCookieDescription.txt
  // in the Apple ALAC sample implementation
  // Here it is:

  /*

    * ALAC Specific Info (24 bytes) (mandatory)
    __________________________________________________________________________________________________________________________________

    The Apple Lossless codec stores specific information about the encoded stream in the ALACSpecificConfig. This
    info is vended by the encoder and is used to setup the decoder for a given encoded bitstream.

    When read from and written to a file, the fields of this struct must be in big-endian order.
    When vended by the encoder (and received by the decoder) the struct values will be in big-endian order.


        struct      ALACSpecificConfig (defined in ALACAudioTypes.h)
        abstract    This struct is used to describe codec provided information about the encoded Apple Lossless bitstream.
                    It must accompany the encoded stream in the containing audio file and be provided to the decoder.

        field       frameLength             uint32_t        indicating the frames per packet when no explicit frames per packet setting is
                                                            present in the packet header. The encoder frames per packet can be explicitly set
                                                            but for maximum compatibility, the default encoder setting of 4096 should be used.

        field       compatibleVersion       uint8_t         indicating compatible version,
                                                            value must be set to 0

        field       bitDepth                uint8_t         describes the bit depth of the source PCM data (maximum value = 32)

        field       pb                      uint8_t         currently unused tuning parameter.
                                                            value should be set to 40

        field       mb                      uint8_t         currently unused tuning parameter.
                                                            value should be set to 10

        field       kb                      uint8_t         currently unused tuning parameter.
                                                            value should be set to 14

        field       numChannels             uint8_t         describes the channel count (1 = mono, 2 = stereo, etc...)
                                                            when channel layout info is not provided in the 'magic cookie', a channel count > 2
                                                            describes a set of discreet channels with no specific ordering

        field       maxRun                  uint16_t        currently unused.
                                                            value should be set to 255

        field       maxFrameBytes           uint32_t        the maximum size of an Apple Lossless packet within the encoded stream.
                                                            value of 0 indicates unknown

        field       avgBitRate              uint32_t        the average bit rate in bits per second of the Apple Lossless stream.
                                                            value of 0 indicates unknown

        field       sampleRate              uint32_t        sample rate of the encoded stream


    typedef struct ALACSpecificConfig
    {
            uint32_t        frameLength;
            uint8_t         compatibleVersion;
            uint8_t         bitDepth;
            uint8_t         pb;
            uint8_t         mb;
            uint8_t         kb;
            uint8_t         numChannels;
            uint16_t        maxRun;
            uint32_t        maxFrameBytes;
            uint32_t        avgBitRate;
            uint32_t        sampleRate;

    } ALACSpecificConfig;

   */

   // We are going to go on that basis

  // clang-format on

  alac_file *alac;

  alac = alac_create(conn->input_bit_depth,
                     conn->input_num_channels); // no pthread cancellation point in here
  if (!alac)
    return 1;
  conn->decoder_info = alac;

  alac->setinfo_max_samples_per_frame = conn->frames_per_packet;
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
  return 0;
}
#endif

static void init_buffer(rtsp_conn_info *conn) {
  int i;
  for (i = 0; i < BUFFER_FRAMES; i++) {
    conn->audio_buffer[i].data = NULL;
#ifdef CONFIG_FFMPEG
    conn->audio_buffer[i].avframe = NULL;
    conn->audio_buffer[i].ssrc = SSRC_NONE;
#endif
  }
}

static void free_audio_buffers(rtsp_conn_info *conn) {
  int i;
  for (i = 0; i < BUFFER_FRAMES; i++) {
    free_audio_buffer_payload(&conn->audio_buffer[i]);
  }
}

int first_possibly_missing_frame = -1;

void reset_buffer(rtsp_conn_info *conn) {
  pthread_cleanup_debug_mutex_lock(&conn->ab_mutex, 30000, 0);
  ab_resync(conn);
  pthread_cleanup_pop(1);
#if CONFIG_FFMPEG
  avflush(conn);
#endif
  if (config.output->flush) {
    config.output->flush(); // no cancellation points
                            //            debug(1, "reset_buffer: flush output device.");
  }
}

// returns the total number of blocks and the number occupied, but not their size,
// because the size is determined by the block size sent

size_t get_audio_buffer_occupancy(rtsp_conn_info *conn) {
  size_t response = 0;
  pthread_cleanup_debug_mutex_lock(&conn->ab_mutex, 30000, 0);
  if (conn->ab_synced) {
    int16_t occ =
        conn->ab_write - conn->ab_read; // will be zero or positive if read and write are within
                                        // 2^15 of each other and write is at or after read
    response = occ;
  }
  pthread_cleanup_pop(1);
  return response;
}

const char *get_category_string(airplay_stream_c cat) {
  char *category;
  switch (cat) {
  case unspecified_stream_category:
    category = "unspecified stream";
    break;
  case ptp_stream:
    category = "PTP stream";
    break;
  case ntp_stream:
    category = "NTP stream";
    break;
  case remote_control_stream:
    category = "Remote Control stream";
    break;
  case classic_airplay_stream:
    category = "Classic AirPlay stream";
    break;
  default:
    category = "Unexpected stream code";
    break;
  }
  return category;
}

#ifdef CONFIG_FFMPEG

static void avcodec_alloc_context3_cleanup_handler(void *arg) {
  debug(3, "avcodec_alloc_context3_cleanup_handler");
  AVCodecContext *codec_context = arg;
  av_free(codec_context);
}

static void avcodec_open2_cleanup_handler(__attribute__((unused)) void *arg) {
  debug(3, "avcodec_open2_cleanup_handler");
  // AVCodecContext *codec_context = arg;
  // avcodec_close(codec_context);
}

static void swr_alloc_cleanup_handler(void *arg) {
  debug(3, "swr_alloc_cleanup_handler");
  SwrContext **swr = arg;
  swr_free(swr);
}

static void av_packet_alloc_cleanup_handler(void *arg) {
  debug(3, "av_packet_alloc_cleanup_handler");
  AVPacket **pkt = arg;
  av_packet_free(pkt);
}

/*
static void av_frame_alloc_cleanup_handler(void *arg) {
  debug(3, "av_frame_alloc_cleanup_handler");
  AVFrame **frame = arg;
  av_frame_free(frame);
}
*/

void clear_decoding_chain(rtsp_conn_info *conn) {
  if (conn->incoming_ssrc != 0) {
    //    debug_mutex_lock(&conn->ab_mutex, 30000, 0);
    //    ab_resync(conn);
    //    debug_mutex_unlock(&conn->ab_mutex, 0);
    pthread_cleanup_push(avcodec_alloc_context3_cleanup_handler, conn->codec_context);
    pthread_cleanup_push(malloc_cleanup, &conn->codec_context->extradata);
    pthread_cleanup_push(avcodec_open2_cleanup_handler, conn->codec_context);
    pthread_cleanup_pop(1); // avcodec_open2_cleanup_handler
    pthread_cleanup_pop(1); // deallocate the malloc
    pthread_cleanup_pop(1); // avcodec_alloc_context3_cleanup_handler
    conn->incoming_ssrc = SSRC_NONE;
  }
}

void clear_software_resampler(rtsp_conn_info *conn) {
  if (conn->swr != NULL) {
    debug(2, "clear_software_resampler");
    pthread_cleanup_push(swr_alloc_cleanup_handler, &conn->swr);
    pthread_cleanup_pop(1); // deallocate the swr
    conn->swr = NULL;
    conn->resampler_ssrc = SSRC_NONE;
  }
}

int ssrc_is_recognised(ssrc_t ssrc) {
  int response = 0;
  switch (ssrc) {
  case ALAC_44100_S16_2:
  case ALAC_48000_S24_2:
  case AAC_44100_F24_2:
  case AAC_48000_F24_2:
  case AAC_48000_F24_5P1:
  case AAC_48000_F24_7P1:
    response = 1;
    break;
  default:
    break;
  }
  return response;
}

int ssrc_is_aac(ssrc_t ssrc) {
  int response = 0;
  switch (ssrc) {
  case AAC_44100_F24_2:
  case AAC_48000_F24_2:
  case AAC_48000_F24_5P1:
  case AAC_48000_F24_7P1:
    response = 1;
    break;
  default:
    break;
  }
  return response;
}

char ssrc_name[1024];
const char *get_ssrc_name(ssrc_t ssrc) {
  const char *response = NULL;
  switch (ssrc) {
  case ALAC_44100_S16_2:
    response = "ALAC/44100/S16_LE/2";
    break;
  case ALAC_48000_S24_2:
    response = "ALAC/48000/S24_LE/2";
    break;
  case AAC_44100_F24_2:
    response = "AAC/44100/F24/2";
    break;
  case AAC_48000_F24_2:
    response = "AAC/48000/F24/2";
    break;
  case AAC_48000_F24_5P1:
    response = "AAC/48000/F24/5.1";
    break;
  case AAC_48000_F24_7P1:
    response = "AAC/48000/F24/7.1";
    break;
  case SSRC_NONE:
    response = "None (0)";
    break;
  default: {
    snprintf(ssrc_name, sizeof(ssrc_name), "<unknown ssrc> (0x%" PRIx32 ")", ssrc);
    response = ssrc_name;
  } break;
  }
  return response;
}

uint32_t get_ssrc_rate(ssrc_t ssrc) {
  uint32_t response = 0;
  switch (ssrc) {
  case ALAC_44100_S16_2:
  case AAC_44100_F24_2:
    response = 44100;
    break;
  case ALAC_48000_S24_2:
  case AAC_48000_F24_2:
  case AAC_48000_F24_5P1:
  case AAC_48000_F24_7P1:
    response = 48000;
    break;
  default:
    break;
  }
  return response;
}

size_t get_ssrc_block_length(ssrc_t ssrc) {
  size_t response = 0;
  switch (ssrc) {
  case ALAC_44100_S16_2:
  case ALAC_48000_S24_2:
    response = 352;
    break;
  case AAC_44100_F24_2:
  case AAC_48000_F24_2:
  case AAC_48000_F24_5P1:
  case AAC_48000_F24_7P1:
    response = 1024;
    break;
  default:
    break;
  }
  return response;
}

int setup_software_resampler(rtsp_conn_info *conn, ssrc_t ssrc) {
  int response = 0;

  unsigned int channels;

  // the output from the software resampler will be the input to the rest of
  // the player chain, so we need to set those parameters according to the SSRC:

  // default values...
  conn->input_bit_depth = 16;
  conn->input_effective_bit_depth = 16;
  conn->input_bytes_per_frame = 4;
  conn->frames_per_packet = 352;

  // most common values first, changed in the switch statement
  conn->input_rate = 48000;
  channels = 2;
  conn->frames_per_packet = 1024;

  sps_format_t suggested_output_format = SPS_FORMAT_S32; // this may be ignored

  switch (ssrc) {
  case ALAC_44100_S16_2:
    conn->input_rate = 44100;
    conn->frames_per_packet = 352;
    suggested_output_format = SPS_FORMAT_S16;
    break;
  case ALAC_48000_S24_2:
    conn->frames_per_packet = 352;
    suggested_output_format = SPS_FORMAT_S24;
    break;
  case AAC_44100_F24_2:
    conn->input_rate = 44100;
    break;
  case AAC_48000_F24_2:
    break;
  case AAC_48000_F24_5P1:
    channels = 6;
    break;
  case AAC_48000_F24_7P1:
    channels = 8;
    break;
  default:
    debug(1, "Can't set rate for %s.", get_ssrc_name(ssrc));
    break;
  }

// Now we ask the backend for its best format, giving it the channels, rate and format

// default format is S32_LE/48000/2 for AP2, S16_LE/44100/2 otherwise
#ifdef CONFIG_AIRPLAY_2
  uint32_t output_configuration = CHANNELS_TO_ENCODED_FORMAT(2) | RATE_TO_ENCODED_FORMAT(48000) |
                                  FORMAT_TO_ENCODED_FORMAT(SPS_FORMAT_S32_LE);
#else
  uint32_t output_configuration = CHANNELS_TO_ENCODED_FORMAT(2) | RATE_TO_ENCODED_FORMAT(44100) |
                                  FORMAT_TO_ENCODED_FORMAT(SPS_FORMAT_S16_LE);
#endif

  int output_configuration_changed = 0;

  if (config.output->get_configuration) {
    output_configuration =
        config.output->get_configuration(channels, conn->input_rate, suggested_output_format);
  }

  // if you can set up a configuration...
  if (output_configuration != 0) {
    if (config.current_output_configuration != output_configuration) {
      output_configuration_changed = 1;
      debug(2, "Connection %d: outgoing audio switching to: %s.", conn->connection_number,
            short_format_description(output_configuration));
    }
    config.current_output_configuration = output_configuration;
    char *output_device_channel_map = NULL;
    if (config.output->configure) {
      config.output->configure(output_configuration, &output_device_channel_map);
    }

    // create a software resampler
    if (conn->swr != NULL) {
      debug(3, "software resampler already set up");
      if (swr_is_initialized(conn->swr)) {
        debug(3, "software resampler already initialised -- close it...");
        swr_close(conn->swr);
      }
      debug(3, "software resampler free it...");
      swr_free(&conn->swr);
      if (conn->swr == NULL) {
        debug(3, "software resampler released");
      }
    }

    // input channels to the player
    conn->input_num_channels = CHANNELS_FROM_ENCODED_FORMAT(output_configuration);

    SwrContext *swr = swr_alloc();
    conn->swr = swr;
    if (swr == NULL) {
      die("can not allocate an swr context");
    }

    // push a deallocator -- av_packet_free(pkt);
    pthread_cleanup_push(swr_alloc_cleanup_handler, &conn->swr);

    enum AVSampleFormat input_format = AV_SAMPLE_FMT_FLTP; // default
    int64_t input_layout = AV_CH_LAYOUT_STEREO;            // default
    int64_t output_layout = AV_CH_LAYOUT_STEREO;           // default

    switch (ssrc) {
    case ALAC_44100_S16_2:
    case ALAC_48000_S24_2: {
      // seems as if the codec_context is correctly set up for ALAC but not for AAC-LC
      input_format = conn->codec_context->sample_fmt;
    } break;
    case AAC_44100_F24_2:
    case AAC_48000_F24_2: {
      // defaults are fine...
    } break;
    case AAC_48000_F24_5P1: {
      input_layout = config.six_channel_layout;
      output_layout = config.six_channel_layout; // assume no mixdown
    } break;
    case AAC_48000_F24_7P1: {
      input_layout = config.eight_channel_layout;
      output_layout = config.eight_channel_layout; // assume no mixdown
    } break;
    default:
      debug(1, "unexpected SSRC: 0x%0x", ssrc);
      break;
    }

    av_opt_set_sample_fmt(swr, "in_sample_fmt", input_format, 0);

    // remember that if mixdown is enabled,
    // set the resampler's channel layout either automatically or use the
    // setting that has been given

#if LIBAVUTIL_VERSION_MAJOR >= 57
    {
      AVChannelLayout input_channel_layout;
      av_channel_layout_from_mask(&input_channel_layout, input_layout);
      av_opt_set_chlayout(swr, "in_chlayout", &input_channel_layout, 0);
      av_channel_layout_uninit(&input_channel_layout);

      AVChannelLayout output_channel_layout;
      if (config.mixdown_enable != 0) {
        if (config.mixdown_channel_layout == 0) {
          av_channel_layout_default(&output_channel_layout,
                                    CHANNELS_FROM_ENCODED_FORMAT(output_configuration));
        } else {
          av_channel_layout_from_mask(&output_channel_layout, config.mixdown_channel_layout);
        }
      } else {
        av_channel_layout_from_mask(&output_channel_layout, output_layout);
      }
      av_opt_set_chlayout(swr, "out_chlayout", &output_channel_layout, 0);
      av_channel_layout_uninit(&output_channel_layout);
    }
#else
    av_opt_set_int(swr, "in_channel_layout", input_layout, 0);
    if (config.mixdown_enable != 0) {
      if (config.mixdown_channel_layout == 0) {
        output_layout =
            av_get_default_channel_layout(CHANNELS_FROM_ENCODED_FORMAT(output_configuration));
      } else {
        output_layout = config.mixdown_channel_layout;
      }
    }
    av_opt_set_int(swr, "out_channel_layout", output_layout, 0); // assume no mixdown
#endif

    av_opt_set_int(swr, "in_sample_rate", conn->input_rate, 0);
    // now set the resampler's output rate to match the output device's rate
    av_opt_set_int(swr, "out_sample_rate", RATE_FROM_ENCODED_FORMAT(output_configuration), 0);

    // Ask for S16 output for AAC/S16 input and for S32 output from resampler for F24 and S24.
    // This is to avoid FFmpeg unnecessarily transcoding S16 to S32.
    // Dither will be added by Shairport Sync itself later, if needed.

    if (ssrc == ALAC_44100_S16_2) {
      av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
      conn->input_bytes_per_frame =
          2 * CHANNELS_FROM_ENCODED_FORMAT(
                  output_configuration); // the output from the decoder will be input to the player
      conn->input_bit_depth = 16;
      conn->input_effective_bit_depth = 16;
    } else {
      av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_S32, 0);
      conn->input_bytes_per_frame =
          4 * CHANNELS_FROM_ENCODED_FORMAT(
                  output_configuration); // the output from the decoder will be input to the player
      conn->input_bit_depth = 32;
      // this is important when it comes to deciding on dither
      // AFAIK 24-bit ALAC comes out in 32-bit format but is actually 24 bit
      // so don't dither if it is truncated from 32 to 24 bit
      if (ssrc == ALAC_48000_S24_2)
        conn->input_effective_bit_depth = 24;
      else
        conn->input_effective_bit_depth = 32;
    }

    // now, having set up the resampler, we can initialise it

    // disabling this, as the soxr-based resampler seems not to give exactly the right number of
    // frames going from 44100 to 48000 and requires stuffing to compensate.

    // also, the soxr resampling engine isn't included in the Docker image.
    // #ifdef CONFIG_SOXR
    //      av_opt_set(swr, "resampler", "soxr", 0);
    // #endif
    int sres = swr_init(swr);
    if (sres != 0)
      debug(1, "swr_init returned %d with SSRC of 0x%0x and LIBAVUTIL_VERSION_MAJOR of %u.", sres,
            ssrc, LIBAVUTIL_VERSION_MAJOR);

    typedef struct {
      char *name;
      int allocated;
    } channel_info_t;

    char resampler_channel_list[1024] = "";
    unsigned int c;
    channel_info_t resampler_channels[64]; // can't be more than 64. This will list the channel
                                           // names in the order they appear in the output from
                                           // the software resampler.
    for (c = 0; c < sizeof(resampler_channels) / sizeof(channel_info_t); c++) {
      resampler_channels[c].name = NULL;
      resampler_channels[c].allocated = 0;
    }

    // get information about the output from the resampler
    int64_t resampler_output_format = 0;
    int resampler_channels_found = 0;

#if LIBAVUTIL_VERSION_MAJOR >= 57

    AVChannelLayout output_channel_layout = {0};
    av_opt_get_chlayout(swr, "out_chlayout", 0, &output_channel_layout);
    conn->resampler_output_channels = output_channel_layout.nb_channels;
    for (c = 0; c < 64; c++) {
      enum AVChannel channel = av_channel_layout_channel_from_index(&output_channel_layout, c);
      if (channel != AV_CHAN_NONE) {
        char buffer[32];
        if (av_channel_name(buffer, 32, channel) > 0) {
          if (resampler_channels_found == 0) {
            strcat(resampler_channel_list, "\"");
          } else {
            strcat(resampler_channel_list, "\", \"");
          }
          strcat(resampler_channel_list, buffer);
          resampler_channels[resampler_channels_found].name = strdup(buffer);
          resampler_channels_found++;
        }
      }
    }
    av_channel_layout_uninit(&output_channel_layout);

#else

    int64_t resampler_output_channel_layout = 0;
    {
      int res = av_opt_get_int(swr, "out_channel_layout", 0, &resampler_output_channel_layout);
      if (res == 0) {
        conn->resampler_output_channels =
            (int64_t)av_get_channel_layout_nb_channels((uint64_t)resampler_output_channel_layout);
      } else {
        debug(1, "Error %d getting resampler output channel layout.", res);
      }
    }
    int64_t mask = 1;
    for (c = 0; c < 64; c++) {
      if ((resampler_output_channel_layout & mask) != 0) {
        if (resampler_channels_found == 0) {
          strcat(resampler_channel_list, "\"");
        } else {
          strcat(resampler_channel_list, "\", \"");
        }
        strcat(resampler_channel_list, av_get_channel_name(1 << c));
        resampler_channels[resampler_channels_found].name = strdup(av_get_channel_name(1 << c));
        resampler_channels_found++;
      }
      mask = mask << 1;
    }

#endif

    if (resampler_channels_found != 0) {
      strcat(resampler_channel_list, "\"");
    }

    if (strlen(resampler_channel_list) == 0) {
      debug(3, "resampler output channel list is empty.");
    } else {
      debug(3, "resampler output channel list: %s.", resampler_channel_list);
    }

    if (output_device_channel_map != NULL) {
      debug(3, "output device's channel map is: \"%s\".", output_device_channel_map);
      // free(output_device_channel_map);
      // output_device_channel_map = NULL;
    }
    int output_channel_map_faulty = 0;
    if (resampler_channels_found != 0) {
      // now we have the names of the channels produced by the resampler in the order they
      // appear in the output from the resampler. We need to map them to the channel ordering
      // of the output device.

      // create an output channel list. It will be 64 channels long.
      // It may not have names for all channels.
      // In fact, it will have no names at all if mapping is disabled or set to auto
      // with no device channel map. That will be okay, as unallocated resampler
      // channels will be assigned to unused output channels at the end anyway

      channel_info_t
          output_channels[64]; // can't be more than 64. This will list the output channel names
                               // in the order they appear in the device channel map.
      for (c = 0; c < sizeof(output_channels) / sizeof(channel_info_t); c++) {
        output_channels[c].name = NULL;
        output_channels[c].allocated = 0;
      }

      // if channel mapping is enabled
      if (config.output_channel_mapping_enable != 0) {
        // if a channel map is given
        if (config.output_channel_map_size != 0) {
          for (c = 0; c < config.output_channel_map_size; c++) {
            output_channels[c].name = strdup(config.output_channel_map[c]);
          }
        } else if (output_device_channel_map != NULL) { // if there is a device channel map...
          char *device_channels = strdup(output_device_channel_map);
          char delim[] = " ";
          char *ptr = strtok(device_channels, delim);
          c = 0;
          while (ptr != NULL) {
            output_channels[c].name = strdup(ptr);
            if (strcasecmp(ptr, "UNKNOWN") == 0)
              output_channel_map_faulty = 1;
            ptr = strtok(NULL, delim);
            c++;
          }
          free(device_channels);
        }
      }

      if (output_channel_map_faulty != 0) {
        once(inform("The output device's %u-channel map is incomplete or faulty: \"%s\".",
                    CHANNELS_FROM_ENCODED_FORMAT(config.current_output_configuration),
                    output_device_channel_map));
      }

      // at this point, we should have two arrays
      // the first is all the resampler channels
      // the second is device channel map channels, which may be empty or incomplete

      for (c = 0; c < 64; c++)
        if (resampler_channels[c].name != NULL)
          debug(3, "audio channel %u is \"%s\".", c, resampler_channels[c].name);
      for (c = 0; c < 64; c++)
        if (output_channels[c].name != NULL)
          debug(3, "output device channel %u is \"%s\".", c, output_channels[c].name);

      conn->output_channel_map_size =
          CHANNELS_FROM_ENCODED_FORMAT(config.current_output_configuration);

      // construct a map to match named resampler channels to named output channels

      unsigned int cmi;
      for (cmi = 0; cmi < conn->output_channel_map_size; cmi++) {
        // debug(1,"checking output channel %u, (\"%s\").", cmi, output_channels[cmi].name);
        conn->output_channel_to_resampler_channel_map[cmi] =
            silent_channel_index; // by default the channel is silent
        if ((output_channels[cmi].name != NULL) && (strcmp(output_channels[cmi].name, "--") == 0)) {
          conn->output_channel_to_resampler_channel_map[cmi] = silent_channel_index;
          output_channels[cmi].allocated = 1;
          debug(1, "output device channel %u (\"--\") will be silent.", cmi);
        } else {
          int resampler_channel_index;
          int found = 0;
          for (resampler_channel_index = 0;
               ((resampler_channel_index < resampler_channels_found) && (found == 0));
               resampler_channel_index++) {
            if ((output_channels[cmi].name != NULL) &&
                (resampler_channels[resampler_channel_index].name != NULL) &&
                (strcmp(output_channels[cmi].name,
                        resampler_channels[resampler_channel_index].name) == 0)) {
              conn->output_channel_to_resampler_channel_map[cmi] = resampler_channel_index;
              output_channels[cmi].allocated = 1;
              resampler_channels[resampler_channel_index].allocated = 1;
              found = 1;
              if ((resampler_channels_found > 2) && (output_configuration_changed != 0)) {
                if (output_channel_map_faulty != 0)
                  debug(3, "%s -> %s/%u.", resampler_channels[resampler_channel_index].name,
                        output_channels[cmi].name, cmi);
                else
                  debug(3, "%s -> %s/%u.", resampler_channels[resampler_channel_index].name,
                        output_channels[cmi].name, cmi);
              }
            }
          }
        }
      }

      // now there may be unmapped resampler channels and unallocated output channels
      // allocate them on a first-come-first-served basis

      for (cmi = 0; cmi < conn->output_channel_map_size; cmi++) {
        if (output_channels[cmi].allocated == 0)
          debug(3, "output device channel %u (\"%s\") is unallocated.", cmi,
                output_channels[cmi].name);
      }
      for (c = 0; c < 64; c++) {
        if ((resampler_channels[c].name != NULL) && (resampler_channels[c].allocated == 0))
          debug(3, "audio channel %u (\"%s\") is unmapped.", c, resampler_channels[c].name);
      }

      c = 0; // for indexing through the unmapped resampler channels
      for (cmi = 0; (cmi < conn->output_channel_map_size) && (c < 64); cmi++) {
        if (output_channels[cmi].allocated == 0) {
          do {
            if ((resampler_channels[c].name != NULL) && (resampler_channels[c].allocated == 0)) {
              output_channels[cmi].allocated = 1;
              resampler_channels[c].allocated = 1;
              conn->output_channel_to_resampler_channel_map[cmi] = c;
              if (output_channel_map_faulty != 0)
                debug(3, "%s -> %s/%u.", resampler_channels[c].name, output_channels[cmi].name,
                      cmi);
              else
                debug(3, "%s -> %s/%u.", resampler_channels[c].name, output_channels[cmi].name,
                      cmi);
            } else {
              c++;
            }
          } while ((output_channels[cmi].allocated == 0) && (c < 64));
        }
      }

      if (output_configuration_changed != 0) {
        char channel_mapping_list[256] = "";
        for (c = 0; c < 8; c++) {
          if ((output_channels[c].allocated != 0) &&
              (conn->output_channel_to_resampler_channel_map[c] != silent_channel_index)) {
            char channel_mapping[32] = "";
            if (output_channels[c].name != NULL)
              snprintf(channel_mapping, sizeof(channel_mapping) - 1, " %u (\"%s\") <- %s |", c,
                       output_channels[c].name,
                       resampler_channels[conn->output_channel_to_resampler_channel_map[c]].name);
            else
              snprintf(channel_mapping, sizeof(channel_mapping) - 1, " %u <- %s |", c,
                       resampler_channels[conn->output_channel_to_resampler_channel_map[c]].name);
            strncat(channel_mapping_list, channel_mapping,
                    sizeof(channel_mapping_list) - 1 - strlen(channel_mapping_list));
          }
        }
        debug(1, "Channel Mapping: |%s", channel_mapping_list);
      }

      for (c = 0; c < 64; c++) {
        if (output_channels[c].name != NULL)
          free(output_channels[c].name);
      }
      for (c = 0; c < 64; c++) {
        if (resampler_channels[c].name != NULL)
          free(resampler_channels[c].name);
      }
    }

    {
      int res = av_opt_get_int(swr, "out_sample_fmt", 0, &resampler_output_format);
      if (res == 0) {
        conn->resampler_output_bytes_per_sample = av_get_bytes_per_sample(resampler_output_format);
        debug(3, "resampler output bytes per sample in swr: %d.",
              conn->resampler_output_bytes_per_sample);
      } else {
        debug(1, "Error %d getting resampler output bytes per sample.", res);
      }
    }
    conn->resampler_ssrc = ssrc;

    pthread_cleanup_pop(0); // successful exit -- don't deallocate the swr
  } else {
    debug(1, "Error setting the configuration of the output backend.");
  }
  return response; // 0 if everything is okay
}
void prepare_decoding_chain(rtsp_conn_info *conn, ssrc_t ssrc) {
  if ((ssrc_is_recognised(ssrc)) && (ssrc != conn->incoming_ssrc)) {

    if ((config.statistics_requested != 0) && (ssrc != SSRC_NONE) &&
        (conn->incoming_ssrc != SSRC_NONE)) {
      debug(2, "Connection %d: incoming audio switching to \"%s\".", conn->connection_number,
            get_ssrc_name(ssrc));
#ifdef CONFIG_METADATA
      send_ssnc_metadata('sdsc', get_ssrc_name(ssrc), strlen(get_ssrc_name(ssrc)), 1);
#endif
    }

    // the ssrc of the incoming packet is different to the ssrc of the decoding chain
    // so the decoding chain must be rebuilt

    clear_decoding_chain(conn);
    // create the new decoding chain
    conn->incoming_ssrc = ssrc;
    if (conn->incoming_ssrc != SSRC_NONE) {

      // get a codec
      // ideas and some code from https://rodic.fr/blog/libavcodec-tutorial-decode-audio-file/
      // with thanks

      // Set up the decoder depending on the ssrc code.
      switch (ssrc) {
      case ALAC_44100_S16_2:
      case ALAC_48000_S24_2:
        conn->codec = avcodec_find_decoder(AV_CODEC_ID_ALAC);
        break;
      case AAC_44100_F24_2:
      case AAC_48000_F24_2:
      case AAC_48000_F24_5P1:
      case AAC_48000_F24_7P1:
        conn->codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
        break;
      default:
        die("Can't find a suitable codec for SSRC: %s", get_ssrc_name(ssrc));
        break;
      }

      // Get a decoder-dependent codec context
      conn->codec_context = avcodec_alloc_context3(conn->codec);
      if (conn->codec_context == NULL) {
        debug(1, "Could not allocate a codec context!");
      }
      // push a deallocator -- av_free(codec_context)
      pthread_cleanup_push(avcodec_alloc_context3_cleanup_handler, conn->codec_context);

      // prepare to open the codec context with that codec
      // but first, if it's the ALAC decoder, prepare a magic cookie
      if ((ssrc == ALAC_48000_S24_2) || (ssrc == ALAC_44100_S16_2)) {
        alac_ffmpeg_magic_cookie *extradata =
            malloc(sizeof(alac_ffmpeg_magic_cookie)); // might not use it
        if (extradata == NULL)
          die("Could not allocate memory for a magic cookie.");
        // creata a magic cookie preceded by the 12-byte "atom" (?) expected by FFMPEG (?)
        memset(extradata, 0, sizeof(alac_ffmpeg_magic_cookie));
        extradata->cookie_size = htonl(sizeof(alac_ffmpeg_magic_cookie));
        extradata->cookie_tag = htonl('alac');
        extradata->alac_config.frameLength = htonl(352);
        if (ssrc == ALAC_48000_S24_2) {
          extradata->alac_config.bitDepth = 24; // Seems to be S24
          extradata->alac_config.sampleRate = htonl(48000);
        } else {
          extradata->alac_config.bitDepth = 16; // Seems to be S16
          extradata->alac_config.sampleRate = htonl(44100);
        }
        extradata->alac_config.pb = 40;
        extradata->alac_config.mb = 10;
        extradata->alac_config.kb = 14;
        extradata->alac_config.numChannels = 2;
        extradata->alac_config.maxRun = htons(255);
        conn->codec_context->extradata = (uint8_t *)extradata;
        conn->codec_context->extradata_size = sizeof(alac_ffmpeg_magic_cookie);
      } else {
        conn->codec_context->extradata = NULL;
      }
      pthread_cleanup_push(malloc_cleanup, &conn->codec_context->extradata);

      if (avcodec_open2(conn->codec_context, conn->codec, NULL) < 0) {
        die("Could not initialise the codec context");
      }

      // push a closer -- avcodec_close(codec_context);
      pthread_cleanup_push(avcodec_open2_cleanup_handler, conn->codec_context);

      conn->input_rate = get_ssrc_rate(ssrc);
      if ((ssrc == ALAC_48000_S24_2) || (ssrc == ALAC_44100_S16_2)) {
        conn->frames_per_packet = 352;
      } else {
        conn->frames_per_packet = 1024;
      }

      conn->codec_context->sample_rate = conn->input_rate;

      conn->ffmpeg_decoding_chain_initialised = 1;
      pthread_cleanup_pop(0); // successful exit -- don't run the avcodec_open2_cleanup_handler
      pthread_cleanup_pop(0); // successful exit -- don't deallocate the malloc
      pthread_cleanup_pop(
          0); // successful exit -- don't run the avcodec_alloc_context3_cleanup_handler
    }
  }
}

// take an AV Frame, run it through the swr resampler and map the output to the
// appropriate channels for the output device

// returns the length of time in nanoseconds associated with the frames that are being retained

int64_t avframe_to_audio(rtsp_conn_info *conn, AVFrame *decoded_frame, uint8_t **decoded_audio,
                         size_t *decoded_audio_data_length, size_t *decoded_audio_samples_count) {
  uint8_t *pcm_audio = NULL;
  int dst_linesize;

  int number_of_output_samples_expected = swr_get_out_samples(conn->swr, decoded_frame->nb_samples);

  debug(3, "A maximum of %d output samples expected for %d input samples.",
        number_of_output_samples_expected, decoded_frame->nb_samples);
  // allocate enough space for the required number of output channels
  // and the number of samples decoded
  // the format is always S32
  av_samples_alloc(&pcm_audio, &dst_linesize, conn->resampler_output_channels,
                   number_of_output_samples_expected, AV_SAMPLE_FMT_S32, 0);
  uint64_t conversion_start_time = get_absolute_time_in_ns();
  int samples_generated =
      swr_convert(conn->swr, &pcm_audio, number_of_output_samples_expected,
                  (const uint8_t **)decoded_frame->extended_data, decoded_frame->nb_samples);
  debug(3, "conversion time for %u incoming samples: %.3f milliseconds.", decoded_frame->nb_samples,
        (get_absolute_time_in_ns() - conversion_start_time) * 0.000001);
  if (samples_generated > 0) {
    debug(3, "swr generated %d frames of %" PRId64 " channels.", samples_generated,
          conn->resampler_output_channels);
    // samples_generated will be different from
    // the number of samples input if the output rate is different from the input
    // now, allocate a buffer and transfer the audio into it.

    ssize_t sample_buffer_size = conn->resampler_output_bytes_per_sample *
                                 CHANNELS_FROM_ENCODED_FORMAT(config.current_output_configuration) *
                                 samples_generated;
    void *sample_buffer = malloc(sample_buffer_size);

    memset(sample_buffer, 0, sample_buffer_size); // silence

    unsigned int input_stride = conn->resampler_output_channels;
    unsigned int output_stride = CHANNELS_FROM_ENCODED_FORMAT(config.current_output_configuration);

    unsigned int channels_to_map =
        CHANNELS_FROM_ENCODED_FORMAT(config.current_output_configuration); // the output width
    // if (conn->output_channel_map_size < channels_to_map)
    //  channels_to_map = conn->output_channel_map_size; // or the channel map given

    switch (conn->resampler_output_bytes_per_sample) {
    case 4: {
      int32_t *inframe = (int32_t *)pcm_audio;
      int32_t *outframe = (int32_t *)sample_buffer;
      int i;
      for (i = 0; i < samples_generated; i++) {
        unsigned int j;
        for (j = 0; j < channels_to_map; j++) {
          if (conn->output_channel_to_resampler_channel_map[j] == front_mono_channel_index) {
            // asking for the "FM" channel, which is a made-up name for
            // Front Mono
            int32_t monoValue = (inframe[0] / 2) + (inframe[1] / 2);
            outframe[j] = monoValue;
          } else if (conn->output_channel_to_resampler_channel_map[j] !=
                     silent_channel_index) // means you're asking for the
                                           // silent channel
            outframe[j] = inframe[conn->output_channel_to_resampler_channel_map[j]];
        }
        inframe += input_stride;   // address increment is scaled by the
                                   // size of an int32_t
        outframe += output_stride; // address increment is scaled by the
                                   // size of an int32_t
      }
    } break;
    case 2: {
      int16_t *inframe = (int16_t *)pcm_audio;
      int16_t *outframe = (int16_t *)sample_buffer;
      int i;
      for (i = 0; i < samples_generated; i++) {
        unsigned int j;
        for (j = 0; j < channels_to_map; j++) {
          if (conn->output_channel_to_resampler_channel_map[j] == front_mono_channel_index) {
            // asking for the "FM" channel, which is a made-up name for
            // Front Mono
            int16_t monoValue = (inframe[0] / 2) + (inframe[1] / 2);
            outframe[j] = monoValue;
          } else if (conn->output_channel_to_resampler_channel_map[j] !=
                     silent_channel_index) // means you're asking for the
                                           // silent channel
            outframe[j] = inframe[conn->output_channel_to_resampler_channel_map[j]];
        }
        inframe += input_stride;   // address increment is scaled by the
                                   // size of an int16_t
        outframe += output_stride; // address increment is scaled by the
                                   // size of an int16_t
      }
    } break;
    default:
      debug(1, "resampler output byte size of %u not handled.",
            conn->resampler_output_bytes_per_sample);
      break;
    }

    *decoded_audio = sample_buffer;
    *decoded_audio_data_length = sample_buffer_size;
    *decoded_audio_samples_count = samples_generated;
  } else {
    // samples_generated contains the negative of the error code
    debug(1, "swr_convert error %d. No samples generated from this avframe", -samples_generated);
    *decoded_audio = NULL;
    *decoded_audio_data_length = 0;
    *decoded_audio_samples_count = 0;
  }
  av_freep(&pcm_audio);
  return swr_get_delay(
      conn->swr,
      RATE_FROM_ENCODED_FORMAT(
          config.current_output_configuration)); // number of frames left in the resampler
}

// take a block of incoming data and decode it.
// it might get decoded into fltp ot lpcm or something -- it'll be
// transcoded and maybe resampled later
AVFrame *block_to_avframe(rtsp_conn_info *conn, uint8_t *incoming_data,
                          size_t incoming_data_length) {
  AVFrame *decoded_frame = NULL;
  if (incoming_data_length > 8) {
    AVPacket *pkt = av_packet_alloc();
    if (pkt) {
      // push a deallocator -- av_packet_free(pkt);
      pthread_cleanup_push(av_packet_alloc_cleanup_handler, &pkt);
      pkt->data = incoming_data;
      pkt->size = incoming_data_length;
      int ret = avcodec_send_packet(conn->codec_context, pkt);
      if (ret == 0) {
        decoded_frame = av_frame_alloc();
        if (decoded_frame == NULL) {
          debug(1, "Can't allocate an AVFrame!");
        } else {
          ret = avcodec_receive_frame(conn->codec_context, decoded_frame);

          if (ret < 0) {
            av_frame_free(&decoded_frame);
            decoded_frame = NULL;
            debug(1, "error %d during decoding. Data size: %zd", ret, incoming_data_length);
            /*
                      char *obf = malloc(incoming_data_length * 3);
                      char *obfp = obf;
                      unsigned int obfc;
                      for (obfc = 0; obfc < incoming_data_length; obfc++) {
                        snprintf(obfp, 3, "%02X", incoming_data[obfc]);
                        obfp += 2;
                        if ((obfc & 7) == 7) {
                          snprintf(obfp, 2, " ");
                          obfp += 1;
                        }
                      };
                      *obfp = 0;
                      debug(1, "%s...", obf);
                      free(obf);
            */
          }
        }
      } else {
        debug(1, "error %d during decoding. Gross data size: %zd", ret, incoming_data_length);
        /*
              char obf[128];
              char *obfp = obf;
              int obfc;
              for (obfc = 0; obfc < 32; obfc++) {
                snprintf(obfp, 3, "%02X", incoming_data[obfc]);
                obfp += 2;
                if ((obfc & 7) == 7) {
                  snprintf(obfp, 2, " ");
                  obfp += 1;
                }
              }
              *obfp = 0;
              debug(1, "%s", obf);
        */
      }
      pthread_cleanup_pop(1); // deallocate the AVPacket;
    } else {
      debug(1, "Can't allocate an AVPacket!");
    }
  }
  return decoded_frame;
}

size_t avflush(rtsp_conn_info *conn) {
  size_t response = 0;
  if (conn->swr != NULL) {

    int number_of_output_samples_expected = swr_get_out_samples(conn->swr, 0);
    debug(3, "avflush of %d samples.", number_of_output_samples_expected);
    int ret = swr_init(conn->swr);
    if (ret)
      debug(1, "error %d in swr_init().", ret);
    response = (size_t)number_of_output_samples_expected;
  }
  return response;
}

#endif

#ifdef CONFIG_OPENSSL
// Thanks to
// https://stackoverflow.com/questions/27558625/how-do-i-use-aes-cbc-encrypt-128-openssl-properly-in-ubuntu
// for inspiration. Changed to a 128-bit key and no padding.

int openssl_aes_decrypt_cbc(unsigned char *ciphertext, int ciphertext_len, unsigned char *key,
                            unsigned char *iv, unsigned char *plaintext) {
  EVP_CIPHER_CTX *ctx;
  int len;
  int plaintext_len = 0;
  ctx = EVP_CIPHER_CTX_new();
  if (ctx != NULL) {
    if (EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv) == 1) {
      EVP_CIPHER_CTX_set_padding(ctx, 0); // no padding -- always returns 1
      // no need to allow space for padding in the output, as padding is disabled
      if (EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext, ciphertext_len) == 1) {
        plaintext_len = len;
        if (EVP_DecryptFinal_ex(ctx, plaintext + len, &len) == 1) {
          plaintext_len += len;
        } else {
          debug(1, "EVP_DecryptFinal_ex error \"%s\".", ERR_error_string(ERR_get_error(), NULL));
        }
      } else {
        debug(1, "EVP_DecryptUpdate error \"%s\".", ERR_error_string(ERR_get_error(), NULL));
      }
    } else {
      debug(1, "EVP_DecryptInit_ex error \"%s\".", ERR_error_string(ERR_get_error(), NULL));
    }
    EVP_CIPHER_CTX_free(ctx);
  } else {
    debug(1, "EVP_CIPHER_CTX_new error \"%s\".", ERR_error_string(ERR_get_error(), NULL));
  }
  return plaintext_len;
}
#endif

#ifdef CONFIG_AIRPLAY_2

#ifdef CONFIG_AIRPLAY_2
// This is a big dirty hack to try to accommodate packets that come in in sequence but are timed to
// be earlier that what went before them. This happens when the feed is switching from AAC to ALAC.
// So basically we will look back through the buffers in the queue until we find the last buffer
// that predates the incoming one. We will make the subsequent buffer the revised_seqno. If we can't
// find an older buffer, that means we can't go back far enough to find an older buffer and then the
// ab_read buffer becomes the revised_seqno.
seq_t get_revised_seqno(rtsp_conn_info *conn, uint32_t timestamp) {
  // go back through the buffers to find the first buffer following a buffer that predates
  // the given timestamp, if any.
  seq_t revised_seqno = conn->ab_write;
  pthread_cleanup_debug_mutex_lock(&conn->ab_mutex, 30000, 0);
  int older_seqno_found = 0;
  while ((older_seqno_found == 0) && (revised_seqno != conn->ab_read)) {
    revised_seqno--;
    abuf_t *tbuf = conn->audio_buffer + BUFIDX(revised_seqno);
    if (tbuf->ready != 0) {
      int32_t timestamp_difference = timestamp - tbuf->timestamp;
      if (timestamp_difference >= 0) {
        older_seqno_found = 1;
      }
    }
  }
  if (older_seqno_found)
    revised_seqno++;

  pthread_cleanup_pop(1);
  return revised_seqno;
}

void clear_buffers_from(rtsp_conn_info *conn, seq_t from_here) {
  seq_t bi = from_here;
  while (bi != conn->ab_write) {
    abuf_t *tbuf = conn->audio_buffer + BUFIDX(bi);
    free_audio_buffer_payload(tbuf);
    bi++;
  }
}

#endif

#endif

#ifdef CONFIG_FFMPEG
uint32_t player_put_packet(uint32_t ssrc, seq_t seqno, uint32_t actual_timestamp, uint8_t *data,
                           size_t len, int mute, int32_t timestamp_gap, rtsp_conn_info *conn) {
#else
uint32_t player_put_packet(uint32_t ssrc, seq_t seqno, uint32_t actual_timestamp, uint8_t *data,
                           size_t len, __attribute__((unused)) int mute, int32_t timestamp_gap,
                           rtsp_conn_info *conn) {
#endif

  // clang-format off
  
  // The timestamp_gap is the difference between the timestamp and the expected timestamp.
  // It should normally be zero.

  // It can be decoded by the Hammerton or Apple ALAC decoders, or by the FFmpeg decoder.

  // The SSRC signifies the encoding used for that block of audio.
  // It is used to select the type of decoding to be done by the FFMPEG-based
  // decoding chain

  // If mute is true, then decode the packet to get its length, but mute it -- i.e.
  // replace it with the same duration of silence.
  // This is useful because the first block of an AAC play sequence usually contains
  // noisy transients.
  // Not needed in Classic airPlay as there's no AAC in it.

  // Function returns the number of samples in the packet so that callers can watch for
  // anomalies in sequencing.
  // clang-format on

  uint32_t input_packets_used = 0;

  // ignore a request to flush that has been made before the first packet...
  if (conn->packet_count == 0) {
    debug_mutex_lock(&conn->flush_mutex, 1000, 1);
    conn->flush_requested = 0;
    conn->flush_rtp_timestamp = 0;
    debug_mutex_unlock(&conn->flush_mutex, 3);
  }

  pthread_cleanup_debug_mutex_lock(&conn->ab_mutex, 30000, 0);
  uint64_t time_now = get_absolute_time_in_ns();
  conn->packet_count++;
  conn->packet_count_since_flush++;
  conn->time_of_last_audio_packet = time_now;
  if (conn->connection_state_to_output) { // if we are supposed to be processing these packets
    abuf_t *abuf = 0;
    if (!conn->ab_synced) {
      conn->ab_write = seqno;
      conn->ab_read = seqno;
      conn->ab_synced = 1;
      conn->first_packet_timestamp = 0;
      debug(2, "Connection %d: synced by first packet, timestamp %u, seqno %u.",
            conn->connection_number, actual_timestamp, seqno);
    }
    if (conn->ab_write ==
        seqno) { // if this is the expected packet (which could be the first packet...)
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
      conn->ab_write = seqno + 1; // move the write pointer to the next free space
    } else {
      int16_t after_ab_write_gap = seqno - conn->ab_write;
      if (after_ab_write_gap > 0) {
        int i;
        for (i = 0; i < after_ab_write_gap; i++) {
          abuf = conn->audio_buffer + BUFIDX(conn->ab_write + i);
          abuf->ready = 0; // to be sure, to be sure
          abuf->resend_request_number = 0;
          abuf->initialisation_time =
              time_now;          // this represents when the packet was noticed to be missing
          abuf->status = 1 << 0; // signifying missing
          abuf->resend_time = 0;
          abuf->timestamp = 0;
          abuf->sequence_number = 0;
        }
        abuf = conn->audio_buffer + BUFIDX(seqno);
        //        rtp_request_resend(ab_write, gap);
        //        resend_requests++;
        conn->ab_write = seqno + 1;
      } else {
        int16_t after_ab_read_gap = seqno - conn->ab_read;
        if (after_ab_read_gap >= 0) { // older than expected but not too late
          debug(3, "buffer %u is older than expected but not too late", seqno);
          conn->late_packets++;
          abuf = conn->audio_buffer + BUFIDX(seqno);
        } else { // too late.
          debug(3, "buffer %u is too late", seqno);
          conn->too_late_packets++;
        }
      }
    }
    if (abuf) {
      if (free_audio_buffer_payload(abuf)) {
        if (seqno == abuf->sequence_number)
          debug(3, "audio block %u received for a second (or more) time?", seqno);
        else
          debug(3, "audio block %u with prior sequence number %u  -- payload not freed until now!",
                seqno, abuf->sequence_number);
      }
      abuf->initialisation_time = time_now;
      abuf->resend_time = 0;
      abuf->length = 0; // may not be needed

      if (ssrc == ALAC_44100_S16_2) {
        // This could be a Classic AirPlay or an AirPlay 2 Realtime packet.
        // It always has a length of 352 frames per packet.
        // And it's always 16-bit interleaved stereo.
        uint8_t *data_to_use = data;
        uint8_t *intermediate_buffer = malloc(len); // encryption is not compression...

        // decrypt it if necessary
        if (conn->stream.encrypted) {
          unsigned char iv[16];
          int aeslen = len & ~0xf;
          memcpy(iv, conn->stream.aesiv, sizeof(iv));
#ifdef CONFIG_MBEDTLS
          mbedtls_aes_crypt_cbc(&conn->dctx, MBEDTLS_AES_DECRYPT, aeslen, iv, data,
                                intermediate_buffer);
#endif
#ifdef CONFIG_POLARSSL
          aes_crypt_cbc(&conn->dctx, AES_DECRYPT, aeslen, iv, data, intermediate_buffer);
#endif
#ifdef CONFIG_OPENSSL
          openssl_aes_decrypt_cbc(data, aeslen, conn->stream.aeskey, iv, intermediate_buffer);
          // AES_cbc_encrypt(data, intermediate_buffer, aeslen, &conn->aes, iv, AES_DECRYPT);
#endif
          memcpy(intermediate_buffer + aeslen, data + aeslen, len - aeslen);
          data_to_use = intermediate_buffer;
        }

        // Use the selected decoder
        if ((config.decoder_in_use == 1 << decoder_hammerton) ||
            (config.decoder_in_use == 1 << decoder_apple_alac)) {
          abuf->data = malloc(conn->frames_per_packet * conn->input_bytes_per_frame);
          if (abuf->data != NULL) {
            unencrypted_packet_decode(conn, data_to_use, len, abuf->data);
            input_packets_used = conn->frames_per_packet; // return this to the caller
            abuf->length = conn->frames_per_packet;       // these decoders don't transcode
          } else {
            debug(1, "audio block not allocated!");
          }
        } else if (config.decoder_in_use == 1 << decoder_ffmpeg_alac) {
#ifdef CONFIG_FFMPEG
          prepare_decoding_chain(conn, ALAC_44100_S16_2);
          // if (len > 8) {
            abuf->avframe = block_to_avframe(conn, data_to_use, len);
            abuf->ssrc = ALAC_44100_S16_2;
            if (abuf->avframe) {
              input_packets_used = abuf->avframe->nb_samples;
            }
            if (mute) {
              // it's important to have already run it through the decoder before dropping it
              // especially if it an AAC decoder
              debug(2, "ap1 muting frame %u.", actual_timestamp);
              abuf->length = abuf->avframe->nb_samples;
              av_frame_free(&abuf->avframe);
              abuf->avframe = NULL;
            }
          // } else {
        if (len <= 8) {
          debug(1,
                "Using the FFMPEG ALAC_44100_S16_2 decoder, a short audio packet %u, rtptime %u, of length %zu has been decoded but not discarded. Contents follow:", seqno,
                actual_timestamp, len);
          debug_print_buffer(1, data, len);
            // abuf->length = conn->frames_per_packet;
            // abuf->avframe = NULL;
          }
#else
          debug(1, "FFMPEG support has not been built into this version Shairport Sync!");
#endif
        } else {
          debug(1, "Unknown decoder!");
        }

        // may be used during decryption
        if (intermediate_buffer != NULL) {
          free(intermediate_buffer);
          intermediate_buffer = NULL;
        }

        abuf->ready = 1;
        abuf->status = 0; // signifying that it was received
        abuf->timestamp = actual_timestamp;
        abuf->timestamp_gap = timestamp_gap; // needed to decide if a resync is needed
        abuf->sequence_number = seqno;
      } else {
        // This is AirPlay 2 -- always use FFmpeg
#ifdef CONFIG_FFMPEG

        // Use the appropriate FFMPEG decoder

        // decoding is done now, transcoding to S32 and resampling is
        // deferred to the player thread, to be sure all the blocks
        // of data are present

        prepare_decoding_chain(conn, ssrc); // dynamically set the decoding environment

        // if (len > 8) {
          abuf->avframe = block_to_avframe(conn, data, len);
          abuf->ssrc = ssrc; // tag the avframe with its specific SSRC
          if (abuf->avframe) {
            input_packets_used = abuf->avframe->nb_samples;
          }
          if (mute) {
            // it's important to have already run it through the decoder before dropping it
            debug(2, "ap2 muting frame %u.", actual_timestamp);
            abuf->length = abuf->avframe->nb_samples;
            av_frame_free(&abuf->avframe);
            abuf->avframe = NULL;
          }
        //} else {
        if (len <= 8) {
          debug(1,
                "Using an FFMPEG decoder, a short audio packet %u, rtptime %u, of length %zu has been decoded but not discarded. Contents follow:", seqno,
                actual_timestamp, len);
          debug_print_buffer(1, data, len);
          // abuf->length = 0;
          // abuf->avframe = NULL;
        }
        abuf->ready = 1;
        abuf->status = 0; // signifying that it was received
        abuf->timestamp = actual_timestamp;
        abuf->timestamp_gap = timestamp_gap;
        abuf->sequence_number = seqno;
#else
        debug(1, "FFMPEG support has not been included, so the packet is discarded.");
        abuf->ready = 0;
        abuf->status = 1 << 1; // bad packet, discarded
        abuf->resend_request_number = 0;
        abuf->timestamp = 0;
        abuf->timestamp_gap = 0;
        abuf->sequence_number = 0;
#endif
      }
    }
    /*
    {
    uint64_t the_time_this_frame_should_be_played;
                  frame_to_local_time(abuf->timestamp,
                                      &the_time_this_frame_should_be_played, conn);
    int64_t lead_time = the_time_this_frame_should_be_played - get_absolute_time_in_ns();
    debug(1, "put_packet %u, lead time is %.3f ms.", abuf->timestamp, lead_time * 0.000001);
    }
    */
    int rc = pthread_cond_signal(&conn->flowcontrol);
    if (rc)
      debug(1, "Error signalling flowcontrol.");

    // resend checks
    {
      uint64_t minimum_wait_time =
          (uint64_t)(config.resend_control_first_check_time * (uint64_t)1000000000);
      uint64_t resend_repeat_interval =
          (uint64_t)(config.resend_control_check_interval_time * (uint64_t)1000000000);
      uint64_t minimum_remaining_time = (uint64_t)((config.resend_control_last_check_time +
                                                    config.audio_backend_buffer_desired_length) *
                                                   (uint64_t)1000000000);
      uint64_t latency_time = (uint64_t)(conn->latency * (uint64_t)1000000000);
      latency_time = latency_time / (uint64_t)conn->input_rate;

      // find the first frame that is missing, if known
      int x = conn->ab_read;
      if (first_possibly_missing_frame >= 0) {
        // if it's within the range
        int16_t buffer_size = conn->ab_write - conn->ab_read; // must be positive
        if (buffer_size >= 0) {
          int16_t position_in_buffer = first_possibly_missing_frame - conn->ab_read;
          if ((position_in_buffer >= 0) && (position_in_buffer < buffer_size))
            x = first_possibly_missing_frame;
        }
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
            // debug_mutex_unlock(&conn->ab_mutex, 3);
            rtp_request_resend(start_of_missing_frame_run, missing_frame_run_count, conn);
            // debug_mutex_lock(&conn->ab_mutex, 20000, 1);
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
  pthread_cleanup_pop(1);
  // debug_mutex_unlock(&conn->ab_mutex, 0);
  return input_packets_used;
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

static inline void process_sample(int32_t sample, char **outp, sps_format_t format, int volume,
                                  int dither, rtsp_conn_info *conn) {
  int64_t hyper_sample = sample;
  int result = 0;

  if (conn->do_loudness != 0) {
    hyper_sample <<=
        32; // Do not apply volume as it has already been done with the Loudness DSP filter
  } else {
    int64_t hyper_volume = (int64_t)volume << 16;
    hyper_sample = hyper_sample * hyper_volume; // this is 64 bit bit multiplication -- we may need
                                                // to dither it down to its target resolution
  }

  // next, do dither, if necessary
  if (dither) {

    // Add a TPDF dither -- see
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

void buffer_get_frame_cleanup_handler(__attribute__((unused)) void *arg) {
  // rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  // debug_mutex_unlock(&conn->ab_mutex, 0);
}

// get the next frame, when available. return 0 if underrun/stream reset.
static abuf_t *buffer_get_frame(rtsp_conn_info *conn, int resync_requested) {
  // int16_t buf_fill;
  // struct timespec tn;

  /*
  {
    abuf_t *curframe = conn->audio_buffer + BUFIDX(conn->ab_read);
    if (curframe != NULL) {
      debug(1, "get seqno %u with ready: %u.", curframe->sequence_number, curframe->ready);
    }
  }
  */

  abuf_t *curframe = NULL;
  int notified_buffer_empty = 0; // diagnostic only

  pthread_cleanup_debug_mutex_lock(&conn->ab_mutex, 30000, 0);

  int wait;
  long dac_delay = 0; // long because alsa returns a long

  int output_device_has_been_primed =
      0; // set to true when we have sent at least one silent frame to the DAC

  pthread_cleanup_push(buffer_get_frame_cleanup_handler,
                       (void *)conn); // undo what's been done so far
  do {
    // debug(3, "buffer_get_frame is iterating");
    // we must have timing information before we can do anything here
    if (have_timestamp_timing_information(conn)) {

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

      pthread_cleanup_debug_mutex_lock(&conn->flush_mutex, 1000, 0);
      if (conn->flush_requested == 1) {
        if (conn->flush_output_flushed == 0) {
#if CONFIG_FFMPEG
          avflush(conn);
#endif
          if (config.output->flush) {
            config.output->flush(); // no cancellation points
            debug(2, "flush request: flush output device.");
          }
        }
        conn->flush_output_flushed = 1;
      }
      // now check to see it the flush request is for frames in the buffer or not
      // if the first_packet_timestamp is zero, don't check
      int flush_needed = 0;
      int drop_request = 0;
      if (conn->flush_requested == 1) {
        if (conn->flush_rtp_timestamp == 0) {
          debug(1, "flush request: flush frame 0 -- flush assumed to be needed.");
          flush_needed = 1;
          drop_request = 1;
        } else {
          if ((conn->ab_synced) && ((conn->ab_write - conn->ab_read) > 0)) {
            abuf_t *firstPacket = conn->audio_buffer + BUFIDX(conn->ab_read);
            abuf_t *lastPacket = conn->audio_buffer + BUFIDX(conn->ab_write - 1);
            if ((firstPacket != NULL) && (firstPacket->ready)) {
              uint32_t first_frame_in_buffer = firstPacket->timestamp;
              int32_t offset_from_first_frame = conn->flush_rtp_timestamp - first_frame_in_buffer;
              if ((lastPacket != NULL) && (lastPacket->ready)) {
                // we have enough information to check if the flush is needed or can be discarded
                uint32_t last_frame_in_buffer = lastPacket->timestamp + lastPacket->length - 1;

                // clang-format off
                // Now we have to work out if the flush frame is in the buffer.
                
                // If it is later than the end of the buffer, flush everything and keep the
                // request active.
                
                // If it is in the buffer, we need to flush part of the buffer.
                // (Actually we flush the entire buffer and drop the request.)
                
                // If it is before the buffer, no flush is needed. Drop the request.
                // clang-format on

                if (offset_from_first_frame > 0) {
                  int32_t offset_to_last_frame = last_frame_in_buffer - conn->flush_rtp_timestamp;
                  if (offset_to_last_frame >= 0) {
                    debug(2,
                          "flush request: flush frame %u active -- buffer contains %u frames, from "
                          "%u to %u.",
                          conn->flush_rtp_timestamp,
                          last_frame_in_buffer - first_frame_in_buffer + 1, first_frame_in_buffer,
                          last_frame_in_buffer);

                    // We need to drop all complete frames leading up to the frame containing
                    // the flush request frame.
                    int32_t offset_to_flush_frame = 0;
                    abuf_t *current_packet = NULL;
                    do {
                      current_packet = conn->audio_buffer + BUFIDX(conn->ab_read);
                      if (current_packet != NULL) {
                        uint32_t last_frame_in_current_packet =
                            current_packet->timestamp + current_packet->length - 1;
                        offset_to_flush_frame =
                            conn->flush_rtp_timestamp - last_frame_in_current_packet;
                        if (offset_to_flush_frame > 0) {
                          debug(2,
                                "flush to %u request: flush buffer %u, from "
                                "%u to %zu. ab_write is: %u.",
                                conn->flush_rtp_timestamp, conn->ab_read, current_packet->timestamp,
                                current_packet->timestamp + current_packet->length - 1,
                                conn->ab_write);
                          conn->ab_read++;
                        }
                      } else {
                        debug(1, "NULL current_packet");
                      }
                      pthread_testcancel(); // even if no packets are coming in...
                    } while ((current_packet == NULL) || (offset_to_flush_frame > 0));
                    // now remove any frames from the buffer that are before the flush frame itself.
                    int32_t frames_to_remove =
                        conn->flush_rtp_timestamp - current_packet->timestamp;
                    if (frames_to_remove > 0) {
                      debug(2, "%u frames to remove from current buffer", frames_to_remove);
                      void *dest = (void *)current_packet->data;
                      void *source = dest + conn->input_bytes_per_frame * frames_to_remove;
                      size_t frames_remaining = (current_packet->length - frames_to_remove);
                      memmove(dest, source, frames_remaining * conn->input_bytes_per_frame);
                      current_packet->timestamp = conn->flush_rtp_timestamp;
                      current_packet->length = frames_remaining;
                    }
                    debug(
                        2,
                        "flush request: flush frame %u complete -- buffer contains %u frames, from "
                        "%u to %u -- flushed to %u in buffer %u, with %u frames remaining.",
                        conn->flush_rtp_timestamp, last_frame_in_buffer - first_frame_in_buffer + 1,
                        first_frame_in_buffer, last_frame_in_buffer, current_packet->timestamp,
                        conn->ab_read, last_frame_in_buffer - current_packet->timestamp + 1);
                    drop_request = 1;
                  } else {
                    if (conn->flush_rtp_timestamp == last_frame_in_buffer + 1) {
                      debug(
                          2,
                          "flush request: flush frame %u completed -- buffer contained %u frames, "
                          "from "
                          "%u to %u",
                          conn->flush_rtp_timestamp,
                          last_frame_in_buffer - first_frame_in_buffer + 1, first_frame_in_buffer,
                          last_frame_in_buffer);
                      drop_request = 1;
                    } else {
                      debug(2,
                            "flush request: flush frame %u pending -- buffer contains %u frames, "
                            "from "
                            "%u to %u",
                            conn->flush_rtp_timestamp,
                            last_frame_in_buffer - first_frame_in_buffer + 1, first_frame_in_buffer,
                            last_frame_in_buffer);
                    }
                    flush_needed = 1;
                  }
                } else {
                  debug(2,
                        "flush request: flush frame %u expired -- buffer contains %u frames, "
                        "from %u "
                        "to %u",
                        conn->flush_rtp_timestamp, last_frame_in_buffer - first_frame_in_buffer + 1,
                        first_frame_in_buffer, last_frame_in_buffer);
                  drop_request = 1;
                }
              }
            }
          } else {
            debug(3,
                  "flush request: flush frame %u  -- buffer not synced or empty: synced: %d, "
                  "ab_read: "
                  "%u, ab_write: %u",
                  conn->flush_rtp_timestamp, conn->ab_synced, conn->ab_read, conn->ab_write);
            conn->flush_requested = 0; // remove the request
            // leave flush request pending and don't do a buffer flush, because there isn't one
          }
        }
      }
      if (flush_needed) {
        debug(2, "flush request: flush done.");
        ab_resync(conn); // no cancellation points
        conn->first_packet_timestamp = 0;
        conn->first_packet_time_to_play = 0;
        conn->time_since_play_started = 0;
        output_device_has_been_primed = 0;
        dac_delay = 0;
      }
      if (drop_request) {
        conn->flush_requested = 0;
        conn->flush_rtp_timestamp = 0;
        conn->flush_output_flushed = 0;
      }
      pthread_cleanup_pop(1); // unlock the conn->flush_mutex

      // skip out-of-date frames, and even more if we haven't seen the first frame
      int out_of_date = 1;
      uint32_t should_be_frame;

      uint64_t time_to_aim_for = get_absolute_time_in_ns();
      uint64_t desired_lead_time = 0;
      if (conn->first_packet_timestamp == 0)
        time_to_aim_for = time_to_aim_for + desired_lead_time;

      while ((conn->ab_synced) && ((conn->ab_write - conn->ab_read) > 0) && (out_of_date != 0)) {
        abuf_t *thePacket = conn->audio_buffer + BUFIDX(conn->ab_read);
        if ((thePacket != NULL) && (thePacket->ready)) {
          local_time_to_frame(time_to_aim_for, &should_be_frame, conn);
          // debug(1,"should_be frame is %u.",should_be_frame);
          int32_t frame_difference = thePacket->timestamp - should_be_frame;
          if (frame_difference < 0) {
            debug(3,
                  "Connection %d: dropping-out-of-date packet %u with timestamp %u. Lead time is "
                  "%f seconds.",
                  conn->connection_number, conn->ab_read, thePacket->timestamp,
                  frame_difference * 1.0 / conn->input_rate + desired_lead_time * 0.000000001);
            free_audio_buffer_payload(thePacket);
            conn->last_seqno_read = conn->ab_read;
            conn->ab_read++;
          } else {
            if (conn->first_packet_timestamp == 0)
              debug(3,
                    "Connection %d: accepting packet sequence number %u, ab_read: %u with "
                    "timestamp %u. Lead time is %f seconds.",
                    conn->connection_number, thePacket->sequence_number, conn->ab_read,
                    thePacket->timestamp,
                    frame_difference * 1.0 / conn->input_rate + desired_lead_time * 0.000000001);
            out_of_date = 0;
          }
        } else {
          if (thePacket == NULL)
            debug(2, "Connection %d: packet %u is empty.", conn->connection_number, conn->ab_read);
          else
            debug(3, "Connection %d: packet %u not ready.", conn->connection_number, conn->ab_read);
          conn->ab_read++;
          conn->last_seqno_read++; // don' let it trigger the missing packet warning...
        }
        pthread_testcancel(); // even if no packets are coming in...
      }
      int16_t buffers_available = conn->ab_write - conn->ab_read;

      if ((conn->ab_synced) && (buffers_available > 0)) {
        curframe = conn->audio_buffer + BUFIDX(conn->ab_read);
        if (resync_requested != 0) {
          /*
          if (((curframe != NULL) && ((conn->first_packet_timestamp != curframe->timestamp) &&
                                      (curframe->timestamp_gap < 0))) ||
              (resync_requested != 0)) {
            // ignore a timestamp gap that occurs before the first_packet_timestamp
            if (curframe == NULL)
              debug(1, "Connection %d: reset first_packet_timestamp because curframe is NULL.",
                    conn->connection_number);
            if (curframe->timestamp_gap != 0)
              debug(1,
                    "Connection %d: reset first_packet_timestamp because curframe %u's timestamp_gap
          is negative: "
                    "%d.",
                    conn->connection_number, curframe->timestamp, curframe->timestamp_gap);
            if (resync_requested != 0)
          */
          debug(2, "Connection %d: reset first_packet_timestamp resync_requested.",
                conn->connection_number);
          conn->ab_buffering = 1;
          conn->first_packet_timestamp = 0;
          conn->first_packet_time_to_play = 0;
          output_device_has_been_primed = 1; // so that it can rely on the delay provided by it
        }

        if (conn->ab_synced) {

          if (curframe != NULL) {
            uint64_t should_be_time;
            frame_to_local_time(curframe->timestamp, &should_be_time, conn);
            int64_t time_difference = should_be_time - get_absolute_time_in_ns();
            debug(3, "Check packet from buffer %u, timestamp %u, %f seconds ahead.", conn->ab_read,
                  curframe->timestamp, 0.000000001 * time_difference);
          } else {
            debug(3, "Check packet from buffer %u, empty.", conn->ab_read);
          }

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
                debug(1, "Connection %d: aliasing of buffer index -- reset.",
                      conn->connection_number);
              } else {
                debug(1, "Connection %d: inconsistent sequence numbers detected",
                      conn->connection_number);
              }
            }
          }

          if ((curframe) && (curframe->ready)) {
            notified_buffer_empty = 0; // at least one buffer now -- diagnostic only.
            if (conn->ab_buffering) {  // if we are getting packets but not yet forwarding them to
                                       // the player
              if (conn->first_packet_timestamp == 0) { // if this is the very first packet
                conn->first_packet_timestamp =
                    curframe->timestamp; // we will keep buffering until we are
                                         // supposed to start playing this
                debug(2, "Connection %d: first packet timestamp is %u.", conn->connection_number,
                      conn->first_packet_timestamp);

                // Even though it'll be some time before the first frame will be output
                // (and thus some time before the resampling chain is needed),
                // we need to set up the output device to correspond to
                // the input format w.r.t. rate, depth and channels
                // because we'll be sending silence before the first real frame.
                debug(3, "reset loudness filters.");
                loudness_reset();
#ifdef CONFIG_FFMPEG
                // Set up the output chain, including the software resampler.
                debug(2, "set up the output chain to %s for FFmpeg.",
                      get_ssrc_name(curframe->ssrc));
                setup_software_resampler(conn, curframe->ssrc);
                conn->output_sample_ratio = 1; // it's always 1 if we're using FFmpeg
#else
                // here, in the non-FFmpeg decoder case, we have the first frame, so
                // we should set up the output device now.
                debug(3,
                      "set up the output chain for the non-FFmpeg case with incoming audio at %u "
                      "FPS.",
                      conn->input_rate);

                // ask the backend if it can give us its best choice for a non-ffmpeg configuration:
                if (config.output->get_configuration) {
                  config.current_output_configuration = config.output->get_configuration(
                      2, conn->input_rate, (unsigned int)(SPS_FORMAT_S16));
                } else {
                  // otherwise, use the standard 44100/S16_LE/2 for non-ffmpeg operation
                  config.current_output_configuration = CHANNELS_TO_ENCODED_FORMAT(2) |
                                                        RATE_TO_ENCODED_FORMAT(44100) |
                                                        FORMAT_TO_ENCODED_FORMAT(SPS_FORMAT_S16_LE);
                }

                // tell the output device, if possible
                if (config.output->configure) {
                  config.output->configure(config.current_output_configuration, NULL);
                }

                if (conn->input_rate == 0)
                  debug(1, "input rate not set!");
                else
                  conn->output_sample_ratio =
                      RATE_FROM_ENCODED_FORMAT(config.current_output_configuration) /
                      conn->input_rate;

#endif
                // calculate the output bit depth
                conn->output_bit_depth = 16; // default;
                switch (FORMAT_FROM_ENCODED_FORMAT(config.current_output_configuration)) {
                case SPS_FORMAT_S8:
                case SPS_FORMAT_U8:
                  conn->output_bit_depth = 8;
                  break;
                case SPS_FORMAT_S16:
                case SPS_FORMAT_S16_LE:
                case SPS_FORMAT_S16_BE:
                  conn->output_bit_depth = 16;
                  break;
                case SPS_FORMAT_S24:
                case SPS_FORMAT_S24_LE:
                case SPS_FORMAT_S24_BE:
                case SPS_FORMAT_S24_3LE:
                case SPS_FORMAT_S24_3BE:
                  conn->output_bit_depth = 24;
                  break;
                case SPS_FORMAT_S32:
                case SPS_FORMAT_S32_LE:
                case SPS_FORMAT_S32_BE:
                  conn->output_bit_depth = 32;
                  break;
                case SPS_FORMAT_UNKNOWN:
                  die("An unknown format was encountered while choosing output bit depth. Please "
                      "check your configuration and settings.");
                  break;
                case SPS_FORMAT_AUTO:
                  die("Invalid format -- SPS_FORMAT_AUTO -- choosing output bit depth. Please "
                      "check your configuration and settings.");
                  break;
                case SPS_FORMAT_INVALID:
                  die("Invalid format -- SPS_FORMAT_INVALID -- choosing output bit depth. Please "
                      "check your configuration and settings.");
                  break;
                }
                debug(3, "output bit depth is %u.", conn->output_bit_depth);

                uint64_t should_be_time;
                frame_to_local_time(conn->first_packet_timestamp, // this will go modulo 2^32
                                    &should_be_time, conn);

                conn->first_packet_time_to_play = should_be_time;

                int64_t lt = conn->first_packet_time_to_play - get_absolute_time_in_ns();

                // can't be too late because we skipped late packets already, FLW.
                debug(2, "Connection %d: lead time for first frame %u: %f seconds.",
                      conn->connection_number, conn->first_packet_timestamp, lt * 0.000000001);
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

                  // readjust first packet time to play
                  frame_to_local_time(conn->first_packet_timestamp, &should_be_time, conn);

                  int64_t change_in_should_be_time =
                      (int64_t)(should_be_time - conn->first_packet_time_to_play);

                  if (fabs(0.000001 * change_in_should_be_time) >
                      0.001) // ignore this unless if's more than a microsecond
                    debug(
                        2,
                        "Change in estimated first_packet_time: %f milliseconds for first_packet.",
                        0.000001 * change_in_should_be_time);

                  conn->first_packet_time_to_play = should_be_time;

                  int64_t lead_time = conn->first_packet_time_to_play -
                                      get_absolute_time_in_ns(); // negative means late
                  if (lead_time < 0) {
                    debug(2, "Gone past starting time for %u by %" PRId64 " nanoseconds.",
                          conn->first_packet_timestamp, -lead_time);
                    conn->ab_buffering = 0;
                  } else {
                    // do some calculations
                    if ((config.audio_backend_silent_lead_in_time_auto == 1) ||
                        (lead_time <= (int64_t)(config.audio_backend_silent_lead_in_time *
                                                (int64_t)1000000000))) {
                      debug(3, "Lead time: %" PRId64 " nanoseconds.", lead_time);
                      int resp = 0;
                      dac_delay = 0;
                      if (output_device_has_been_primed != 0)
                        resp = config.output->delay(
                            &dac_delay); // we know the output device must have a delay function
                      if (resp == 0) {
                        int64_t gross_frame_gap =
                            ((conn->first_packet_time_to_play - get_absolute_time_in_ns()) *
                             RATE_FROM_ENCODED_FORMAT(config.current_output_configuration)) /
                            1000000000;
                        int64_t exact_frame_gap = gross_frame_gap - dac_delay;
                        debug(3,
                              "Exact frame gap: %" PRId64
                              ". DAC delay: %ld. First packet timestamp: %u.",
                              exact_frame_gap, dac_delay, conn->first_packet_timestamp);
                        // int64_t frames_needed_to_maintain_desired_buffer =
                        //     (int64_t)(config.audio_backend_buffer_desired_length *
                        //               config.current_output_configuration->rate) -
                        //     dac_delay;
                        // below, remember that exact_frame_gap and
                        // frames_needed_to_maintain_desired_buffer could both be negative
                        int64_t fs =
                            (RATE_FROM_ENCODED_FORMAT(config.current_output_configuration) * 100) /
                            1000; // 100 milliseconds
                        // if there isn't enough time to have the desired buffer size
                        // if (exact_frame_gap <= fs) {
                        //  fs = conn->frames_per_packet * 2;
                        // }
                        // if we are close to the end of buffering,
                        // just add the remaining silence needed and end buffering
                        if (exact_frame_gap < fs) {
                          debug(3, "exact frame below fs of %" PRId64 " frames.", fs);
                          fs = exact_frame_gap;
                          conn->ab_buffering = 0;
                        }
                        void *silence;
                        if (fs > 0) {
                          silence = malloc(
                              sps_format_sample_size(
                                  FORMAT_FROM_ENCODED_FORMAT(config.current_output_configuration)) *
                              CHANNELS_FROM_ENCODED_FORMAT(config.current_output_configuration) *
                              fs);
                          if (silence == NULL)
                            debug(1, "Failed to allocate %" PRId64 " byte silence buffer.", fs);
                          else {
                            // generate frames of silence with dither if necessary
                            pthread_cleanup_push(malloc_cleanup, &silence);

                            conn->previous_random_number = generate_zero_frames(
                                silence, fs, conn->enable_dither, conn->previous_random_number,
                                config.current_output_configuration);

                            debug(3, "Send %" PRId64 " frames of silence.", fs);
                            config.output->play(silence, fs, play_samples_are_untimed, 0, 0);
                            debug(3, "Sent %" PRId64 " frames of silence.", fs);
                            pthread_cleanup_pop(1); // deallocate silence
                            output_device_has_been_primed = 1;
                          }
                        }
                      } else {
                        if ((resp == -EAGAIN) || (resp == -EIO) || (resp == -ENODEV)) {
                          debug(2, "delay() information not (yet, hopefully!) available.");
                        } else {
                          debug(1, "delay() error %d: \"%s\".", -resp, strerror(-resp));
                        }
                        if (resp == sps_extra_code_output_stalled) {
                          if (config.unfixable_error_reported == 0) {
                            config.unfixable_error_reported = 1;
                            if (config.cmd_unfixable) {
                              command_execute(config.cmd_unfixable, "output_device_stalled", 1);
                            } else {
                              die("an unrecoverable error, \"output_device_stalled\", has been "
                                  "detected.");
                            }
                          }
                        } else {
                          debug(3, "Unexpected response to getting dac delay: %d.", resp);
                        }
                      }
                    }
                  }
                } else {
                  // if the output device doesn't have a delay, we simply send the lead-in
                  int64_t lead_time = conn->first_packet_time_to_play -
                                      get_absolute_time_in_ns(); // negative if we are late
                  void *silence;
                  int64_t frame_gap =
                      (lead_time * RATE_FROM_ENCODED_FORMAT(config.current_output_configuration)) /
                      1000000000;
                  // debug(1,"%d frames needed.",frame_gap);
                  while (frame_gap > 0) {
                    int64_t fs = RATE_FROM_ENCODED_FORMAT(config.current_output_configuration) / 10;

                    if (fs > frame_gap)
                      fs = frame_gap;

                    silence = malloc(
                        sps_format_sample_size(
                            FORMAT_FROM_ENCODED_FORMAT(config.current_output_configuration)) *
                        CHANNELS_FROM_ENCODED_FORMAT(config.current_output_configuration) * fs);
                    if (silence == NULL)
                      debug(1, "Failed to allocate %" PRId64 " frame silence buffer.", fs);
                    else {
                      // debug(1, "No delay function -- outputting %d frames of silence.", fs);
                      pthread_cleanup_push(malloc_cleanup, &silence);
                      conn->previous_random_number = generate_zero_frames(
                          silence, fs, conn->enable_dither, conn->previous_random_number,
                          config.current_output_configuration);
                      config.output->play(silence, fs, play_samples_are_untimed, 0, 0);
                      pthread_cleanup_pop(1); // deallocate silence
                    }
                    frame_gap -= fs;
                  }
                  conn->ab_buffering = 0;
                }
              }
#ifdef CONFIG_METADATA
              if (conn->ab_buffering == 0) {
                if ((curframe) && (curframe->ready) && (curframe->timestamp))
                  debug(3, "Current frame timestamp at \"resume\" is %u.", curframe->timestamp);
                else
                  debug(1, "Current frame at \"resume\" is not known.");

                send_ssnc_metadata('prsm', NULL, 0,
                                   0); // "resume", but don't wait if the queue is locked
              }
#endif
            }
          }
        }
      } else {
        // if (conn->ab_synced)
        // debug(1, "no buffers available at seqno %u.", conn->ab_read);
      }

      // Here, we work out whether to release a packet or wait
      // We release a packet when the time is right.

      // To work out when the time is right, we need to take account of (1) the actual time the
      // packet should be released, (2) the latency requested, (3) the audio backend latency offset
      // and (4) the desired length of the audio backend's buffer

      // The time is right if the current time is later or the same as
      // The packet time + (latency + latency offset - backend_buffer_length).
      // Note: the last three items are expressed in frames and must be converted to time.

      int do_wait = 0; // don't wait unless we can really prove we must
      if ((conn->ab_synced) && (curframe) && (curframe->ready) && (curframe->timestamp)) {
        do_wait = 1; // if the current frame exists and is ready, then wait unless it's time to let
                     // it go...

        // here, get the time to play the current frame.

        if (have_timestamp_timing_information(conn)) { // if we have a reference time

          uint64_t time_to_play;

          // we must enable packets to be released early enough for the
          // audio buffer to be filled to the desired length

          uint32_t desired_buffer_latency =
              (uint32_t)(config.audio_backend_buffer_desired_length * conn->input_rate);
          frame_to_local_time(curframe->timestamp - desired_buffer_latency, &time_to_play, conn);
          uint64_t current_buffer_delay = 0;
          int resp = -1;
          if (config.output->delay) {
            long l_delay;
            resp = config.output->delay(&l_delay);
            if (resp == 0) { // no error
              if (l_delay >= 0)
                current_buffer_delay = l_delay;
              else {
                debug(2, "Underrun of %ld frames reported, but ignored.", l_delay);
                current_buffer_delay =
                    0; // could get a negative value if there was underrun, but ignore it.
              }
            }
          }
          // If it's the first packet, or we don't have a working delay() function in the backend,
          // then wait until it's time to play it
          if ((((conn->first_packet_timestamp == curframe->timestamp) || (resp != 0)) &&
               (get_absolute_time_in_ns() >= time_to_play)) ||
              // Otherwise, if it isn't the first packet and we have a valid delay from the backend,
              // ensure the buffer stays nearly full
              ((conn->first_packet_timestamp != curframe->timestamp) && (resp == 0) &&
               (current_buffer_delay < desired_buffer_latency))) {
            do_wait = 0;
          }
          // here, do a sanity check. if the time_to_play is not within a few seconds of the
          // time now, the frame is probably not meant to be there, so let it go.
          if (do_wait != 0) {
            // this is a hack.
            // we subtract two 2^n unsigned numbers and get a signed 2^n result.
            // If we think of the calculation as occurring in modulo 2^n arithmetic
            // then the signed result's magnitude represents the shorter distance around
            // the modulo wheel of values from one number to the other.
            // The sign indicates the direction: positive means clockwise (upwards) from the
            // second number to the first (i.e. the first number comes "after" the second).

            int64_t time_difference = get_absolute_time_in_ns() - time_to_play;
            if ((time_difference > 10000000000) || (time_difference < -10000000000)) {
              debug(2,
                    "crazy time interval of %f seconds between time now: 0x%" PRIx64
                    " and time of packet: %" PRIx64 ".",
                    0.000000001 * time_difference, get_absolute_time_in_ns(), time_to_play);
              debug(2, "packet rtptime: %u, reference_timestamp: %u", curframe->timestamp,
                    conn->anchor_rtptime);

              do_wait = 0; // let it go
            }
          }
        }
      }
      if (do_wait == 0)
        // wait if the buffer is empty
        if ((conn->ab_synced != 0) && (conn->ab_read == conn->ab_write)) { // the buffer is empty!
          if (notified_buffer_empty == 0) {
            debug(2, "Connection %d: Buffer Empty", conn->connection_number);
            notified_buffer_empty = 1;
            // reset_input_flow_metrics(conn); // don't do a full flush parameters reset
            // conn->initial_reference_time = 0;
            // conn->initial_reference_timestamp = 0;
            // conn->first_packet_timestamp = 0; // make sure the first packet isn't late
          }
          do_wait = 1;
        }
      wait = (conn->ab_buffering || (do_wait != 0) || (!conn->ab_synced));
    } else {
      wait = 1; // keep waiting until the timing information becomes available
    }
    if (wait) {
      if (conn->frames_per_packet == 0)
        debug(1, "frames_per_packet is zero!");

      uint64_t time_to_wait_for_wakeup_ns = 20000000; // default if no input rate is set
      if (conn->input_rate != 0) {
        time_to_wait_for_wakeup_ns =
            1000000000 / conn->input_rate; // this is time period of one frame
        time_to_wait_for_wakeup_ns *= 4 * conn->frames_per_packet;
      }

#ifdef COMPILE_FOR_LINUX_AND_FREEBSD_AND_CYGWIN_AND_OPENBSD
      uint64_t time_of_wakeup_ns = get_realtime_in_ns() + time_to_wait_for_wakeup_ns;
      uint64_t sec = time_of_wakeup_ns / 1000000000;
      uint64_t nsec = time_of_wakeup_ns % 1000000000;

      struct timespec time_of_wakeup;
      time_of_wakeup.tv_sec = sec;
      time_of_wakeup.tv_nsec = nsec;

      int rc = pthread_cond_timedwait(&conn->flowcontrol, &conn->ab_mutex,
                                      &time_of_wakeup); // this is a pthread cancellation point
      if ((rc != 0) && (rc != ETIMEDOUT))
        // if (rc)
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
      curframe->timestamp = 0; // indicate a silent frame should be substituted
    }
    curframe->ready = 0;
  }
  conn->ab_read++;

  pthread_cleanup_pop(1); // unlock the ab_mutex
  pthread_cleanup_pop(1); // buffer_get_frame_cleanup_handler
  // debug(1, "Release frame %u.", curframe->timestamp);
#ifdef CONFIG_FFMPEG

#ifndef CONFIG_AIRPLAY_2
  if (config.decoder_in_use == 1 << decoder_ffmpeg_alac) {
#endif
    // clang-format off
    // If we're using the Hammerton or ALAC decoder, then curframe->data will 
    // point to a malloced buffer of the stereo interleaved LPCM/44100/S16/2 audio
    // But here, we must be using the FFMPEG decoder.
    // With the FFmpeg decoder we have an AVFrame in curframe->avframe.
    // The format could be anything -- it'll be transcoded here and placed in
    // malloc memory pointed to by curframe->data and the AVFrame will be freed.
    // If the avframe is NULL, then the length will be the number of frames of silence
    // to be inserted into the audio stream to replace an AVFrame of the same length
    // that is to be muted. Phew.
    // clang-format on

    if (curframe) {
      if (conn->resampler_ssrc != curframe->ssrc) {
        if (conn->resampler_ssrc == SSRC_NONE) {
          debug(2, "setting up software resampler for %s for the first time.",
                get_ssrc_name(curframe->ssrc));
        } else {
          debug(1, "Connection %d: queued audio buffers switching to \"%s\".", conn->connection_number,
                get_ssrc_name(curframe->ssrc));
          clear_software_resampler(conn);
          // ask the backend if it can give us its best choice for an ffmpeg configuration:
        }
        debug(3, "setup software resampler for %s", get_ssrc_name(curframe->ssrc));
        if (curframe->ssrc != SSRC_NONE) {
          setup_software_resampler(conn, curframe->ssrc);
        } else {
          debug(1, "attempt to setup_software_resampler for SSRC_NONE");
        }
      }
      size_t number_of_output_frames;
      uint8_t *pp;
      size_t pl;
      if (curframe->avframe) {
        conn->frames_retained_in_the_resampler =
            avframe_to_audio(conn, curframe->avframe, &pp, &pl, &number_of_output_frames);
        curframe->data = (short *)pp;
        curframe->length = number_of_output_frames;
        av_frame_free(&curframe->avframe);
        curframe->avframe = NULL;
      } else if (curframe->length != 0) {
        // if there's no data and no avframe, then the length is
        // the number of frames of silence requested.
        int ret = swr_inject_silence(conn->swr, curframe->length); // hardwired, ugh!
        if (ret)
          debug(1, "error %d", ret);
        // We need to get those frames of silence out of the resampler
        // so we'll pass in an empty AVFrame to flush them through
        AVFrame *avf = av_frame_alloc(); // empty frame
        conn->frames_retained_in_the_resampler =
            avframe_to_audio(conn, avf, &pp, &pl, &number_of_output_frames);
        curframe->data = (short *)pp;
        curframe->length = number_of_output_frames;
        av_frame_free(&avf);
      }
    }
#ifndef CONFIG_AIRPLAY_2
  }
#endif

#endif

#ifdef CONFIG_METADATA
  if ((curframe != NULL) && (conn->first_packet_timestamp) &&
      (conn->first_packet_timestamp == curframe->timestamp)) {
    char buffer[32];
    memset(buffer, 0, sizeof(buffer));
    // if this is not a resumption after a discontinuity,
    // say we have started receiving frames here
    if (curframe->timestamp_gap == 0) {
      snprintf(buffer, sizeof(buffer), "%" PRIu32 "/%" PRIu64 "", curframe->timestamp,
               conn->first_packet_time_to_play);
      send_ssnc_metadata('pffr', buffer, strlen(buffer),
                         0); // "first frame received", but don't wait if the queue is locked
      debug(3, "pffr: \"%s\"", buffer);
    } else {
      // otherwise, say a discontinuity occurred
      snprintf(buffer, sizeof(buffer), "%" PRIu32 "/%" PRId32 "", curframe->timestamp,
               curframe->timestamp_gap);
      send_ssnc_metadata(
          'pdis', buffer, strlen(buffer),
          0); // "a discontinuity of this many frames", but don't wait if the queue is locked
      debug(3, "pdis: \"%s\"", buffer);
    }
  }
#endif

  if (curframe) {
    // check sequencing
    if (conn->last_seqno_valid == 0) {
      conn->last_seqno_valid = 1;
      conn->last_seqno_read = curframe->sequence_number;
    } else {
      conn->last_seqno_read++;
      if (curframe->sequence_number != conn->last_seqno_read) {
        debug(1,
              "Player: packets out of sequence: expected: %u, got: %u, with ab_read: %u "
              "and ab_write: %u.",
              conn->last_seqno_read, curframe->sequence_number, conn->ab_read, conn->ab_write);
        conn->last_seqno_read = curframe->sequence_number; // reset warning...
      }
    }
  }

  return curframe;
}

static inline int32_t mean_32(int32_t a, int32_t b) {
  int64_t al = a;
  int64_t bl = b;
  int64_t mean = (al + bl) / 2;
  int32_t r = (int32_t)mean;
  if (r != mean)
    debug(1, "Error calculating average of two int32_t values: %d, %d.", a, b);
  return r;
}

// this takes an array of channels of signed 32-bit integers and
// (a) removes or inserts a frame as specified in "stuff",
// (b) multiplies each sample by the fixedvolume (a 16-bit quantity)
// (c) dithers the result to the output size 32/24/16/8 bits
// (d) outputs the result in the approprate format
// formats accepted include U8, S8, S16, S24, S24_3LE, S24_3BE and S32

// can only accept a plus or minus 1
// stuff: 1 means add 1; 0 means do nothing; -1 means remove 1
static int stuff_buffer_basic_32(int32_t *inptr, int length, sps_format_t l_output_format,
                                 char *outptr, int stuff, int dither, rtsp_conn_info *conn) {
  int tstuff = 0;
  if (length >= 3) {
    tstuff = stuff;
    if (tstuff)
      debug(3, "stuff_buffer_basic_32 %+d.", tstuff);
    char *l_outptr = outptr;
    if (stuff > 1)
      stuff = 1;
    if (stuff < -1)
      stuff = -1;
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
      unsigned int channel;
      for (channel = 0; channel < conn->input_num_channels; channel++)
        process_sample(*inptr++, &l_outptr, l_output_format, conn->fix_volume, dither, conn);
    };
    if (tstuff) {
      if (tstuff == 1) {
        // debug(3, "+++++++++");
        // interpolate one sample
        unsigned int channel;
        for (channel = 0; channel < conn->input_num_channels; channel++)
          process_sample(mean_32(inptr[-2], inptr[0]), &l_outptr, l_output_format, conn->fix_volume,
                         dither, conn);
      } else if (stuff == -1) {
        // debug(3, "---------");
        unsigned int channel;
        for (channel = 0; channel < conn->input_num_channels; channel++)
          inptr++;
      }

      // if you're removing, i.e. stuff < 0, copy that much less over. If you're adding, do all the
      // rest.
      int remainder = length;
      if (tstuff < 0)
        remainder = remainder + tstuff; // don't run over the correct end of the output buffer

      for (i = stuffsamp; i < remainder; i++) {
        unsigned int channel;
        for (channel = 0; channel < conn->input_num_channels; channel++)
          process_sample(*inptr++, &l_outptr, l_output_format, conn->fix_volume, dither, conn);
      }
    }
  }
  return length + tstuff;
}

// this takes an array of channels of n signed 32-bit integers and
// (a) replaces all of them with channels of n+stuff (+/-1) signed 32-bit integers,
// by first order interpolation.
// (b) multiplies each sample by the fixedvolume (a 16-bit quantity)
// (c) dithers the result to the output size 32/24/16/8 bits
// (d) outputs the result in the approprate format
// formats accepted include U8, S8, S16, S24, S24_3LE, S24_3BE and S32

// stuff: 1 means add 1; 0 means do nothing; -1 means remove 1

static int stuff_buffer_vernier(int32_t *inptr, int length, sps_format_t l_output_format,
                                char *outptr, int stuff, int dither, rtsp_conn_info *conn) {
  int tstuff = 0;
  if (length >= 3) {
    tstuff = stuff;
    if ((stuff > INTERPOLATION_LIMIT) || (stuff < -INTERPOLATION_LIMIT) || (length < 100)) {
      debug(2,
            "Stuff argument %d to stuff_buffer_vernier of length %d must be from -%d to +%d and "
            "length > 100.",
            stuff, length, INTERPOLATION_LIMIT, INTERPOLATION_LIMIT);
      tstuff = 0; // if any of these conditions hold, don't stuff anything/
    }

    char *l_outptr = outptr;
    int i;

    if (tstuff == 0) {
      for (i = 0; i < length; i++) { // the whole frame, if no stuffing
        unsigned int channel;
        for (channel = 0; channel < conn->input_num_channels; channel++)
          process_sample(*inptr++, &l_outptr, l_output_format, conn->fix_volume, dither, conn);
      }
    } else {
      // we are using 64 bit integers to represent fixed point numbers
      // the high 32 bits are the integer value and the low 32 bits are the fraction.
      int64_t one_fp = 0x100000000L;

      // this result will always be less than or equal to the exact true value.
      int64_t step_size_fp = one_fp * (length - 1);
      step_size_fp = step_size_fp / (length + tstuff - 1);

      // the interpolation is done between the previous sample, starting
      // with the zeroth sample, and the next one.
      // the very first and very last sample of the stuffed frame should
      // correspond 100% to the first and last samples of the original frame.

      // the first sample will be calculated as 100% of the first sample and 0% of the next sample
      // however, the last sample can not be calculated as 100% of the last sample and
      // 0% of the next one, because there isn't a "next" sample after the last one, duh.

      // however, rather than add extra code to deal with the last sample,
      // simply use a copy of the last sample as the "next" one, and the maths will work out.

      int64_t current_input_sample_index_fp = 0;
      for (i = 0; i < length + tstuff; i++) {
        int64_t current_input_sample_floor_index =
            current_input_sample_index_fp >> 32; // this is the (integer) index of the sample before
                                                 // where the new sample will be interpolated
        if (current_input_sample_floor_index == length) {
          // generate the whole and fractional parts of current_input_sample_index_fp for printing
          // without converting to floating point, which may do rounding.
          int64_t current_input_sample_index_int = current_input_sample_index_fp >> 32;
          int64_t current_input_sample_index_low = current_input_sample_index_fp & 0xFFFFFFFF;
          current_input_sample_index_low =
              current_input_sample_index_low * 100000; // 100000 for 5 decimal places
          current_input_sample_index_low = current_input_sample_index_low >> 32;
          debug(1,
                "Can't see how this could ever happen, but "
                "current_input_sample_floor_index %" PRId64
                " has just stepped outside the frame of %d samples, with stuff %d and current_input_sample_index_fp at %" PRId64 ".%05" PRId64
                ".",
                current_input_sample_floor_index, length, stuff, current_input_sample_index_int,
                current_input_sample_index_low);
          current_input_sample_floor_index = length - 1; // hack
        }

        // increment the ceiling index, but ensure it stays within the frame
        int64_t current_input_sample_ceil_index = current_input_sample_floor_index + 1;
        if (current_input_sample_ceil_index == length) {
          if (current_input_sample_floor_index == length - 1) {
            current_input_sample_ceil_index = length - 1;
          } else {
            // generate the whole and fractional parts of current_input_sample_index_fp for printing
            // without converting to floating point, which may do rounding.
            int64_t current_input_sample_index_int = current_input_sample_index_fp >> 32;
            int64_t current_input_sample_index_low = current_input_sample_index_fp & 0xFFFFFFFF;
            current_input_sample_index_low =
                current_input_sample_index_low * 100000; // 100000 for 5 decimal places
            current_input_sample_index_low = current_input_sample_index_low >> 32;
            debug(1,
                  "Can't see how this could ever happen, but "
                  "current_input_sample_ceil_index %" PRId64
                  " has just stepped outside the frame of %d samples, with stuff %d and current_input_sample_index_fp at %" PRId64
                  ".%05" PRId64 ".",
                  current_input_sample_floor_index, length, stuff, current_input_sample_index_int,
                  current_input_sample_index_low);
          }
        }

        /*
        {
          // generate the whole and fractional parts of current_input_sample_index_fp for printing
          // without converting to floating point, which may do rounding.
          int64_t current_input_sample_index_int = current_input_sample_index_fp >> 32;
          int64_t current_input_sample_index_low = current_input_sample_index_fp & 0xFFFFFFFF;
          current_input_sample_index_low =
              current_input_sample_index_low * 100000; // 100000 for 5 decimal places
          current_input_sample_index_low = current_input_sample_index_low >> 32;
          debug(1,
                "samples: %u, stuff: %d, output_sample: %d, current_input_sample_index_fp: %" PRId64
                ".%05" PRId64 ", floor: %" PRId64 ", ceil: %" PRId64 ".",
                length, stuff, i, current_input_sample_index_int, current_input_sample_index_low,
                current_input_sample_floor_index, current_input_sample_ceil_index);
        }
        */
        unsigned int channel;
        for (channel = 0; channel < conn->input_num_channels; channel++) {
          int32_t current_sample =
              inptr[current_input_sample_floor_index * conn->input_num_channels + channel];
          int32_t next_sample =
              inptr[current_input_sample_ceil_index * conn->input_num_channels + channel];
          int64_t current_sample_fp = current_sample;
          // current_sample_fp = current_sample_fp << 32;
          int64_t next_sample_fp = next_sample;
          // next_sample_fp = next_sample_fp << 32;
          int64_t offset_from_floor_fp = current_input_sample_index_fp & 0xffffffff;
          int64_t offset_to_ceil_fp = one_fp - offset_from_floor_fp;
          int64_t interpolated_sample_value_fp =
              current_sample_fp * offset_to_ceil_fp + next_sample_fp * offset_from_floor_fp;
          interpolated_sample_value_fp =
              interpolated_sample_value_fp / one_fp; // back to a 32-bit samplle
          int32_t interpolated_sample_value = interpolated_sample_value_fp;
          process_sample(interpolated_sample_value, &l_outptr, l_output_format, conn->fix_volume,
                         dither, conn);
        }
        current_input_sample_index_fp = current_input_sample_index_fp + step_size_fp;
      }
    }
  }
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
// formats accepted include U8, S8, S16, S24, S24_3LE, S24_3BE and S32

int32_t stat_n = 0;
double stat_mean = 0.0;
double stat_M2 = 0.0;
double longest_soxr_execution_time = 0.0;
int64_t packets_processed = 0;

int stuff_buffer_soxr_32(int32_t *inptr, int length, sps_format_t l_output_format, char *outptr,
                         int stuff, int dither, rtsp_conn_info *conn) {
  // if (scratchBuffer == NULL) {
  //  die("soxr scratchBuffer not initialised.");
  //}
  packets_processed++;
  int tstuff = stuff;
  if ((stuff > INTERPOLATION_LIMIT) || (stuff < -INTERPOLATION_LIMIT) || (length < 100)) {
    debug(2,
          "Stuff argument %d to stuff_buffer_soxr_32 of length %d must be from -%d to +%d and "
          "length > 100.",
          stuff, length, INTERPOLATION_LIMIT, INTERPOLATION_LIMIT);
    tstuff = 0; // if any of these conditions hold, don't stuff anything/
  }

  if (tstuff) {
    // debug(1, "stuff_buffer_soxr_32 %+d.",stuff);
    int32_t *scratchBuffer =
        malloc(sizeof(int32_t) * CHANNELS_FROM_ENCODED_FORMAT(config.current_output_configuration) *
               (length + tstuff));
    if (scratchBuffer != NULL) {
      soxr_io_spec_t io_spec;
      io_spec.itype = SOXR_INT32_I;
      io_spec.otype = SOXR_INT32_I;
      io_spec.scale = 1.0; // this seems to crash if not = 1.0
      io_spec.e = NULL;
      io_spec.flags = 0;

      size_t odone;

      uint64_t soxr_start_time = get_absolute_time_in_ns();

      soxr_error_t error =
          soxr_oneshot(length, length + tstuff, conn->input_num_channels, // Rates and # of chans.
                       inptr, length, NULL,                               // Input.
                       scratchBuffer, length + tstuff, &odone,            // Output.
                       &io_spec,    // Input, output and transfer spec.
                       NULL, NULL); // Default configuration.

      if (error)
        die("soxr error: %s\n", soxr_strerror(error));

      if (odone > (size_t)(length + INTERPOLATION_LIMIT))
        die("odone = %zu!\n", odone);

      // mean and variance calculations from "online_variance" algorithm at
      // https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance#Online_algorithm

      double soxr_execution_time = (get_absolute_time_in_ns() - soxr_start_time) * 0.000000001;
      // debug(1,"soxr_execution_time_us: %10.1f",soxr_execution_time_us);
      if (soxr_execution_time > longest_soxr_execution_time)
        longest_soxr_execution_time = soxr_execution_time;
      stat_n += 1;
      double stat_delta = soxr_execution_time - stat_mean;
      if (stat_n != 0)
        stat_mean += stat_delta / stat_n;
      else
        warn("calculation error for stat_n");
      stat_M2 += stat_delta * (soxr_execution_time - stat_mean);

      int i;
      int32_t *ip, *op;
      ip = inptr;
      op = scratchBuffer;

      const int gpm = 5;
      // keep the first (dpm) samples, to mitigate the Gibbs phenomenon
      for (i = 0; i < gpm; i++) {
        unsigned int channel;
        for (channel = 0; channel < conn->input_num_channels; channel++)
          *op++ = *ip++;
      }

      // keep the last (dpm) samples, to mitigate the Gibbs phenomenon

      // pointer arithmetic, baby -- it's da bomb.
      op = scratchBuffer + (length + tstuff - gpm) * conn->input_num_channels;
      ip = inptr + (length - gpm) * conn->input_num_channels;
      for (i = 0; i < gpm; i++) {
        unsigned int channel;
        for (channel = 0; channel < conn->input_num_channels; channel++)
          *op++ = *ip++;
      }

      // now, do the volume, dither and formatting processing
      ip = scratchBuffer;
      char *l_outptr = outptr;
      for (i = 0; i < length + tstuff; i++) {
        unsigned int channel;
        for (channel = 0; channel < conn->input_num_channels; channel++)
          process_sample(*ip++, &l_outptr, l_output_format, conn->fix_volume, dither, conn);
      };
      free(scratchBuffer);
    } else {
      debug(1, "Cannot allocate scratchbuffer");
    }
  } else { // the whole frame, if no stuffing

    // now, do the volume, dither and formatting processing
    int32_t *ip = inptr;
    char *l_outptr = outptr;
    int i;

    for (i = 0; i < length; i++) {
      unsigned int channel;
      for (channel = 0; channel < conn->input_num_channels; channel++)
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

  return length + tstuff;
}
#endif

char line_of_stats[1024];
int statistics_row; // statistics_line 0 means print the headings; anything else 1 means print the
                    // values. Set to 0 the first time out.
int statistics_column; // used to index through the statistics_print_profile array to check if it
                       // should be printed
int was_a_previous_column;
int *statistics_print_profile;

// these arrays specify which of the statistics specified by the statistics_item calls will actually
// be printed -- 2 means print, 1 means print only in a debug mode, 0 means skip

// clang-format off
int ap1_synced_statistics_print_profile[] =                  {2, 1, 2, 2, 0, 2, 1, 1, 2, 1, 1, 1, 0, 1, 1, 2, 2};
int ap1_nosync_statistics_print_profile[] =                  {2, 0, 0, 0, 0, 2, 1, 1, 2, 1, 1, 1, 0, 1, 1, 0, 0};
int ap1_nodelay_statistics_print_profile[] =                 {0, 0, 0, 0, 0, 2, 1, 1, 2, 0, 1, 1, 0, 1, 1, 0, 0};

int ap2_realtime_synced_stream_statistics_print_profile[] =  {2, 1, 2, 2, 0, 2, 1, 1, 2, 1, 1, 1, 0, 0, 1, 2, 2};
int ap2_realtime_nosync_stream_statistics_print_profile[] =  {2, 0, 0, 0, 0, 2, 1, 1, 2, 1, 1, 1, 0, 0, 1, 0, 0};
int ap2_realtime_nodelay_stream_statistics_print_profile[] = {0, 0, 0, 0, 0, 2, 1, 1, 2, 0, 1, 1, 0, 0, 1, 0, 0};

int ap2_buffered_synced_stream_statistics_print_profile[] =  {2, 2, 2, 1, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0, 0, 2, 2};
int ap2_buffered_nosync_stream_statistics_print_profile[] =  {2, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0};
int ap2_buffered_nodelay_stream_statistics_print_profile[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0};
// clang-format on

void statistics_item(const char *heading, const char *format, ...) {
  if (((statistics_print_profile[statistics_column] == 1) && (debug_level() != 0)) ||
      (statistics_print_profile[statistics_column] == 2)) { // include this column?
    if (was_a_previous_column != 0) {
      if (statistics_row == 0)
        strcat(line_of_stats, " | ");
      else
        strcat(line_of_stats, "   ");
    }
    if (statistics_row == 0) {
      strcat(line_of_stats, heading);
    } else {
      char b[1024];
      b[0] = 0;
      va_list args;
      va_start(args, format);
      vsnprintf(b, sizeof(b), format, args);
      va_end(args);
      strcat(line_of_stats, b);
    }
    was_a_previous_column = 1;
  }
  statistics_column++;
}

double suggested_volume(rtsp_conn_info *conn) {
  double response = config.airplay_volume;
  if ((conn != NULL) && (conn->own_airplay_volume_set != 0)) {
    response = conn->own_airplay_volume;
  }
  return response;
}

#ifdef CONFIG_METADATA
void send_ssnc_stream_description(const char *type, const char *description) {
  send_ssnc_metadata('styp', type, strlen(type), 1);
  send_ssnc_metadata('sdsc', description, strlen(description), 1);
}
#endif

void player_thread_cleanup_handler(void *arg) {
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  // debug(1, "Connection %d: player_thread_cleanup_handler start.", conn->connection_number);

#ifdef CONFIG_FFMPEG
  // debug(1, "FFmpeg clearup");
  clear_software_resampler(conn);
  clear_decoding_chain(conn);
  // debug(1, "FFmpeg clearup done");
#endif

  if (config.output->stop) {
#ifdef CONFIG_FFMPEG
    if (avflush(conn) > 1)
      debug(3, "ffmpeg flush at stop!");
#endif
    debug(2, "Connection %d: player: stop the output backend.", conn->connection_number);
    config.output->stop();
  }

  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  debug(3, "Connection %d: player thread main loop exit via player_thread_cleanup_handler.",
        conn->connection_number);

  if ((conn->at_least_one_frame_seen_this_session != 0) && (config.statistics_requested)) {
    int64_t time_playing = get_absolute_time_in_ns() - conn->playstart;
    time_playing = time_playing / 1000000000;
    int64_t elapsedHours = time_playing / 3600;
    int64_t elapsedMin = (time_playing / 60) % 60;
    int64_t elapsedSec = time_playing % 60;
    if (conn->frame_rate_valid)
      inform("Connection %d: Playback stopped. Total playing time %02" PRId64 ":%02" PRId64
             ":%02" PRId64 ". "
             "Output: %0.2f (raw), %0.2f (corrected) "
             "frames per second.",
             conn->connection_number, elapsedHours, elapsedMin, elapsedSec, conn->raw_frame_rate,
             conn->corrected_frame_rate);
    else
      inform("Connection %d: Playback stopped. Total playing time %02" PRId64 ":%02" PRId64
             ":%02" PRId64 ".",
             conn->connection_number, elapsedHours, elapsedMin, elapsedSec);
  }

#ifdef CONFIG_DACP_CLIENT
  relinquish_dacp_server_information(
      conn); // say it doesn't belong to this conversation thread any more...
#else
  mdns_dacp_monitor_set_id(NULL); // say we're not interested in following that DACP id any more
#endif

  // four possibilities
  // 1 -- Classic Airplay -- "AirPlay 1"
  // 2 -- AirPlay 2 in Classic Airplay mode
  // 3 -- AirPlay 2 in Buffered Audio Mode
  // 4 -- AirPlay 3 in Realtime Audio Mode.

#ifdef CONFIG_AIRPLAY_2
  if (conn->airplay_type == ap_2) {
    debug(2, "Cancelling AP2 timing, control and audio threads...");
    if (conn->airplay_stream_type == realtime_stream) {
      debug(2, "Connection %d: Delete Realtime Audio Stream thread", conn->connection_number);
      pthread_cancel(conn->rtp_realtime_audio_thread);
      pthread_join(conn->rtp_realtime_audio_thread, NULL);

    } else if (conn->airplay_stream_type == buffered_stream) {

      debug(3,
            "Connection %d: Delete Buffered Audio Stream thread by player_thread_cleanup_handler",
            conn->connection_number);
      pthread_cancel(conn->rtp_buffered_audio_thread);
      pthread_join(conn->rtp_buffered_audio_thread, NULL);
      debug(3,
            "Connection %d: Deleted Buffered Audio Stream thread by player_thread_cleanup_handler",
            conn->connection_number);
    } else {
      die("Unrecognised Stream Type");
    }

    debug(2, "Connection %d: Delete AirPlay 2 Control thread", conn->connection_number);
    pthread_cancel(conn->rtp_ap2_control_thread);
    pthread_join(conn->rtp_ap2_control_thread, NULL);
  } else {
    debug(2, "Cancelling AP1-compatible timing, control and audio threads...");
#else
  debug(2, "Cancelling AP1 timing, control and audio threads...");
#endif
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

#ifdef CONFIG_AIRPLAY_2
  }
  ptp_send_control_message_string("E");
#endif

  if (conn->outbuf) {
    free(conn->outbuf);
    conn->outbuf = NULL;
  }
  if (conn->tbuf) {
    free(conn->tbuf);
    conn->tbuf = NULL;
  }

  free_audio_buffers(conn);
  if (conn->stream.type == ast_apple_lossless) {
#ifdef CONFIG_APPLE_ALAC
    if (config.decoder_in_use == 1 << decoder_apple_alac) {
      apple_alac_terminate();
    }
#endif

#ifdef CONFIG_HAMMERTON
    if (config.decoder_in_use == 1 << decoder_hammerton) {
      alac_free(conn->decoder_info);
    }
#endif
  }

  conn->rtp_running = 0;

  pthread_setcancelstate(oldState, NULL);
  debug(2, "Connection %d: player terminated.", conn->connection_number);
}

void *player_thread_func(void *arg) {
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  // if (config.output->prepare)
  // config.output->prepare(); // give the backend its first chance to prepare itself, knowing it
  // has access to the output device (i.e. knowing that it should not be in use by another program
  // at this time).
#ifdef CONFIG_METADATA
  uint64_t time_of_last_metadata_progress_update =
      0; // the assignment is to stop a compiler warning...
#endif

#ifdef CONFIG_CONVOLUTION
  double highest_convolver_output_db = 0.0;
#endif

  uint64_t previous_frames_played = 0; // initialised to avoid a "possibly uninitialised" warning
  uint64_t previous_raw_measurement_time =
      0; // initialised to avoid a "possibly uninitialised" warning
  uint64_t previous_corrected_measurement_time =
      0; // initialised to avoid a "possibly uninitialised" warning
  int previous_frames_played_valid = 0;

  conn->latency_warning_issued =
      0; // be permitted to generate a warning each time a play is attempted
  conn->packet_count = 0;
  conn->packet_count_since_flush = 0;
  conn->previous_random_number = 0;
  conn->decoder_in_use = 0;
  conn->ab_buffering = 1;
  conn->ab_synced = 0;
  conn->first_packet_timestamp = 0;
  conn->flush_requested = 0;
  conn->flush_output_flushed = 0; // only send a flush command to the output device once
  conn->flush_rtp_timestamp = 0;  // it seems this number has a special significance -- it seems to
                                  // be used as a null operand, so we'll use it like that too
  conn->fix_volume = 0x10000;
  conn->frames_per_packet = 352; // for ALAC -- will be changed if necessary

#ifdef CONFIG_AIRPLAY_2
  conn->ap2_rate = 0;
  conn->ap2_play_enabled = 0;

  unsigned int f = 0;
  for (f = 0; f < MAX_DEFERRED_FLUSH_REQUESTS; f++) {
    conn->ap2_deferred_flush_requests[f].inUse = 0;
    conn->ap2_deferred_flush_requests[f].active = 0;
  }
#endif

  const unsigned int sync_history_length = 40;
  int64_t sync_samples[sync_history_length];
  int64_t sync_samples_highest_error = 0;
  int64_t sync_samples_lowest_error = 0;
  int64_t sync_samples_second_highest_error;
  int64_t sync_samples_second_lowest_error;
  conn->sync_samples_index = 0;
  conn->sync_samples_count = 0;

  if (conn->stream.type == ast_apple_lossless) {
#ifdef CONFIG_HAMMERTON
    if (config.decoder_in_use == 1 << decoder_hammerton) {
      init_alac_decoder((int32_t *)&conn->stream.fmtp,
                        conn); // this sets up incoming rate, bit depth, channels.
                               // No pthread cancellation point in here
    }
#endif
#ifdef CONFIG_APPLE_ALAC
    if (config.decoder_in_use == 1 << decoder_apple_alac) {
      apple_alac_init(conn->stream.fmtp); // no pthread cancellation point in here
    }
#endif
  }
  // This must be after init_alac_decoder
  init_buffer(conn); // will need a corresponding deallocation. No cancellation points in here
  ab_resync(conn);

  if (conn->stream.encrypted) {
#ifdef CONFIG_MBEDTLS
    memset(&conn->dctx, 0, sizeof(mbedtls_aes_context));
    mbedtls_aes_setkey_dec(&conn->dctx, conn->stream.aeskey, 128);
#endif

#ifdef CONFIG_POLARSSL
    memset(&conn->dctx, 0, sizeof(aes_context));
    aes_setkey_dec(&conn->dctx, conn->stream.aeskey, 128);
#endif
  }

  conn->session_corrections = 0;
  conn->connection_state_to_output = get_requested_connection_state_to_output();

  int number_of_statistics, oldest_statistic, newest_statistic;
  uint32_t frames_since_last_stats_logged = 0;
  int at_least_one_frame_seen = 0;
  int64_t tsum_of_sync_errors, tsum_of_corrections, tsum_of_insertions_and_deletions;
  size_t tsum_of_frames;
  minimum_dac_queue_size = UINT64_MAX;
  int64_t tsum_of_gaps;
  int32_t minimum_buffer_occupancy = INT32_MAX;
  int32_t maximum_buffer_occupancy = INT32_MIN;

#ifdef CONFIG_AIRPLAY_2
  conn->ap2_audio_buffer_minimum_size = -1;
#endif

  conn->at_least_one_frame_seen_this_session = 0;
  conn->raw_frame_rate = 0.0;
  conn->corrected_frame_rate = 0.0;
  conn->frame_rate_valid = 0;

  conn->input_frame_rate = 0.0;
  conn->input_frame_rate_starting_point_is_valid = 0;

  conn->buffer_occupancy = 0;

  int play_samples = 0;
  uint64_t current_delay;
  int play_number = 0;
  conn->play_number_after_flush = 0;
  conn->time_of_last_audio_packet = 0;
  // conn->shutdown_requested = 0;
  number_of_statistics = oldest_statistic = newest_statistic = 0;
  tsum_of_sync_errors = tsum_of_corrections = tsum_of_insertions_and_deletions = 0;
  tsum_of_frames = 0;
  tsum_of_gaps = 0;

  // I think it's useful to keep this prime to prevent it from falling into a pattern with some
  // other process.

  static char rnstate[256];
  initstate(time(NULL), rnstate, 256);

  // signed short *inbuf;
  int inbuflength;

  // remember, the output device may never have been initialised prior to this call
#ifdef CONFIG_FFMPEG
  if (avflush(conn) > 1)
    debug(1, "ffmpeg flush at start!");
#endif

  // leave this relic -- jack and soundio still use it
  if (config.output->start != NULL)
    config.output->start(44100, SPS_FORMAT_S16_LE);

  conn->first_packet_timestamp = 0;
  conn->missing_packets = conn->late_packets = conn->too_late_packets = conn->resend_requests = 0;
  int sync_error_out_of_bounds =
      0; // number of times in a row that there's been a serious sync error

  // conn->statistics = malloc(sizeof(stats_t) * trend_samples);
  // if (conn->statistics == NULL)
  //   die("Failed to allocate a statistics buffer");

  conn->framesProcessedInThisEpoch = 0;
  conn->framesGeneratedInThisEpoch = 0;
  conn->correctionsRequestedInThisEpoch = 0;
  statistics_row = 0; // statistics_line 0 means print the headings; anything else 1 means print the
                      // values. Set to 0 the first time out.

  // decide on what statistics profile to use, if requested
#ifdef CONFIG_AIRPLAY_2
  if (conn->airplay_type == ap_2) {
    if (conn->airplay_stream_type == realtime_stream) {
      if (config.output->delay) {
        // if (config.no_sync == 0)
        statistics_print_profile = ap2_realtime_synced_stream_statistics_print_profile;
        // else
        // statistics_print_profile = ap2_realtime_nosync_stream_statistics_print_profile;
      } else {
        statistics_print_profile = ap2_realtime_nodelay_stream_statistics_print_profile;
      }
    } else {
      if (config.output->delay) {
        // if (config.no_sync == 0)
        statistics_print_profile = ap2_buffered_synced_stream_statistics_print_profile;
        // else
        //   statistics_print_profile = ap2_buffered_nosync_stream_statistics_print_profile;
      } else {
        statistics_print_profile = ap2_buffered_nodelay_stream_statistics_print_profile;
      }
    }
  } else {
#endif
    if (config.output->delay) {
      // if (config.no_sync == 0)
      statistics_print_profile = ap1_synced_statistics_print_profile;
      // else
      //   statistics_print_profile = ap1_nosync_statistics_print_profile;
    } else {
      statistics_print_profile = ap1_nodelay_statistics_print_profile;
    }
// airplay 1 stuff here
#ifdef CONFIG_AIRPLAY_2
  }
#endif

#ifdef CONFIG_AIRPLAY_2
  if (conn->timing_type == ts_ntp) {
#endif

    // create and start the timing, control and audio receiver threads
    named_pthread_create(&conn->rtp_audio_thread, NULL, &rtp_audio_receiver, (void *)conn,
                         "ap1_audio_%d", conn->connection_number);
    named_pthread_create(&conn->rtp_control_thread, NULL, &rtp_control_receiver, (void *)conn,
                         "ap1_control_%d", conn->connection_number);
    named_pthread_create(&conn->rtp_timing_thread, NULL, &rtp_timing_receiver, (void *)conn,
                         "ap1_tim_rcv_%d", conn->connection_number);

#ifdef CONFIG_AIRPLAY_2
  }
#endif

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

  // if not already set, set the volume to the pending_airplay_volume, if any, or otherwise to the
  // suggested volume.

  double initial_volume = suggested_volume(conn);
  debug(2, "Set initial volume to %.6f.", initial_volume);
  player_volume(initial_volume, conn); // will contain a cancellation point if asked to wait

  debug(2, "Play begin");

#ifdef CONFIG_FFMPEG
  int64_t frames_previously_retained_in_the_resampler = 0;
#endif

  // uint32_t flush_to_frame;
  // int enable_flush_to_frame = 0;
  int request_resync = 0;      // will be set if a big discontinuity is detected
  uint32_t frames_to_skip = 0; // when a discontinuity is registered
  int skipping_frames_at_start_of_play = 0;
  // debug(1, "player begin processing packets");
  while (1) {

#ifdef CONFIG_METADATA
    int this_is_the_first_frame = 0; // will be set if it is
#endif

    pthread_testcancel(); // allow a pthread_cancel request to take effect.

    // if we are using the software attenuator or downsampling or mixing to mono, enable dithering

    if ((conn->fix_volume != 0x10000) || // if not 0x10000, it is attenuating...
        ((conn->output_bit_depth > 0) &&
         (conn->input_effective_bit_depth > conn->output_bit_depth)) ||
        (config.playback_mode == ST_mono)) {
      if (conn->enable_dither == 0)
        debug(2, "enabling dither");
      conn->enable_dither = 1;
    } else {
      if (conn->enable_dither != 0)
        debug(2, "disabling dither");
      conn->enable_dither = 0;
    }

    abuf_t *inframe = buffer_get_frame(
        conn, request_resync); // this has a guaranteed [and needed!] cancellation point
    request_resync = 0;
    if (inframe) {
      if (inframe->data != NULL) {
        /*
        {
          uint64_t the_time_this_frame_should_be_played;
                    frame_to_local_time(inframe->timestamp,
                                        &the_time_this_frame_should_be_played, conn);
          int64_t lead_time = the_time_this_frame_should_be_played - get_absolute_time_in_ns();
          debug(1, "get_packet %u, lead time is %3.f ms.", inframe->timestamp, lead_time *
        0.000001);
        }
        */
        unsigned int last_sample_index;
        int frames_played = 0;
        int64_t sync_error = 0;
        int amount_to_stuff = 0;
        if (inframe->data) {
          if (play_number == 0)
            conn->playstart = get_absolute_time_in_ns();
          play_number++;
          //        if (play_number % 100 == 0)
          //          debug(3, "Play frame %d.", play_number);
          conn->play_number_after_flush++;

          if (inframe->timestamp == 0) {
            debug(2,
                  "Player has supplied a silent frame, (possibly frame %u) for play number %d, "
                  "status 0x%X after %u resend requests.",
                  conn->last_seqno_read + 1, play_number, inframe->status,
                  inframe->resend_request_number);
            conn->last_seqno_read++; // manage the packet out of sequence minder

            void *silence =
                malloc(sps_format_sample_size(
                           FORMAT_FROM_ENCODED_FORMAT(config.current_output_configuration)) *
                       CHANNELS_FROM_ENCODED_FORMAT(config.current_output_configuration) *
                       conn->frames_per_packet);
            if (silence == NULL) {
              debug(1, "Failed to allocate memory for a silent frame silence buffer.");
            } else {
              // the player may change the contents of the buffer, so it has to be zeroed each
              // time; might as well malloc and free it locally
              conn->previous_random_number = generate_zero_frames(
                  silence, conn->frames_per_packet, conn->enable_dither,
                  conn->previous_random_number, config.current_output_configuration);
              config.output->play(silence, conn->frames_per_packet, play_samples_are_untimed, 0, 0);
              free(silence);
              frames_played += conn->frames_per_packet;
            }
          } else {
            // process the frame
            // here, let's transform the frame of data, if necessary
            // we need an intermediate "transition" buffer

            if (conn->tbuf != NULL) {
              debug(1, "conn->tbuf not free'd");
              free(conn->tbuf);
            }
            conn->tbuf =
                malloc(sizeof(int32_t) *
                       CHANNELS_FROM_ENCODED_FORMAT(config.current_output_configuration) *
                       ((inframe->length) * conn->output_sample_ratio + INTERPOLATION_LIMIT));
            if (conn->tbuf == NULL)
              die("Failed to allocate memory for the transition buffer.");
            // size change
            conn->outbuf =
                malloc(sps_format_sample_size(
                           FORMAT_FROM_ENCODED_FORMAT(config.current_output_configuration)) *
                       CHANNELS_FROM_ENCODED_FORMAT(config.current_output_configuration) *
                       ((inframe->length) * conn->output_sample_ratio + INTERPOLATION_LIMIT));
            if (conn->outbuf == NULL)
              die("Failed to allocate memory for an output buffer.");

            if (conn->input_num_channels == 2) {
              // if (0) {

              switch (conn->input_bit_depth) {
              case 16: {
                unsigned int i, j;
                int16_t ls, rs;
                int32_t ll = 0, rl = 0;
                int16_t *inps = inframe->data;
                // int16_t *outps = tbuf;
                int32_t *outpl = (int32_t *)conn->tbuf;
                for (i = 0; i < (inframe->length); i++) {
                  ls = *inps++;
                  rs = *inps++;

                  // here, do the mode stuff -- mono / reverse stereo / leftonly / rightonly
                  // also, raise the 16-bit samples to 32 bits.

                  switch (config.playback_mode) {
                  case ST_mono: {
                    int32_t lsl = ls;
                    int32_t rsl = rs;
                    int32_t both = lsl + rsl;
                    both =
                        both
                        << (16 -
                            1); // keep all 17 bits of the sum of the 16 bit left and right channels
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
              case 32: {
                unsigned int i, j;
                int32_t ls, rs;
                int32_t ll = 0, rl = 0;
                int32_t *inps = (int32_t *)inframe->data;
                int32_t *outpl = (int32_t *)conn->tbuf;
                for (i = 0; i < (inframe->length); i++) {
                  ls = *inps++;
                  rs = *inps++;

                  // here, do the mode stuff -- mono / reverse stereo / leftonly / rightonly

                  switch (config.playback_mode) {
                  case ST_mono: {
                    int64_t lsl = ls;
                    int64_t rsl = rs;
                    int64_t both = lsl + rsl;
                    both = both >> 1;
                    uint32_t both32 = both;
                    ll = both32;
                    rl = both32;
                  } break;
                  case ST_reverse_stereo: {
                    ll = rs;
                    rl = ls;
                  } break;
                  case ST_left_only:
                    rl = ls;
                    ll = ls;
                    break;
                  case ST_right_only:
                    ll = rs;
                    rl = rs;
                    break;
                  case ST_stereo:
                    ll = ls;
                    rl = rs;
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
                die("Shairport Sync only supports 16 or 32 bit input (stereo)");
              }
            } else {
              // multichannel -- don't do anything odd here
              if (conn->input_bit_depth == 16) {
                unsigned int i;
                int16_t ss;
                int32_t sl;
                int16_t *inps = inframe->data;
                int32_t *outpl = (int32_t *)conn->tbuf;
                for (i = 0; i < (inframe->length) * conn->input_num_channels; i++) {
                  ss = *inps++;
                  sl = ss;
                  sl = sl << 16;
                  unsigned int j;
                  for (j = 0; j < conn->output_sample_ratio; j++) {
                    *outpl++ = sl;
                  }
                }
              } else if (conn->input_bit_depth == 32) {
                unsigned int i;
                int32_t *inpl = (int32_t *)inframe->data;
                int32_t *outpl = (int32_t *)conn->tbuf;
                for (i = 0; i < (inframe->length) * conn->input_num_channels; i++) {
                  int32_t sl = *inpl++;
                  unsigned int j;
                  for (j = 0; j < conn->output_sample_ratio; j++) {
                    *outpl++ = sl;
                  }
                }
              } else {
                die("Shairport Sync only supports 16 or 32 bit input (multichannel)");
              }
            }

            inbuflength = (inframe->length) * conn->output_sample_ratio;

            // We have a frame of data. We need to see if we want to add or remove a frame from
            // it to keep in sync. So we calculate the timing error for the first frame in the
            // DAC. If it's ahead of time, we add one audio frame to this frame to delay a
            // subsequent frame If it's late, we remove an audio frame from this frame to bring
            // a subsequent frame forward in time

            // now, go back as far as the total latency less, say, 100 ms, and check the
            // presence of frames from then onwards

            at_least_one_frame_seen = 1;

            int16_t bo = conn->ab_write - conn->ab_read; // do this in 16 bits
            conn->buffer_occupancy = bo;                 // 32 bits

            if (conn->buffer_occupancy < minimum_buffer_occupancy)
              minimum_buffer_occupancy = conn->buffer_occupancy;

            if (conn->buffer_occupancy > maximum_buffer_occupancy)
              maximum_buffer_occupancy = conn->buffer_occupancy;

            // now, before outputting anything to the output device, check the stats

            uint32_t stats_logging_interval_in_frames =
                8 * RATE_FROM_ENCODED_FORMAT(config.current_output_configuration);
            if ((stats_logging_interval_in_frames != 0) &&
                (frames_since_last_stats_logged > stats_logging_interval_in_frames)) {

              // here, calculate the input and output frame rates, where possible, even if
              // statistics have not been requested this is to calculate them in case they are
              // needed by the D-Bus interface or elsewhere.

              if (conn->input_frame_rate_starting_point_is_valid) {
                uint64_t elapsed_reception_time, frames_received;
                elapsed_reception_time = conn->frames_inward_measurement_time -
                                         conn->frames_inward_measurement_start_time;
                frames_received = conn->frames_inward_frames_received_at_measurement_time -
                                  conn->frames_inward_frames_received_at_measurement_start_time;
                conn->input_frame_rate = (1.0E9 * frames_received) /
                                         elapsed_reception_time; // an IEEE double calculation
                                                                 // with two 64-bit integers
              } else {
                conn->input_frame_rate = 0.0;
              }

              int stats_status = 0;
              if ((config.output->delay) && (config.output->stats)) {
                uint64_t frames_sent_for_play;
                uint64_t raw_measurement_time;
                uint64_t corrected_measurement_time;
                uint64_t actual_delay;
                stats_status =
                    config.output->stats(&raw_measurement_time, &corrected_measurement_time,
                                         &actual_delay, &frames_sent_for_play);
                // debug(1,"status: %d, actual_delay: %" PRIu64 ", frames_sent_for_play: %"
                // PRIu64
                // ", frames_played: %" PRIu64 ".", stats_status, actual_delay,
                // frames_sent_for_play, frames_sent_for_play - actual_delay);
                uint64_t frames_played_by_output_device = frames_sent_for_play - actual_delay;
                // If the status is zero, it means that there were no output problems since the
                // last time the stats call was made. Thus, the frame rate should be valid.
                if ((stats_status == 0) && (previous_frames_played_valid != 0)) {
                  uint64_t frames_played_in_this_interval =
                      frames_played_by_output_device - previous_frames_played;
                  int64_t raw_interval = raw_measurement_time - previous_raw_measurement_time;
                  int64_t corrected_interval =
                      corrected_measurement_time - previous_corrected_measurement_time;
                  if (raw_interval != 0) {
                    conn->raw_frame_rate = (1e9 * frames_played_in_this_interval) / raw_interval;
                    conn->corrected_frame_rate =
                        (1e9 * frames_played_in_this_interval) / corrected_interval;
                    conn->frame_rate_valid = 1;
                    // debug(1,"frames_played_in_this_interval: %" PRIu64 ", interval: %" PRId64
                    // ", rate: %f.",
                    //  frames_played_in_this_interval, interval, conn->frame_rate);
                  }
                }

                // uncomment the if statement if your want to get as long a period for
                // calculating the frame rate as possible without an output break or error
                if ((stats_status != 0) || (previous_frames_played_valid == 0)) {
                  // if we have just detected an outputting error, or if we have no
                  // starting information
                  if (stats_status != 0)
                    conn->frame_rate_valid = 0;
                  previous_frames_played = frames_played_by_output_device;
                  previous_raw_measurement_time = raw_measurement_time;
                  previous_corrected_measurement_time = corrected_measurement_time;
                  previous_frames_played_valid = 1;
                }
              }

              // we can now calculate running averages for sync error (frames), corrections
              // (ppm), insertions plus deletions (ppm)
              double average_sync_error = 0.0;
              double average_gap_ms = 0.0;
              double corrections_ppm = 0.0;
              double insertions_plus_deletions_ppm = 0.0;
              if (number_of_statistics == 0) {
                debug(1, "number_of_statistics is zero!");
              } else {
                average_sync_error =
                    (1000.0 * tsum_of_sync_errors) /
                    (number_of_statistics *
                     RATE_FROM_ENCODED_FORMAT(config.current_output_configuration));
                average_gap_ms = ((1.0 * tsum_of_gaps) / number_of_statistics) * 0.000001;
                if (tsum_of_frames != 0) {
                  corrections_ppm = (1000000.0 * tsum_of_corrections) / tsum_of_frames;
                  insertions_plus_deletions_ppm =
                      (1000000.0 * tsum_of_insertions_and_deletions) / tsum_of_frames;
                } else {
                  debug(3, "tsum_of_frames: %zu.", tsum_of_frames);
                }
              }
              if (config.statistics_requested) {
                if (at_least_one_frame_seen) {
                  do {
                    line_of_stats[0] = '\0';
                    statistics_column = 0;
                    was_a_previous_column = 0;
                    statistics_item("Av Sync Error (ms)", "%*.2f", 18, average_sync_error);
                    statistics_item("Net Sync PPM", "%*.1f", 12, corrections_ppm);
                    statistics_item("All Sync PPM", "%*.1f", 12, insertions_plus_deletions_ppm);
                    statistics_item("Av Sync Window (ms)", "%*.2f", 19, average_gap_ms);
                    statistics_item("    Packets", "%*d", 11, play_number);
                    statistics_item("Missing", "%*" PRIu64 "", 7, conn->missing_packets);
                    statistics_item("  Late", "%*" PRIu64 "", 6, conn->late_packets);
                    statistics_item("Too Late", "%*" PRIu64 "", 8, conn->too_late_packets);
                    statistics_item("Resend Reqs", "%*" PRIu64 "", 11, conn->resend_requests);
                    statistics_item("Min DAC Queue", "%*" PRIu64 "", 13, minimum_dac_queue_size);
                    statistics_item("Min Buffers", "%*" PRIu32 "", 11, minimum_buffer_occupancy);
                    statistics_item("Max Buffers", "%*" PRIu32 "", 11, maximum_buffer_occupancy);
#ifdef CONFIG_AIRPLAY_2
                    if (conn->ap2_audio_buffer_minimum_size > 10 * 1024)
                      statistics_item("Min Buffer Size", "%*" PRIu32 "k", 14,
                                      conn->ap2_audio_buffer_minimum_size / 1024);
                    else
                      statistics_item("Min Buffer Size", "%*" PRIu32 "", 15,
                                      conn->ap2_audio_buffer_minimum_size);
#else
                    statistics_item("N/A", "   "); // dummy -- should never be visible
#endif
                    statistics_item("Nominal FPS", "%*.2f", 11, conn->remote_frame_rate);
                    statistics_item("Received FPS", "%*.2f", 12, conn->input_frame_rate);
                    // only make the next two columns appear if we are getting stats information
                    // from the back end
                    if (config.output->stats) {
                      if (conn->frame_rate_valid) {
                        statistics_item("Output FPS (r)", "%*.2f", 14, conn->raw_frame_rate);
                        statistics_item("Output FPS (c)", "%*.2f", 14, conn->corrected_frame_rate);
                      } else {
                        statistics_item("Output FPS (r)", "           N/A");
                        statistics_item("Output FPS (c)", "           N/A");
                      }
                    } else {
                      statistics_column = statistics_column + 2;
                    }
                    statistics_row++;
                    inform("%s", line_of_stats);
                  } while (statistics_row < 2);
                } else {
                  inform("No frames received in the last sampling interval.");
                }
              }
              tsum_of_sync_errors = 0;
              tsum_of_corrections = 0;
              tsum_of_insertions_and_deletions = 0;
              number_of_statistics = 0;
              tsum_of_frames = 0;
              tsum_of_gaps = 0;
              minimum_dac_queue_size = UINT64_MAX;  // hack reset
              maximum_buffer_occupancy = INT32_MIN; // can't be less than this
              minimum_buffer_occupancy = INT32_MAX; // can't be more than this
#ifdef CONFIG_AIRPLAY_2
              conn->ap2_audio_buffer_minimum_size = -1;
#endif
              at_least_one_frame_seen = 0;
              frames_since_last_stats_logged = 0;
            }

            if (conn->at_least_one_frame_seen_this_session == 0) {
              conn->at_least_one_frame_seen_this_session = 1;

#ifdef CONFIG_METADATA
              this_is_the_first_frame = 1;
#endif

              char short_description[256];
              snprintf(short_description, sizeof(short_description), "%u/%s/%u",
                       RATE_FROM_ENCODED_FORMAT(config.current_output_configuration),
                       sps_format_description_string(
                           FORMAT_FROM_ENCODED_FORMAT(config.current_output_configuration)),
                       CHANNELS_FROM_ENCODED_FORMAT(config.current_output_configuration));
              // since this is the first frame of audio, inform the user if requested...
#ifdef CONFIG_AIRPLAY_2
              if (conn->airplay_stream_type == realtime_stream) {
                if (conn->airplay_type == ap_1) {
#ifdef CONFIG_METADATA
                  send_ssnc_stream_description("Classic", get_ssrc_name(conn->incoming_ssrc));
#endif
                  if (config.statistics_requested)
                    inform("Connection %d: Classic AirPlay (\"AirPlay 1\") Compatible playback. "
                           "Input format: %s. Output format: %s.",
                           conn->connection_number, get_ssrc_name(conn->incoming_ssrc),
                           short_description);
                } else {
#ifdef CONFIG_METADATA
                  send_ssnc_stream_description("Realtime", get_ssrc_name(conn->incoming_ssrc));
#endif
                  if (config.statistics_requested) {
                    if (conn->ap2_client_name == NULL)
                      inform("Connection %d: AirPlay 2 Realtime playback. "
                             "Input format: %s. Output format: %s.",
                             conn->connection_number, get_ssrc_name(conn->incoming_ssrc), "");
                    else
                      inform("Connection %d: AirPlay 2 Realtime playback. "
                             "Source: \"%s\". Input format: %s. Output format: %s.",
                             conn->connection_number, conn->ap2_client_name,
                             get_ssrc_name(conn->incoming_ssrc), short_description);
                  }
                }
              } else {
#ifdef CONFIG_METADATA
                send_ssnc_stream_description("Buffered", get_ssrc_name(conn->incoming_ssrc));
#endif

                if (config.statistics_requested) {

                  if (conn->ap2_client_name == NULL)
                    inform("Connection %d: AirPlay 2 Buffered playback. "
                           "Input format: %s. Output format: %s.",
                           conn->connection_number, get_ssrc_name(conn->incoming_ssrc),
                           short_description);
                  else
                    inform("Connection %d: AirPlay 2 Buffered playback. "
                           "Source: \"%s\". Input format: %s. Output format: %s.",
                           conn->connection_number, conn->ap2_client_name,
                           get_ssrc_name(conn->incoming_ssrc), short_description);
                }
              }
#else
#ifdef CONFIG_METADATA
              send_ssnc_stream_description("AirPlay", "ALAC/44100/S16/2");
#endif
              if (config.statistics_requested)
                inform("Connection %d: Classic AirPlay (\"AirPlay 1\") playback. "
                       "Input format: ALAC/44100/S16/2. Output format: %s.",
                       conn->connection_number, short_description);
#endif

#ifdef CONFIG_METADATA
              send_ssnc_metadata('odsc', short_description, strlen(short_description), 1);
#endif
            }

            // here, we want to check (a) if we are meant to do synchronisation,
            // (b) if we have a delay procedure, (c) if we can get the delay.

            // If any of these are false, we don't do any synchronisation stuff

            int resp = -1; // use this as a flag -- if negative, we can't rely on a real known delay
            current_delay = -1; // use this as a failure flag

            // if making the measurements takes too long (e.g. due to scheduling) , don't use
            // it.

            uint64_t mst = get_absolute_time_in_ns(); // measurement start time
            uint64_t delay_measurement_time = 0;
            if (config.output->delay) {
              long l_delay;
              resp = config.output->delay(&l_delay);
              delay_measurement_time =
                  get_absolute_time_in_ns(); // put this after delay() returns, as it may take an
                                             // appreciable amount of time to run
              if (resp == 0) {               // no error
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
                    (config.unfixable_error_reported == 0)) {
                  config.unfixable_error_reported = 1;
                  if (config.cmd_unfixable) {
                    warn("Connection %d: An unfixable error has been detected -- output device "
                         "is "
                         "stalled. Executing the "
                         "\"run_this_if_an_unfixable_error_is_detected\" command.",
                         conn->connection_number);
                    command_execute(config.cmd_unfixable, "output_device_stalled", 1);
                  } else {
                    warn("Connection %d: An unfixable error has been detected -- output device "
                         "is "
                         "stalled. \"No "
                         "run_this_if_an_unfixable_error_is_detected\" command provided -- "
                         "nothing "
                         "done.",
                         conn->connection_number);
                  }
                } else {
                  if ((resp != -EBUSY) &&
                      (resp != -ENODEV)) // delay and not-there errors can be reported if the
                                         // device is (hopefully temporarily) busy or unavailable
                                         // Note: ENODATA (a better fit) is not availabe in FreeBSD.
                    debug(1, "Delay error %d when checking running latency.", resp);
                }
              }
              // debug(1, "resp is %d, delay is %ld.", resp, l_delay);
            }
            if (resp == 0) {

              uint64_t the_time_this_frame_should_be_played;
              frame_to_local_time(inframe->timestamp, &the_time_this_frame_should_be_played, conn);

              uint64_t output_buffer_delay_time = current_delay;

#ifdef CONFIG_FFMPEG
              // the current delay should also include the frames that were kept in swr
              // before the current block was requested
              output_buffer_delay_time =
                  output_buffer_delay_time + frames_previously_retained_in_the_resampler;
              // debug(1,"Allowing for %" PRId64 " frames previously held in the resampler.",
              // frames_previously_retained_in_the_resampler);
              // now we'll update frames_previously_retained_in_the_resampler
              // to the figure after the current block
              frames_previously_retained_in_the_resampler = conn->frames_retained_in_the_resampler;
#endif

              output_buffer_delay_time =
                  output_buffer_delay_time *
                  1000000000; // there should be plenty of space in a uint64_t for any
                              // conceivable current_delay value
              output_buffer_delay_time =
                  output_buffer_delay_time /
                  RATE_FROM_ENCODED_FORMAT(config.current_output_configuration);
              debug(3,
                    "current_delay: %" PRId64 ", output_buffer_delay_time: %.3f, output rate: %u.",
                    current_delay, output_buffer_delay_time * 0.000000001,
                    RATE_FROM_ENCODED_FORMAT(config.current_output_configuration));

              uint64_t the_time_this_frame_will_be_played =
                  delay_measurement_time + output_buffer_delay_time;

              double centered_sync_error_time = 0.0;
              int64_t sync_error_ns = 0;
              int64_t measurement_time = get_absolute_time_in_ns() - mst;

              // debug(1, "measurement time: %" PRId64 " ns.", measurement_time);

              if (measurement_time < 2000000) {

                sync_error_ns =
                    the_time_this_frame_will_be_played - the_time_this_frame_should_be_played;
                sync_error = (sync_error_ns *
                              RATE_FROM_ENCODED_FORMAT(config.current_output_configuration)) /
                             1000000000;

                // debug(1, "measurement time: %" PRId64 " ns. Sync error: %" PRId64 " ns, %" PRId64
                // " frames, skipping_frames_at_start_of_play is %d.", measurement_time,
                // sync_error_ns, sync_error, skipping_frames_at_start_of_play);

                // A timestamp gap is when the timstamp of the next packet of frames is not equal to
                // the previous packet's timstamp + number o frames therein.

                // But wait! If there is a timestamp gap, this isn't really an error.

                // Also, if it's a sync error at the start of a play sequence, then
                // it can be dealt with by inserting a silence or skipping frames.

                // So, here we have enough information to decide what to do with the "frame" of
                // audio.

                // If we are already skipping frames because of a prior first frame,
                // we might need to adjust the skipping count due to a better time estimate

                if (skipping_frames_at_start_of_play != 0) {
                  if (sync_error <= 0) {
                    debug(3,
                          "cancel skipping at start of play -- skip estimate was: %" PRId32
                          ", but sync_error is now: %" PRId64 ".",
                          frames_to_skip, sync_error);
                    frames_to_skip = 0;
                    skipping_frames_at_start_of_play = 0;
                  } else if (frames_to_skip != sync_error) {
                    debug(3,
                          "updating skipping at start of play count from: %" PRId32 " to: %" PRId64
                          ".",
                          frames_to_skip, sync_error);
                    frames_to_skip = sync_error;
                  }
                }

                // If it's the first frame or if it's at a timestamp discontinuity, then we can
                // deal with it straight away.
                if ((inframe != NULL) && ((conn->first_packet_timestamp == inframe->timestamp) ||
                                          (inframe->timestamp_gap != 0))) {

                  // By default, when there is a sync error and some kind of discontinuity,
                  // e.g. a gap between timestamps of adjacent packets or a first packet,
                  // then we try to fix the sync error, either by skipping frames or by inserting a
                  // silence. However, if it's a negative timestamp gap between packets, only try to
                  // fix the timestamp_gap. The reason for this is that we don't know the purpose of
                  // the negative gaps. For all we know, it may be that the audio frames before and
                  // after the gap are meant to be contiguous.

                  int64_t gap_to_fix = sync_error; // this is what we look at normally

                  if (conn->first_packet_timestamp == inframe->timestamp) {
                    debug(3, "first frame: %u, sync_error %" PRId64 " frames.", inframe->timestamp,
                          sync_error);
                    skipping_frames_at_start_of_play = 1;
                  } else {
                    debug(3, "timestamp_gap: %d on frame %u, sync_error %" PRId64 " frames.",
                          inframe->timestamp_gap, inframe->timestamp, sync_error);
                    if (inframe->timestamp_gap < 0) {
                      gap_to_fix = -inframe->timestamp_gap; // this is frames at the input rate
                      int64_t gap_to_fix_ns = (gap_to_fix * 1000000000) / conn->input_rate;
                      gap_to_fix = (gap_to_fix_ns *
                                    RATE_FROM_ENCODED_FORMAT(config.current_output_configuration)) /
                                   1000000000; // this is frames at the output rate
                      // debug(3, "due to timstamp gap of %d frames, skip %" PRId64 " output
                      // frames.", inframe->timestamp_gap, gap_to_fix);
                    }
                  }

                  if (gap_to_fix > 0) {
                    // debug(1, "drop %u frames, timestamp: %u, skipping_frames_at_start_of_play is
                    // %d.", gap_to_fix, inframe->timestamp, skipping_frames_at_start_of_play);
                    frames_to_skip += gap_to_fix;
                    sync_error_ns = 0;         // don't invoke any sync checking
                  } else if (gap_to_fix < 0) { // this packet is early, so insert the right number
                                               // of frames to zero the error
                    frames_to_skip = 0;
                    skipping_frames_at_start_of_play = 0;
                    int64_t gap = -gap_to_fix;
                    void *silence = malloc(
                        sps_format_sample_size(
                            FORMAT_FROM_ENCODED_FORMAT(config.current_output_configuration)) *
                        CHANNELS_FROM_ENCODED_FORMAT(config.current_output_configuration) * gap);
                    if (silence == NULL) {
                      debug(1, "Failed to allocate memory for a silent gap.");
                    } else {
                      // the player may change the contents of the buffer, so it has to be zeroed
                      // each time; might as well malloc and free it locally
                      conn->previous_random_number = generate_zero_frames(
                          silence, gap, conn->enable_dither, conn->previous_random_number,
                          config.current_output_configuration);
                      config.output->play(silence, gap, play_samples_are_untimed, 0, 0);
                      free(silence);
                      frames_played += gap;
                      // debug(1,"sent %d frames of silence.", gap);
                      sync_error_ns = 0; // don't invoke any sync checking
                      sync_error = 0;
                    }
                  }
                }
                // debug(1, "frames_to_skip: %u.", frames_to_skip);
                // don't do any sync error calculations if you're skipping frames
                if (frames_to_skip == 0) {
                  // first, make room in the array if it's full
                  if (conn->sync_samples_count == sync_history_length) {
                    conn->sync_samples_count--;
                  }
                  last_sample_index = conn->sync_samples_index;
                  sync_samples[conn->sync_samples_index] = sync_error_ns;
                  conn->sync_samples_count++;
                  conn->sync_samples_index = (conn->sync_samples_index + 1) % sync_history_length;

                  // now find the lowest and highest errors
                  sync_samples_highest_error = sync_samples[0];
                  sync_samples_lowest_error = sync_samples[0];
                  sync_samples_second_highest_error = sync_samples[0];
                  sync_samples_second_lowest_error = sync_samples[0];
                  unsigned int s;
                  int64_t mean = 0;
                  for (s = 0; s < conn->sync_samples_count; s++) {
                    mean += sync_samples[s];
                    if (sync_samples[s] > sync_samples_highest_error) {
                      sync_samples_second_highest_error = sync_samples_highest_error;
                      sync_samples_highest_error = sync_samples[s];
                    } else if (sync_samples[s] > sync_samples_second_highest_error) {
                      sync_samples_second_highest_error = sync_samples[s];
                    } else if (sync_samples[s] < sync_samples_lowest_error) {
                      sync_samples_second_lowest_error = sync_samples_lowest_error;
                      sync_samples_lowest_error = sync_samples[s];
                    } else if (sync_samples[s] < sync_samples_second_lowest_error) {
                      sync_samples_second_lowest_error = sync_samples[s];
                    }
                  }

                  if (conn->sync_samples_count != 0)
                    mean = mean / conn->sync_samples_count;

                  tsum_of_gaps = tsum_of_gaps + sync_samples_second_highest_error -
                                 sync_samples_second_lowest_error;

                  int64_t centered_sync_error_ns =
                      (sync_samples_second_highest_error + sync_samples_second_lowest_error) / 2;
                  centered_sync_error_time = centered_sync_error_ns * 0.000000001;

                  // debug(1, "centered_sync_error_ns: %" PRId64 ", %.3f sec.",
                  // centered_sync_error_ns, centered_sync_error_time);

                  // int64_t centered_sync_error =
                  //     (centered_sync_error_ns * config.current_output_configuration->rate) /
                  //     1000000000;

                  // decide whether to do a stuff

                  /*
                  // calculate the standard deviation

                  double sd = 0.0;
                  for (s = 0; s < conn->sync_samples_count; s++) {
                    sd += pow(sync_samples[s] - mean, 2);
                  }
                  if (conn->sync_samples_count != 0)
                    sd = sqrt(sd / conn->sync_samples_count);

                  // debug(1, "samples: %u, mean: %" PRId64 ", standard deviation: %f.",
                  // conn->sync_samples_count, mean, sd);
                  */

                  // it seems (?) that the standard deviation settles down markedly after 10 samples
                  // == 1024 * 10 frames in AAC
                  if (play_number * inbuflength >= 10 * 1024) {
                    // the tolerance is on either side of the correct, thus it contributes twice
                    // to the overall gap
                    int64_t tolerance_ns = (int64_t)(config.tolerance * 1000000000L);
                    // int64_t gap = 2 * tolerance_ns + sync_samples_second_highest_error -
                    //               sync_samples_second_lowest_error;
                    // int64_t gap = 2 * tolerance_ns;
                    // since the gap should be symmetrical about 0, stuff accordingly
                    if (centered_sync_error_ns > tolerance_ns) {
                      amount_to_stuff = -1 * (inbuflength / 350);
                      if (amount_to_stuff == 0)
                        amount_to_stuff = -1;
                      debug(3, "drop a frame, inbuflength is %d, amount_to_stuff is %d.",
                            inbuflength, amount_to_stuff);
                    } else if (centered_sync_error_ns < (-tolerance_ns)) {
                      amount_to_stuff = +1 * (inbuflength / 350);
                      if (amount_to_stuff == 0)
                        amount_to_stuff = 1;
                      debug(3, "add a frame, inbuflength is %d, amount_to_stuff is %d.",
                            inbuflength, amount_to_stuff);
                    } else {
                      debug(3,
                            "error is within tolerance: centered_sync_error_ns: %" PRId64
                            ", tolerance_ns: %" PRId64 " ns.",
                            centered_sync_error_ns, tolerance_ns);
                    }
                  }
                }
              }

              if (amount_to_stuff)
                debug(3,
                      //                          "stuff: %+d, sync_error: %+5.3f milliseconds.",
                      //                          amount_to_stuff, sync_error * 1000);
                      "stuff: %+d, sync_errors actual: %+5.3f milliseconds, bufferlength: %d, "
                      "sync window : %+5.3f "
                      "milliseconds, prior second highest: %+5.3f milliseconds, prior second "
                      "lowest: %+5.3f "
                      "milliseconds.",
                      amount_to_stuff, sync_error_ns * 0.000001, inbuflength,
                      (sync_samples_second_highest_error - sync_samples_second_lowest_error) *
                          0.000001,
                      sync_samples_second_highest_error * 0.000001,
                      sync_samples_second_lowest_error * 0.000001);

              // now, deal with sync errors and anomalies

              if ((config.no_sync == 0) && (inframe->timestamp != 0) &&
                  (config.resync_threshold > 0.0) &&
                  //                      (fabs(sync_error) > config.resync_threshold)) {
                  (fabs(centered_sync_error_time) > config.resync_threshold) &&
                  // don't count it if the error max and min values bracket (i.e. are on either
                  // size of) zero
                  !((sync_samples_highest_error >= 0) && ((sync_samples_lowest_error <= 0))) &&
                  (conn->sync_samples_count == sync_history_length)) {
                sync_error_out_of_bounds++;
              } else {
                sync_error_out_of_bounds = 0;
              }

              if (sync_error_out_of_bounds != 0) {
                debug(2,
                      "sync error for frame %" PRIu32
                      " out of bounds on %d successive occasions. Error is %.3f milliseconds -- "
                      "resync requested (%u, %" PRId64 ", %" PRId64 ").",
                      inframe->timestamp, sync_error_out_of_bounds, centered_sync_error_time * 1000,
                      conn->sync_samples_count, sync_samples_highest_error,
                      sync_samples_lowest_error);

                if (centered_sync_error_time < 0) {
                  request_resync = 1; // ask for a resync
                } else {
                  int16_t occ = conn->ab_write - conn->ab_read;
                  debug(2,
                        "drop late packet, timestamp: %u,  late by: %.3f ms, packets remaining in "
                        "the buffer: %u.",
                        inframe->timestamp, centered_sync_error_time * 1000, occ);
                  unsigned int s;
                  for (s = 0; s < conn->sync_samples_count; s++) {
                    debug(3, "sample: %u, value: %.3f ms", s, sync_samples[s] * 0.000001);
                  }
                  debug(3, "sync_history_length: %u, samples_count: %u, sample_index: %u",
                        sync_history_length, conn->sync_samples_count, last_sample_index);
                }
                sync_error_out_of_bounds = 0;
                // conn->sync_samples_index = 0;
                // conn->sync_samples_count = 0;
              } else {

                if (config.no_sync != 0)
                  amount_to_stuff = 0; // no stuffing if it's been disabled

                // Apply DSP here

                loudness_update(conn);

                if (conn->do_loudness
#ifdef CONFIG_CONVOLUTION
                    || config.convolution_enabled
#endif
                ) {

                  float(*fbufs)[1024] = malloc(conn->input_num_channels * sizeof(*fbufs));
                  // debug(1, "size of array allocated is %d bytes.", conn->input_num_channels *
                  // sizeof(*fbufs));
                  int32_t *tbuf32 = conn->tbuf;

                  // Deinterleave, and convert to float
                  unsigned int i, j;
                  for (i = 0; i < inframe->length; i++) {
                    for (j = 0; j < conn->input_num_channels; j++) {
                      fbufs[j][i] = tbuf32[conn->input_num_channels * i + j];
                    }
                  }

#ifdef CONFIG_CONVOLUTION
                  // Apply convolution
                  // First, have we got the right convolution setup?

                  static int convolver_is_valid = 0;
                  static size_t current_convolver_block_size = 0;
                  static unsigned int current_convolver_rate = 0;
                  static unsigned int current_convolver_channels = 0;
                  static double current_convolver_maximum_length_in_seconds = 0;

                  if (config.convolution_enabled) {
                    if (
                        // if any of these are true, we need to create a new convolver
                        // (conn->convolver_is_valid == 0) ||
                        (current_convolver_block_size != inframe->length) ||
                        (current_convolver_rate != conn->input_rate) ||
                        !((current_convolver_channels == 1) ||
                          (current_convolver_channels == conn->input_num_channels)) ||
                        (current_convolver_maximum_length_in_seconds !=
                         config.convolution_max_length_in_seconds) ||
                        (config.convolution_ir_files_updated == 1)) {

                      // look for a convolution ir file with a matching rate and channel count

                      convolver_is_valid = 0; // declare any current convolver as invalid
                      current_convolver_block_size = inframe->length;
                      current_convolver_rate = conn->input_rate;
                      current_convolver_channels = conn->input_num_channels;
                      current_convolver_maximum_length_in_seconds =
                          config.convolution_max_length_in_seconds;
                      config.convolution_ir_files_updated = 0;
                      debug(2, "try to initialise a %u/%u convolver.", current_convolver_rate,
                            current_convolver_channels);
                      char *convolver_file_found = NULL;
                      unsigned int ir = 0;
                      while ((ir < config.convolution_ir_file_count) &&
                             (convolver_file_found == NULL)) {
                        if ((config.convolution_ir_files[ir].samplerate ==
                             current_convolver_rate) &&
                            (config.convolution_ir_files[ir].channels ==
                             current_convolver_channels)) {
                          convolver_file_found = config.convolution_ir_files[ir].filename;
                        } else {
                          ir++;
                        }
                      }

                      // if no luck, try for a single-channel IR file
                      if (convolver_file_found == NULL) {
                        current_convolver_channels = 1;
                        ir = 0;
                        while ((ir < config.convolution_ir_file_count) &&
                               (convolver_file_found == NULL)) {
                          if ((config.convolution_ir_files[ir].samplerate ==
                               current_convolver_rate) &&
                              (config.convolution_ir_files[ir].channels ==
                               current_convolver_channels)) {
                            convolver_file_found = config.convolution_ir_files[ir].filename;
                          } else {
                            ir++;
                          }
                        }
                      }
                      if (convolver_file_found != NULL) {
                        // we have an apparently suitable convolution ir file, so lets initialise
                        // a convolver
                        convolver_is_valid = convolver_init(
                            convolver_file_found, conn->input_num_channels,
                            config.convolution_max_length_in_seconds, inframe->length);
                        convolver_wait_for_all();
                        // if (convolver_is_valid)
                        // debug(1, "convolver_init for %u channels was successful.",
                        // conn->input_num_channels); convolver_is_valid = convolver_init(
                        //     convolver_file_found, conn->input_num_channels,
                        //     config.convolution_max_length_in_seconds, inframe->length);
                      }

                      if (convolver_is_valid == 0)
                        debug(1, "can not initialise a %u/%u convolver.", current_convolver_rate,
                              conn->input_num_channels);
                      else
                        debug(1, "convolver: \"%s\".", convolver_file_found);
                    }
                    if (convolver_is_valid != 0) {
                      for (j = 0; j < conn->input_num_channels; j++) {
                        // convolver_process(j, fbufs[j], inframe->length);
                        convolver_process(j, fbufs[j], inframe->length);
                      }
                      convolver_wait_for_all();
                    }

                    // apply convolution gain even if no convolution is done...
                    float gain = pow(10.0, config.convolution_gain / 20.0);
                    for (i = 0; i < inframe->length; ++i) {
                      for (j = 0; j < conn->input_num_channels; j++) {
                        float output_level_db = 0.0;
                        if (fbufs[j][i] < 0.0)
                          output_level_db = 20 * log10(fbufs[j][i] / (float)INT32_MIN * 1.0);
                        else
                          output_level_db = 20 * log10(fbufs[j][i] / (float)INT32_MAX);
                        if (output_level_db > highest_convolver_output_db) {
                          highest_convolver_output_db = output_level_db;
                          if ((highest_convolver_output_db + config.convolution_gain) > 0.0)
                            warn("clipping %.1f dB with convolution gain set to %.1f dB!",
                                 highest_convolver_output_db + config.convolution_gain,
                                 config.convolution_gain);
                        }
                        fbufs[j][i] *= gain;
                      }
                    }
                  }

#endif
                  if (conn->do_loudness) {
                    loudness_process_blocks((float *)fbufs, inframe->length,
                                            conn->input_num_channels,
                                            (float)conn->fix_volume / 65536);
                  }

                  // Interleave and convert back to int32_t
                  for (i = 0; i < inframe->length; i++) {
                    for (j = 0; j < conn->input_num_channels; j++) {
                      tbuf32[conn->input_num_channels * i + j] = fbufs[j][i];
                    }
                  }

                  if (fbufs != NULL) {
                    free(fbufs);
                    fbufs = NULL;
                  }
                }
                // }
#ifdef CONFIG_SOXR
                double t = config.audio_backend_buffer_interpolation_threshold_in_seconds *
                           RATE_FROM_ENCODED_FORMAT(config.current_output_configuration);

                // figure out if we're going for soxr or something else

                if ((current_delay < t) || // delay is too small we definitely won't do soxr
                    (config.packet_stuffing == ST_basic) ||
                    (config.packet_stuffing == ST_vernier) ||
                    ((config.packet_stuffing == ST_auto) &&
                     ((config.soxr_delay_index == 0) || // soxr processing time unknown
                      (config.soxr_delay_index > config.soxr_delay_threshold) // too slow
                      ))) {
                  debug(3, "current_delay: %" PRIu64 ", dac buffer queue minimum length: %f.",
                        current_delay, t);
#endif
                  if (config.packet_stuffing == ST_basic)
                    play_samples = stuff_buffer_basic_32(
                        (int32_t *)conn->tbuf, inbuflength,
                        FORMAT_FROM_ENCODED_FORMAT(config.current_output_configuration),
                        conn->outbuf, amount_to_stuff, conn->enable_dither, conn);
                  else
                    play_samples = stuff_buffer_vernier(
                        (int32_t *)conn->tbuf, inbuflength,
                        FORMAT_FROM_ENCODED_FORMAT(config.current_output_configuration),
                        conn->outbuf, amount_to_stuff, conn->enable_dither, conn);

#ifdef CONFIG_SOXR
                } else { // soxr requested or auto requested with the index less or equal to the
                         // threshold
                  play_samples = stuff_buffer_soxr_32(
                      (int32_t *)conn->tbuf, inbuflength,
                      FORMAT_FROM_ENCODED_FORMAT(config.current_output_configuration), conn->outbuf,
                      amount_to_stuff, conn->enable_dither, conn);
                }
#endif

                if (conn->outbuf == NULL)
                  debug(1, "NULL outbuf to play -- skipping it.");
                else {
                  if (play_samples == 0)
                    debug(2, "nothing to play.");
                  else {
                    if (conn->software_mute_enabled) {
                      generate_zero_frames(conn->outbuf, play_samples, conn->enable_dither,
                                           conn->previous_random_number,
                                           config.current_output_configuration);
                    }
                    uint64_t should_be_time;
                    frame_to_local_time(inframe->timestamp, &should_be_time, conn);
                    // debug(1, "play frame %u.", inframe->timestamp);

                    // now, see if we are skipping some or all of these frames
                    if (frames_to_skip == 0) {
                      config.output->play(conn->outbuf, play_samples, play_samples_are_timed,
                                          inframe->timestamp, should_be_time);
                      frames_played += play_samples;
                    } else {
                      if (frames_to_skip > (unsigned int)play_samples) {
                        debug(3, "skipping a packet of %u frames.", play_samples);
                        debug_print_buffer(
                            3, conn->outbuf,
                            play_samples *
                                CHANNELS_FROM_ENCODED_FORMAT(config.current_output_configuration) *
                                sps_format_sample_size(FORMAT_FROM_ENCODED_FORMAT(
                                    config.current_output_configuration)));
                        frames_to_skip -= play_samples;
                      } else {

                        char *offset = conn->outbuf;
                        offset +=
                            frames_to_skip *
                            CHANNELS_FROM_ENCODED_FORMAT(config.current_output_configuration) *
                            sps_format_sample_size(
                                FORMAT_FROM_ENCODED_FORMAT(config.current_output_configuration));
                        config.output->play(offset, play_samples - frames_to_skip,
                                            play_samples_are_timed, inframe->timestamp,
                                            should_be_time);

                        debug(3, "skipping the first %u frames in a packet of %u frames.",
                              frames_to_skip, play_samples);
                        debug_print_buffer(3, conn->outbuf, offset - conn->outbuf);

                        frames_played += play_samples - frames_to_skip;
                        frames_to_skip = 0;
                        skipping_frames_at_start_of_play = 0;
                      }
                    }

#ifdef CONFIG_METADATA
                    // debug(1,"config.metadata_progress_interval is %f.",
                    // config.metadata_progress_interval);
                    if (config.metadata_progress_interval != 0.0) {
                      char hb[128];
                      if (this_is_the_first_frame != 0) {
                        memset(hb, 0, 128);
                        snprintf(hb, 127, "%" PRIu32 "/%" PRId64 "", inframe->timestamp,
                                 should_be_time);
                        send_ssnc_metadata('phb0', hb, strlen(hb), 1);
                        send_ssnc_metadata('phbt', hb, strlen(hb), 1);
                        time_of_last_metadata_progress_update = get_absolute_time_in_ns();
                      } else {
                        uint64_t mx = 1000000000;
                        uint64_t iv = config.metadata_progress_interval * mx;
                        iv = iv + time_of_last_metadata_progress_update;
                        int64_t delta = iv - get_absolute_time_in_ns();
                        if (delta <= 0) {
                          memset(hb, 0, 128);
                          snprintf(hb, 127, "%" PRIu32 "/%" PRId64 "", inframe->timestamp,
                                   should_be_time);
                          send_ssnc_metadata('phbt', hb, strlen(hb), 1);
                          time_of_last_metadata_progress_update = get_absolute_time_in_ns();
                        }
                      }
                    }
#endif
                  }
                }
              }
            } else {

              // if this is the first frame, see if it's close to when it's supposed to be
              // released, which will be its time plus latency and any offset_time

              if (config.packet_stuffing == ST_basic)
                play_samples = stuff_buffer_basic_32(
                    (int32_t *)conn->tbuf, inbuflength,
                    FORMAT_FROM_ENCODED_FORMAT(config.current_output_configuration), conn->outbuf,
                    0, conn->enable_dither, conn);
              else
                play_samples = stuff_buffer_vernier(
                    (int32_t *)conn->tbuf, inbuflength,
                    FORMAT_FROM_ENCODED_FORMAT(config.current_output_configuration), conn->outbuf,
                    0, conn->enable_dither, conn);
              if (conn->outbuf == NULL)
                debug(1, "NULL outbuf to play -- skipping it.");
              else {
                if (conn->software_mute_enabled) {
                  generate_zero_frames(conn->outbuf, play_samples, conn->enable_dither,
                                       conn->previous_random_number,
                                       config.current_output_configuration);
                }
                uint64_t should_be_time;
                frame_to_local_time(inframe->timestamp, &should_be_time, conn);
                debug(3, "play frame %u.", inframe->timestamp);
                config.output->play(conn->outbuf, play_samples, play_samples_are_timed,
                                    inframe->timestamp, should_be_time);
                frames_played += play_samples;
#ifdef CONFIG_METADATA
                // debug(1,"config.metadata_progress_interval is %f.",
                // config.metadata_progress_interval);
                if (config.metadata_progress_interval != 0.0) {
                  char hb[128];
                  if (this_is_the_first_frame != 0) {
                    memset(hb, 0, 128);
                    snprintf(hb, 127, "%" PRIu32 "/%" PRId64 "", inframe->timestamp,
                             should_be_time);
                    send_ssnc_metadata('phb0', hb, strlen(hb), 1);
                    send_ssnc_metadata('phbt', hb, strlen(hb), 1);
                    time_of_last_metadata_progress_update = get_absolute_time_in_ns();
                  } else {
                    uint64_t mx = 1000000000;
                    uint64_t iv = config.metadata_progress_interval * mx;
                    iv = iv + time_of_last_metadata_progress_update;
                    int64_t delta = iv - get_absolute_time_in_ns();
                    if (delta <= 0) {
                      memset(hb, 0, 128);
                      snprintf(hb, 127, "%" PRIu32 "/%" PRId64 "", inframe->timestamp,
                               should_be_time);
                      send_ssnc_metadata('phbt', hb, strlen(hb), 1);
                      time_of_last_metadata_progress_update = get_absolute_time_in_ns();
                    }
                  }
                }
#endif
              }
            }

            if (conn->tbuf) {
              free(conn->tbuf);
              conn->tbuf = NULL;
            }

            if (conn->outbuf) {
              free(conn->outbuf);
              conn->outbuf = NULL;
            }
          }
          tsum_of_frames = tsum_of_frames + frames_played;
          if (frames_played) {
            number_of_statistics++;
            frames_since_last_stats_logged += frames_played;
            // stats accumulation. We want an average of sync error, drift, adjustment,
            // number of additions+subtractions
            tsum_of_sync_errors = tsum_of_sync_errors + sync_error;
            tsum_of_corrections = tsum_of_corrections + amount_to_stuff;
            tsum_of_insertions_and_deletions =
                tsum_of_insertions_and_deletions + abs(amount_to_stuff);
          }
        }
        // free buffers and mark the frame as finished
#ifdef CONFIG_FFMPEG
        if (inframe->avframe != NULL) {
          av_frame_free(&inframe->avframe);
          inframe->avframe = NULL;
        }
#endif
        inframe->timestamp = 0;
        inframe->sequence_number = 0;
        inframe->resend_time = 0;
        inframe->initialisation_time = 0;
        inframe->timestamp_gap = 0;

      } else {
        debug(1, "audio block sequence number %u, ready status: %u with no data!",
              inframe->sequence_number, inframe->ready);
      }
      free_audio_buffer_payload(inframe);
    }
  }

  debug(1, "This should never be called.");
  pthread_cleanup_pop(1); // pop the cleanup handler
                          //  debug(1, "This should never be called either.");
                          //  pthread_cleanup_pop(1); // pop the initial cleanup handler
  pthread_exit(NULL);
}

static void player_send_volume_metadata(uint8_t vol_mode_both, double airplay_volume,
                                        double scaled_attenuation, int32_t max_db, int32_t min_db,
                                        int32_t hw_max_db) {
#ifdef CONFIG_METADATA
  // here, send the 'pvol' metadata message when the airplay volume information
  // is being used by shairport sync to control the output volume
  char dv[128];
  memset(dv, 0, 128);
  if (config.ignore_volume_control == 0) {
    if (vol_mode_both == 1) {
      // normalise the maximum output to the hardware device's max output
      snprintf(dv, 127, "%.2f,%.2f,%.2f,%.2f", airplay_volume,
               (scaled_attenuation - max_db + hw_max_db) / 100.0,
               (min_db - max_db + hw_max_db) / 100.0, (max_db - max_db + hw_max_db) / 100.0);
    } else {
      snprintf(dv, 127, "%.2f,%.2f,%.2f,%.2f", airplay_volume, scaled_attenuation / 100.0,
               min_db / 100.0, max_db / 100.0);
    }
  } else {
    snprintf(dv, 127, "%.2f,%.2f,%.2f,%.2f", airplay_volume, 0.0, 0.0, 0.0);
  }
  send_ssnc_metadata('pvol', dv, strlen(dv), 1);
#else
  (void)vol_mode_both;
  (void)airplay_volume;
  (void)scaled_attenuation;
  (void)max_db;
  (void)min_db;
  (void)hw_max_db;
#endif
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

  // if the device is giving us a decibel-denominated volume range
  if ((config.output->parameters != NULL) && (config.output->parameters()->volume_range != NULL)) {
    volume_mode = vol_hw_only;
    hw_max_db = config.output->parameters()->volume_range->maximum_volume_dB;
    hw_min_db = config.output->parameters()->volume_range->minimum_volume_dB;
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
      } else {
        hw_min_db = hw_max_db - desired_range_db;
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

  if (airplay_volume == -144.0) {
    // only mute if you're not ignoring the volume control
    if (config.ignore_volume_control == 0) {
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
    }
    uint8_t vol_mode_both = (volume_mode == vol_both) ? 1 : 0;
    player_send_volume_metadata(vol_mode_both, airplay_volume, 0, 0, 0, 0);
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
    double scaled_attenuation = max_db;
    if (config.ignore_volume_control == 0) {

      if (config.volume_control_profile == VCP_standard)
        scaled_attenuation = vol2attn(airplay_volume, max_db, min_db); // no cancellation points
      else if (config.volume_control_profile == VCP_flat)
        scaled_attenuation =
            flat_vol2attn(airplay_volume, max_db, min_db); // no cancellation points
      else if (config.volume_control_profile == VCP_dasl_tapered)
        scaled_attenuation =
            dasl_tapered_vol2attn(airplay_volume, max_db, min_db); // no cancellation points
      else
        debug(1, "player_volume_without_notification: unrecognised volume control profile");
    }
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

      if (config.ignore_volume_control == 0)
        debug(3, "Software attenuation set to %f, i.e %f out of 65,536, for airplay volume of %f",
              software_attenuation, temp_fix_volume, airplay_volume);
      else
        debug(3, "Software attenuation set to %f, i.e %f out of 65,536. Volume control is ignored.",
              software_attenuation, temp_fix_volume);

      conn->fix_volume = temp_fix_volume;
    }
    if (conn != NULL)
      debug(3, "Connection %d: AirPlay Volume set to %.3f, Output Level set to: %.2f dB.",
            conn->connection_number, airplay_volume, scaled_attenuation / 100.0);
    else
      debug(3, "AirPlay Volume set to %.3f, Output Level set to: %.2f dB. NULL conn.",
            airplay_volume, scaled_attenuation / 100.0);

    if (config.logOutputLevel) {
      inform("Output Level set to: %.2f dB.", scaled_attenuation / 100.0);
    }

    uint8_t vol_mode_both = (volume_mode == vol_both) ? 1 : 0;
    player_send_volume_metadata(vol_mode_both, airplay_volume, scaled_attenuation, max_db, min_db,
                                hw_max_db);

    if (config.output->mute)
      config.output->mute(0);
    conn->software_mute_enabled = 0;

    debug(3,
          "player_volume_without_notification: volume mode is %d, airplay volume is %.2f, "
          "software_attenuation dB: %.2f, hardware_attenuation dB: %.2f, muting "
          "is disabled.",
          volume_mode, airplay_volume, software_attenuation / 100.0, hardware_attenuation / 100.0);
  }
  // here, store the volume for possible use in the future
  config.airplay_volume = airplay_volume;
  conn->own_airplay_volume = airplay_volume;
  debug_mutex_unlock(&conn->volume_control_mutex, 3);
}

void player_volume(double airplay_volume, rtsp_conn_info *conn) {
  command_set_volume(airplay_volume);
  player_volume_without_notification(airplay_volume, conn);
}

void do_flush(uint32_t timestamp, rtsp_conn_info *conn) {

  debug(3, "do_flush: flush to %u.", timestamp);
  debug_mutex_lock(&conn->flush_mutex, 1000, 1);
  conn->flush_requested = 1;
  conn->flush_rtp_timestamp = timestamp; // flush all packets up to, but not including, this one.
  reset_input_flow_metrics(conn);
  debug_mutex_unlock(&conn->flush_mutex, 3);
}

void player_flush(uint32_t timestamp, rtsp_conn_info *conn) {
  debug(3, "player_flush");
  do_flush(timestamp, conn);
#ifdef CONFIG_CONVOLUTION
  convolver_clear_state();
#endif
#ifdef CONFIG_METADATA
  // only send a flush metadata message if the first packet has been seen -- it's a bogus message
  // otherwise
  if (conn->first_packet_timestamp) {
    char numbuf[32];
    snprintf(numbuf, sizeof(numbuf), "%u", timestamp);
    send_ssnc_metadata('pfls', numbuf, strlen(numbuf), 1); // contains cancellation points
  }
#endif
}

int player_play(rtsp_conn_info *conn) {
  debug(2, "Connection %d: player_play.", conn->connection_number);
  command_start(); // before startup, and before the prepare() method runs
  // give the output device as much advance warning as possible to get ready
  // and make sure it's done before launching the player thread
  if (config.output->prepare)
    config.output->prepare(); // give the backend its first chance to prepare itself, knowing it has
                              // access to the output device (i.e. knowing that it should not be in
                              // use by another program at this time).

  pthread_cleanup_debug_mutex_lock(&conn->player_create_delete_mutex, 5000, 1);
  if (conn->player_thread == NULL) {
    pthread_t *pt = malloc(sizeof(pthread_t));
    if (pt == NULL)
      die("Couldn't allocate space for pthread_t");

    int rc = named_pthread_create_with_priority(pt, 3, player_thread_func, (void *)conn,
                                                "player_%d", conn->connection_number);
    if (rc)
      debug(1, "Connection %d: error creating player_thread: %s", conn->connection_number,
            strerror(errno));
    conn->player_thread = pt; // set _after_ creation of thread
  } else {
    debug(1, "Connection %d: player thread already exists.", conn->connection_number);
  }
  pthread_cleanup_pop(1); // release the player_create_delete_mutex
#ifdef CONFIG_METADATA
  send_ssnc_metadata('pbeg', NULL, 0, 1); // contains cancellation points
#endif
  conn->is_playing = 1;
  return 0;
}

int player_stop(rtsp_conn_info *conn) {
  // note -- this may be called from another connection thread.
  debug(2, "Connection %d: player_stop.", conn->connection_number);
  int response = 0; // okay
  pthread_cleanup_debug_mutex_lock(&conn->player_create_delete_mutex, 5000, 1);
  pthread_t *pt = conn->player_thread;
  if (pt) {
    debug(3, "player_thread cancel...");
    conn->player_thread = NULL; // cleared _before_ cancelling of thread
    pthread_cancel(*pt);
    debug(3, "player_thread join...");
    if (pthread_join(*pt, NULL) == -1) {
      char errorstring[1024];
      strerror_r(errno, (char *)errorstring, sizeof(errorstring));
      debug(1, "Connection %d: error %d joining player thread: \"%s\".", conn->connection_number,
            errno, (char *)errorstring);
    } else {
      debug(2, "Connection %d: player_stop successful.", conn->connection_number);
    }
    free(pt);
    // reset_anchor_info(conn); // say the clock is no longer valid
#ifdef CONFIG_CONVOLUTION
    convolver_clear_state();
#endif
    response = 0; // deleted
  } else {
    debug(2, "Connection %d: no player thread.", conn->connection_number);
    response = -1; // already deleted or never created...
  }
  pthread_cleanup_pop(1); // release the player_create_delete_mutex
  if (response == 0) {    // if the thread was just stopped and deleted...
    conn->is_playing = 0;
/*
// this is done in the player cleanup handler
#ifdef CONFIG_AIRPLAY_2
    ptp_send_control_message_string("E"); // signify play is "E"nding
#endif
*/
#ifdef CONFIG_METADATA
    send_ssnc_metadata('pend', NULL, 0, 1); // contains cancellation points
#endif
    command_stop();
  }
  return response;
}
