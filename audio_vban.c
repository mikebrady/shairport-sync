/*
 * VBAN output driver. This file is part of Shairport Sync.
 *
 * Copyright (c) Mike Brady 2014--2025
 * Copyright (c) DOCa Cola 2026
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

#include "audio.h"
#include "common.h"
#include "config.h"
#include "utilities/network_utilities.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CONFIG_FOR_MINGW
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#define VBAN_DEFAULT_PORT 6980
#define VBAN_HEADER_SIZE 28
#define VBAN_STREAM_NAME_SIZE 16
#define VBAN_DATA_MAX_SIZE 1436
#define VBAN_SAMPLES_MAX_NB 256
#define VBAN_CHANNELS_MAX_NB 8

static const long vban_sample_rates[] = {6000,  12000, 24000, 48000,  96000,  192000, 384000,
                                         8000,  16000, 32000, 64000,  128000, 256000, 512000,
                                         11025, 22050, 44100, 88200,  176400, 352800, 705600};

static const char *destination = NULL;
static int port = VBAN_DEFAULT_PORT;
static char stream_name[VBAN_STREAM_NAME_SIZE] = {0};
static int stream_name_truncated = 0;

static int fd = -1;
static struct sockaddr_storage remote_address;
static socklen_t remote_address_length = 0;

static unsigned int configured_channels = 0;
static unsigned int configured_rate = 0;
static unsigned int configured_format = SPS_FORMAT_UNKNOWN;
static unsigned int bytes_per_frame = 0;
static unsigned int samples_per_packet = 0;
static uint8_t vban_sample_rate_index = 0;
static uint8_t vban_bit_format = 0;
static uint32_t frame_counter = 0;
static int warned = 0;

static int socket_errno(void) {
#ifdef CONFIG_FOR_MINGW
  return WSAGetLastError();
#else
  return errno;
#endif
}

static int vban_rate_index(unsigned int rate) {
  for (unsigned int i = 0; i < sizeof(vban_sample_rates) / sizeof(vban_sample_rates[0]); i++)
    if ((unsigned int)vban_sample_rates[i] == rate)
      return (int)i;
  return -1;
}

static int rate_is_configured(unsigned int rate) {
  for (sps_rate_t r = SPS_RATE_LOWEST; r <= SPS_RATE_HIGHEST; r++)
    if (((config.rate_set & (1 << r)) != 0) && (sps_rate_actual_rate(r) == rate))
      return 1;
  return 0;
}

static sps_format_t native_format(sps_format_t format) {
  switch (format) {
  case SPS_FORMAT_S16:
    return config.endianness == SS_BIG_ENDIAN ? SPS_FORMAT_S16_BE : SPS_FORMAT_S16_LE;
  case SPS_FORMAT_S24:
    return config.endianness == SS_BIG_ENDIAN ? SPS_FORMAT_S24_3BE : SPS_FORMAT_S24_3LE;
  case SPS_FORMAT_S32:
    return config.endianness == SS_BIG_ENDIAN ? SPS_FORMAT_S32_BE : SPS_FORMAT_S32_LE;
  default:
    return format;
  }
}

static int vban_bit_format_for_sps_format(sps_format_t format, uint8_t *bit_format) {
  switch (format) {
  case SPS_FORMAT_S8:
  case SPS_FORMAT_U8:
    *bit_format = 0; // 8I
    return 0;
  case SPS_FORMAT_S16_LE:
    *bit_format = 1; // 16I
    return 0;
  case SPS_FORMAT_S24_3LE:
    *bit_format = 2; // 24I
    return 0;
  case SPS_FORMAT_S32_LE:
    *bit_format = 3; // 32I
    return 0;
  default:
    return -1;
  }
}

static int open_socket(void) {
  if (fd != -1)
    return 0;

  char port_string[16];
  snprintf(port_string, sizeof(port_string), "%d", port);

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;

  struct addrinfo *info = NULL;
  int response = getaddrinfo(destination, port_string, &hints, &info);
  if (response != 0)
    die("vban: can not resolve destination \"%s\" port %d.", destination, port);

  for (struct addrinfo *p = info; p != NULL; p = p->ai_next) {
    fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd == -1)
      continue;

    memcpy(&remote_address, p->ai_addr, p->ai_addrlen);
    remote_address_length = p->ai_addrlen;
    break;
  }

  freeaddrinfo(info);

  if (fd == -1)
    die("vban: can not create UDP socket for destination \"%s\".", destination);

  return 0;
}

static void set_stream_name(const char *name) {
  memset(stream_name, 0, sizeof(stream_name));
  if (name == NULL)
    name = "Shairport Sync";

  size_t length = strlen(name);
  if (length > VBAN_STREAM_NAME_SIZE) {
    length = VBAN_STREAM_NAME_SIZE;
    stream_name_truncated = 1;
  }
  memcpy(stream_name, name, length);
}

static void help(void) {
  printf("    vban.destination: destination host or IP address.\n");
  printf("    vban.port: destination UDP port. The default is %d.\n", VBAN_DEFAULT_PORT);
  printf("    vban.stream_name: VBAN stream name. Defaults to the Shairport Sync service name and is truncated to %d bytes.\n",
         VBAN_STREAM_NAME_SIZE);
}

static int init(int argc, char **argv) {
  config.audio_backend_buffer_desired_length = 1.0;
  config.audio_backend_latency_offset = 0;

  uint32_t default_format_set = (1 << SPS_FORMAT_S8) | (1 << SPS_FORMAT_U8) |
                                (1 << SPS_FORMAT_S16_LE) | (1 << SPS_FORMAT_S24_3LE) |
                                (1 << SPS_FORMAT_S32_LE);
  parse_audio_options("vban", default_format_set, SPS_RATE_SET, (1 << 1) | (1 << 2) |
                                                           (1 << 3) | (1 << 4) |
                                                           (1 << 5) | (1 << 6) |
                                                           (1 << 7) | (1 << 8));

  if (config.cfg != NULL) {
    const char *str;
    int value;

    if (config_lookup_non_empty_string(config.cfg, "vban.destination", &str))
      destination = str;

    if (config_lookup_int(config.cfg, "vban.port", &value)) {
      if ((value <= 0) || (value > 65535))
        die("vban.port must be between 1 and 65535.");
      port = value;
    }

    if (config_lookup_non_empty_string(config.cfg, "vban.stream_name", &str))
      set_stream_name(str);
  }

  if (argc > 3)
    die("too many command-line arguments to vban");
  if (argc >= 1)
    destination = argv[0];
  if (argc >= 2) {
    port = atoi(argv[1]);
    if ((port <= 0) || (port > 65535))
      die("vban port must be between 1 and 65535.");
  }
  if (argc >= 3)
    set_stream_name(argv[2]);

  if (destination == NULL)
    die("vban.destination is required.");

  if (stream_name[0] == 0)
    set_stream_name(config.service_name);

  if (stream_name_truncated)
    warn("vban.stream_name is longer than %d bytes and has been truncated.", VBAN_STREAM_NAME_SIZE);

  return open_socket();
}

static void deinit(void) { safe_socket_close(&fd); }

static int32_t get_configuration(unsigned int channels, unsigned int rate, unsigned int format) {
  sps_format_t selected_format = native_format((sps_format_t)format);

  if ((channels == 0) || (channels > VBAN_CHANNELS_MAX_NB))
    return 0;
  if ((config.channel_set & (1 << channels)) == 0)
    return 0;
  if ((vban_rate_index(rate) < 0) || (rate_is_configured(rate) == 0))
    return 0;
  if ((config.format_set & (1 << selected_format)) == 0)
    return 0;

  uint8_t bit_format;
  if (vban_bit_format_for_sps_format(selected_format, &bit_format) != 0)
    return 0;

  return CHANNELS_TO_ENCODED_FORMAT(channels) | RATE_TO_ENCODED_FORMAT(rate) |
         FORMAT_TO_ENCODED_FORMAT(selected_format);
}

static int configure(int32_t requested_encoded_format, __attribute__((unused)) char **channel_map) {
  configured_channels = CHANNELS_FROM_ENCODED_FORMAT(requested_encoded_format);
  configured_rate = RATE_FROM_ENCODED_FORMAT(requested_encoded_format);
  configured_format = FORMAT_FROM_ENCODED_FORMAT(requested_encoded_format);

  int rate_index = vban_rate_index(configured_rate);
  if (rate_index < 0)
    return EINVAL;

  if (vban_bit_format_for_sps_format((sps_format_t)configured_format, &vban_bit_format) != 0)
    return EINVAL;

  unsigned int bytes_per_sample = sps_format_sample_size((sps_format_t)configured_format);
  if ((bytes_per_sample == 0) || (configured_channels == 0))
    return EINVAL;

  bytes_per_frame = bytes_per_sample * configured_channels;
  samples_per_packet = VBAN_DATA_MAX_SIZE / bytes_per_frame;
  if (samples_per_packet > VBAN_SAMPLES_MAX_NB)
    samples_per_packet = VBAN_SAMPLES_MAX_NB;
  if (samples_per_packet == 0)
    return EINVAL;

  vban_sample_rate_index = (uint8_t)rate_index;
  frame_counter = 0;
  warned = 0;

  debug(1, "vban: setting output configuration to %s.", short_format_description(requested_encoded_format));
  return 0;
}

static void write_frame_counter(uint8_t *p, uint32_t value) {
  p[0] = (uint8_t)(value & 0xff);
  p[1] = (uint8_t)((value >> 8) & 0xff);
  p[2] = (uint8_t)((value >> 16) & 0xff);
  p[3] = (uint8_t)((value >> 24) & 0xff);
}

static int send_packet(const uint8_t *audio, unsigned int samples) {
  uint8_t packet[VBAN_HEADER_SIZE + VBAN_DATA_MAX_SIZE];
  size_t payload_size = samples * bytes_per_frame;

  packet[0] = 'V';
  packet[1] = 'B';
  packet[2] = 'A';
  packet[3] = 'N';
  packet[4] = vban_sample_rate_index;
  packet[5] = (uint8_t)(samples - 1);
  packet[6] = (uint8_t)(configured_channels - 1);
  packet[7] = vban_bit_format;
  memcpy(&packet[8], stream_name, VBAN_STREAM_NAME_SIZE);
  write_frame_counter(&packet[24], frame_counter++);
  memcpy(&packet[VBAN_HEADER_SIZE], audio, payload_size);

  int response = sendto(fd, (const char *)packet, VBAN_HEADER_SIZE + payload_size, 0,
                        (struct sockaddr *)&remote_address, remote_address_length);
  if ((response < 0) && (warned == 0)) {
    warn("vban: error %d sending UDP packet.", socket_errno());
    warned = 1;
  }
  return response;
}

static int play(void *buf, int samples, __attribute__((unused)) int sample_type,
                __attribute__((unused)) uint32_t timestamp,
                __attribute__((unused)) uint64_t playtime) {
  if ((fd == -1) && (open_socket() != 0))
    return -1;
  if ((bytes_per_frame == 0) || (samples_per_packet == 0)) {
    debug(1, "vban: output format not configured before play().");
    return -1;
  }

  const uint8_t *audio = buf;
  int samples_remaining = samples;
  while (samples_remaining > 0) {
    unsigned int chunk = samples_remaining > (int)samples_per_packet ? samples_per_packet
                                                                     : (unsigned int)samples_remaining;
    send_packet(audio, chunk);
    audio += chunk * bytes_per_frame;
    samples_remaining -= chunk;
  }

  return 0;
}

static void flush(void) { frame_counter = 0; }

audio_output audio_vban = {.name = "vban",
                           .help = &help,
                           .init = &init,
                           .deinit = &deinit,
                           .get_configuration = &get_configuration,
                           .configure = &configure,
                           .start = NULL,
                           .stop = NULL,
                           .is_running = NULL,
                           .flush = &flush,
                           .delay = NULL,
                           .stats = NULL,
                           .play = &play,
                           .volume = NULL,
                           .parameters = NULL,
                           .mute = NULL};
