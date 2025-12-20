/*
 * Utility routines. This file is part of Shairport.
 * Copyright (c) James Laird 2013
 * The volume to attenuation function vol2attn copyright (c) Mike Brady 2014
 * Further changes and additions (c) Mike Brady 2014 -- 2021
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

#include "common.h"

#ifdef CONFIG_USE_GIT_VERSION_STRING
#include "gitversion.h"
#endif

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h> // PRIdPTR
#include <libgen.h>
#include <math.h>
#include <memory.h>
#include <poll.h>
#include <popt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <ifaddrs.h>

#ifdef COMPILE_FOR_LINUX
#include <netpacket/packet.h>
#endif

#ifdef COMPILE_FOR_BSD
#include <net/if_dl.h>
#include <net/if_types.h>
#include <netinet/in.h>
#endif

#ifdef COMPILE_FOR_OSX
#include <CoreServices/CoreServices.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <netinet/in.h>
#endif

#ifdef CONFIG_OPENSSL
#include <openssl/aes.h> // needed for older AES stuff
#include <openssl/bio.h> // needed for BIO_new_mem_buf
#include <openssl/err.h> // needed for ERR_error_string, ERR_get_error
#include <openssl/evp.h> // needed for EVP_PKEY_CTX_new, EVP_PKEY_sign_init, EVP_PKEY_sign
#include <openssl/pem.h> // needed for PEM_read_bio_RSAPrivateKey, EVP_PKEY_CTX_set_rsa_padding
#include <openssl/rsa.h> // needed for EVP_PKEY_CTX_set_rsa_padding
#endif

#ifdef CONFIG_POLARSSL
#include "polarssl/ctr_drbg.h"
#include "polarssl/entropy.h"
#include <polarssl/base64.h>
#include <polarssl/md.h>
#include <polarssl/version.h>
#include <polarssl/x509.h>

#if POLARSSL_VERSION_NUMBER >= 0x01030000
#include "polarssl/compat-1.2.h"
#endif
#endif

#ifdef CONFIG_MBEDTLS
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <mbedtls/version.h>
#include <mbedtls/x509.h>

#if MBEDTLS_VERSION_MAJOR == 3
#define MBEDTLS_PRIVATE_V3_ONLY(_q) MBEDTLS_PRIVATE(_q)
#else
#define MBEDTLS_PRIVATE_V3_ONLY(_q) _q
#endif
#endif

#ifdef CONFIG_LIBDAEMON
#include <libdaemon/dlog.h>
#else
#include <syslog.h>
#endif

#ifdef CONFIG_ALSA
void set_alsa_out_dev(char *);
#endif

#ifdef CONFIG_AIRPLAY_2
#include "nqptp-shm-structures.h"
#endif

config_t config_file_stuff;
int type_of_exit_cleanup;
uint64_t ns_time_at_startup, ns_time_at_last_debug_message;

// always lock use this when accessing the ns_time_at_last_debug_message
static pthread_mutex_t debug_timing_lock = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t the_conn_lock = PTHREAD_MUTEX_INITIALIZER;

const char *sps_format_description_string_array[] = {
    "unknown", "S8",      "U8",      "S16", "S16_LE", "S16_BE", "S24",  "S24_LE",
    "S24_BE",  "S24_3LE", "S24_3BE", "S32", "S32_LE", "S32_BE", "auto", "invalid"};

const char *sps_format_description_string(sps_format_t format) {
  if (format <= SPS_FORMAT_AUTO)
    return sps_format_description_string_array[format];
  else
    return sps_format_description_string_array[SPS_FORMAT_INVALID];
}

// true if Shairport Sync is supposed to be sending output to the output device, false otherwise

static volatile int requested_connection_state_to_output = 1;

// this stuff is to direct logging to syslog via libdaemon or directly
// alternatively you can direct it to stderr using a command line option

#ifdef CONFIG_LIBDAEMON
static void (*sps_log)(int prio, const char *t, ...) = daemon_log;
#else
static void (*sps_log)(int prio, const char *t, ...) = syslog;
#endif

void do_sps_log_to_stderr(__attribute__((unused)) int prio, const char *t, ...) {
  char s[16384];
  va_list args;
  va_start(args, t);
  vsnprintf(s, sizeof(s), t, args);
  va_end(args);
  fprintf(stderr, "%s\n", s);
}

void do_sps_log_to_stdout(__attribute__((unused)) int prio, const char *t, ...) {
  char s[16384];
  va_list args;
  va_start(args, t);
  vsnprintf(s, sizeof(s), t, args);
  va_end(args);
  fprintf(stdout, "%s\n", s);
}

int create_log_file(const char *path) {
  int fd = -1;
  if (path != NULL) {
    char *dirc = strdup(path);
    if (dirc) {
      char *dname = dirname(dirc);
      // create the directory, if necessary
      int result = 0;
      if (dname) {
        char *pdir = realpath(dname, NULL); // will return a NULL if the directory doesn't exist
        if (pdir == NULL) {
          mode_t oldumask = umask(000);
          result = mkpath(dname, 0777);
          umask(oldumask);
        } else {
          free(pdir);
        }
        if ((result == 0) || (result == -EEXIST)) {
          // now open the file
          fd = open(path, O_WRONLY | O_NONBLOCK | O_CREAT | O_EXCL,
                    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
          if ((fd == -1) && (errno == EEXIST))
            fd = open(path, O_WRONLY | O_APPEND | O_NONBLOCK);

          if (fd >= 0) {
            // now we switch to blocking mode
            int flags = fcntl(fd, F_GETFL);
            if (flags == -1) {
              //							strerror_r(errno, (char
              //*)errorstring, sizeof(errorstring));
              // debug(1, "create_log_file -- error %d (\"%s\") getting flags of pipe: \"%s\".",
              // errno,
              // (char *)errorstring, pathname);
            } else {
              flags = fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
              //							if (flags == -1) {
              //								strerror_r(errno,
              //(char *)errorstring, sizeof(errorstring));
              // debug(1, "create_log_file -- error %d
              //(\"%s\") unsetting NONBLOCK of pipe: \"%s\".", errno,
              //(char *)errorstring, pathname);
            }
          }
        }
      }
      free(dirc);
    }
  }
  return fd;
}

void do_sps_log_to_fd(__attribute__((unused)) int prio, const char *t, ...) {
  char s[16384];
  va_list args;
  va_start(args, t);
  vsnprintf(s, sizeof(s), t, args);
  va_end(args);
  if (config.log_fd == -1)
    config.log_fd = create_log_file(config.log_file_path);
  if (config.log_fd >= 0) {
    dprintf(config.log_fd, "%s\n", s);
  } else if (errno != ENXIO) { // maybe there is a pipe there but not hooked up
    fprintf(stderr, "%s\n", s);
  }
}

void log_to_stderr() { sps_log = do_sps_log_to_stderr; }
void log_to_stdout() { sps_log = do_sps_log_to_stdout; }
void log_to_file() { sps_log = do_sps_log_to_fd; }
void log_to_syslog() {
#ifdef CONFIG_LIBDAEMON
  sps_log = daemon_log;
#else
  sps_log = syslog;
#endif
}

shairport_cfg config;

volatile int debuglev = 0;

sigset_t pselect_sigset;

int usleep_uncancellable(useconds_t usec) {
  int response;
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  response = usleep(usec);
  pthread_setcancelstate(oldState, NULL);
  return response;
}

static uint16_t UDPPortIndex = 0;

void resetFreeUDPPort() {
  debug(3, "Resetting UDP Port Suggestion to %u", config.udp_port_base);
  UDPPortIndex = 0;
}

uint16_t nextFreeUDPPort() {
  if (UDPPortIndex == 0)
    UDPPortIndex = config.udp_port_base;
  else if (UDPPortIndex == (config.udp_port_base + config.udp_port_range - 1))
    UDPPortIndex = config.udp_port_base + 3; // avoid wrapping back to the first three, as they can
                                             // be assigned by resetFreeUDPPort without checking
  else
    UDPPortIndex++;
  return UDPPortIndex;
}

// if port is zero, pick any port
// otherwise, try the given port only
int bind_socket_and_port(int type, int ip_family, const char *self_ip_address, uint32_t scope_id,
                         uint16_t *port, int *sock) {
  int ret = 0; // no error
  int local_socket = socket(ip_family, type, 0);
  if (local_socket == -1)
    ret = errno;
  if (ret == 0) {
    SOCKADDR myaddr;
    memset(&myaddr, 0, sizeof(myaddr));
    if (ip_family == AF_INET) {
      struct sockaddr_in *sa = (struct sockaddr_in *)&myaddr;
      sa->sin_family = AF_INET;
      sa->sin_port = ntohs(*port);
      inet_pton(AF_INET, self_ip_address, &(sa->sin_addr));
      ret = bind(local_socket, (struct sockaddr *)sa, sizeof(struct sockaddr_in));
    }
#ifdef AF_INET6
    if (ip_family == AF_INET6) {
      struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)&myaddr;
      sa6->sin6_family = AF_INET6;
      sa6->sin6_port = ntohs(*port);
      inet_pton(AF_INET6, self_ip_address, &(sa6->sin6_addr));
      sa6->sin6_scope_id = scope_id;
      ret = bind(local_socket, (struct sockaddr *)sa6, sizeof(struct sockaddr_in6));
    }
#endif
    if (ret < 0) {
      ret = errno;
      close(local_socket);
      char errorstring[1024];
      strerror_r(errno, (char *)errorstring, sizeof(errorstring));
      warn("error %d: \"%s\". Could not bind a port!", errno, errorstring);
    } else {
      uint16_t sport;
      SOCKADDR local;
      socklen_t local_len = sizeof(local);
      ret = getsockname(local_socket, (struct sockaddr *)&local, &local_len);
      if (ret < 0) {
        ret = errno;
        close(local_socket);
        char errorstring[1024];
        strerror_r(errno, (char *)errorstring, sizeof(errorstring));
        warn("error %d: \"%s\". Could not retrieve socket's port!", errno, errorstring);
      } else {
#ifdef AF_INET6
        if (local.SAFAMILY == AF_INET6) {
          struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)&local;
          sport = ntohs(sa6->sin6_port);
        } else
#endif
        {
          struct sockaddr_in *sa = (struct sockaddr_in *)&local;
          sport = ntohs(sa->sin_port);
        }
        *sock = local_socket;
        *port = sport;
      }
    }
  }
  return ret;
}

uint16_t bind_UDP_port(int ip_family, const char *self_ip_address, uint32_t scope_id, int *sock) {
  // look for a port in the range, if any was specified.
  int ret = 0;

  int local_socket = socket(ip_family, SOCK_DGRAM, IPPROTO_UDP);
  if (local_socket == -1)
    die("Could not allocate a socket.");

  /*
    int val = 1;
    ret = setsockopt(local_socket, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    if (ret < 0) {
      char errorstring[1024];
      strerror_r(errno, (char *)errorstring, sizeof(errorstring));
      debug(1, "Error %d: \"%s\". Couldn't set SO_REUSEADDR");
    }
  */

  SOCKADDR myaddr;
  int tryCount = 0;
  uint16_t desired_port;
  do {
    tryCount++;
    desired_port = nextFreeUDPPort();
    memset(&myaddr, 0, sizeof(myaddr));
    if (ip_family == AF_INET) {
      struct sockaddr_in *sa = (struct sockaddr_in *)&myaddr;
      sa->sin_family = AF_INET;
      sa->sin_port = ntohs(desired_port);
      inet_pton(AF_INET, self_ip_address, &(sa->sin_addr));
      ret = bind(local_socket, (struct sockaddr *)sa, sizeof(struct sockaddr_in));
    }
#ifdef AF_INET6
    if (ip_family == AF_INET6) {
      struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)&myaddr;
      sa6->sin6_family = AF_INET6;
      sa6->sin6_port = ntohs(desired_port);
      inet_pton(AF_INET6, self_ip_address, &(sa6->sin6_addr));
      sa6->sin6_scope_id = scope_id;
      ret = bind(local_socket, (struct sockaddr *)sa6, sizeof(struct sockaddr_in6));
    }
#endif

  } while ((ret < 0) && (errno == EADDRINUSE) && (desired_port != 0) &&
           (tryCount < config.udp_port_range));

  // debug(1,"UDP port chosen: %d.",desired_port);

  if (ret < 0) {
    close(local_socket);
    char errorstring[1024];
    strerror_r(errno, (char *)errorstring, sizeof(errorstring));
    die("error %d: \"%s\". Could not bind a UDP port! Check the udp_port_range is large enough -- "
        "it must be "
        "at least 3, and 10 or more is suggested -- or "
        "check for restrictive firewall settings or a bad router! UDP base is %u, range is %u and "
        "current suggestion is %u.",
        errno, errorstring, config.udp_port_base, config.udp_port_range, desired_port);
  }

  uint16_t sport;
  SOCKADDR local;
  socklen_t local_len = sizeof(local);
  getsockname(local_socket, (struct sockaddr *)&local, &local_len);
#ifdef AF_INET6
  if (local.SAFAMILY == AF_INET6) {
    struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)&local;
    sport = ntohs(sa6->sin6_port);
  } else
#endif
  {
    struct sockaddr_in *sa = (struct sockaddr_in *)&local;
    sport = ntohs(sa->sin_port);
  }
  *sock = local_socket;
  return sport;
}

int get_requested_connection_state_to_output() { return requested_connection_state_to_output; }

void set_requested_connection_state_to_output(int v) { requested_connection_state_to_output = v; }

char *generate_preliminary_string(char *buffer, size_t buffer_length, double tss, double tsl,
                                  const char *filename, const int linenumber, const char *prefix) {
  char *insertion_point = buffer;
  if (config.debugger_show_elapsed_time) {
    snprintf(insertion_point, buffer_length, "% 20.9f", tss);
    insertion_point = insertion_point + strlen(insertion_point);
  }
  if (config.debugger_show_relative_time) {
    snprintf(insertion_point, buffer_length, "% 20.9f", tsl);
    insertion_point = insertion_point + strlen(insertion_point);
  }
  if (config.debugger_show_file_and_line) {
    snprintf(insertion_point, buffer_length, " \"%s:%d\"", filename, linenumber);
    insertion_point = insertion_point + strlen(insertion_point);
  }
  if (prefix) {
    snprintf(insertion_point, buffer_length, "%s", prefix);
    insertion_point = insertion_point + strlen(insertion_point);
  }
  return insertion_point;
}

void _die(const char *thefilename, const int linenumber, const char *format, ...) {
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);

  char b[16384];
  b[0] = 0;
  char *s;
  if (debuglev) {
    pthread_mutex_lock(&debug_timing_lock);
    uint64_t time_now = get_absolute_time_in_ns();
    uint64_t time_since_start = time_now - ns_time_at_startup;
    uint64_t time_since_last_debug_message = time_now - ns_time_at_last_debug_message;
    ns_time_at_last_debug_message = time_now;
    pthread_mutex_unlock(&debug_timing_lock);
    char *basec = strdup(thefilename);
    char *filename = basename(basec);
    s = generate_preliminary_string(b, sizeof(b), 1.0 * time_since_start / 1000000000,
                                    1.0 * time_since_last_debug_message / 1000000000, filename,
                                    linenumber, " *fatal error: ");
    free(basec);
  } else {
    strncpy(b, "fatal error: ", sizeof(b));
    s = b + strlen(b);
  }
  va_list args;
  va_start(args, format);
  vsnprintf(s, sizeof(b) - (s - b), format, args);
  va_end(args);
  sps_log(LOG_ERR, "%s", b);
  pthread_setcancelstate(oldState, NULL);
  type_of_exit_cleanup = TOE_emergency;
  exit(EXIT_FAILURE);
}

void _warn(const char *thefilename, const int linenumber, const char *format, ...) {
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  char b[16384];
  b[0] = 0;
  char *s;
  if (debuglev) {
    pthread_mutex_lock(&debug_timing_lock);
    uint64_t time_now = get_absolute_time_in_ns();
    uint64_t time_since_start = time_now - ns_time_at_startup;
    uint64_t time_since_last_debug_message = time_now - ns_time_at_last_debug_message;
    ns_time_at_last_debug_message = time_now;
    pthread_mutex_unlock(&debug_timing_lock);
    char *basec = strdup(thefilename);
    char *filename = basename(basec);
    s = generate_preliminary_string(b, sizeof(b), 1.0 * time_since_start / 1000000000,
                                    1.0 * time_since_last_debug_message / 1000000000, filename,
                                    linenumber, " *warning: ");
    free(basec);
  } else {
    strncpy(b, "warning: ", sizeof(b));
    s = b + strlen(b);
  }
  va_list args;
  va_start(args, format);
  vsnprintf(s, sizeof(b) - (s - b), format, args);
  va_end(args);
  sps_log(LOG_WARNING, "%s", b);
  pthread_setcancelstate(oldState, NULL);
}

void _debug(const char *thefilename, const int linenumber, int level, const char *format, ...) {
  if (level > debuglev)
    return;
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);

  char b[16384];
  b[0] = 0;
  pthread_mutex_lock(&debug_timing_lock);
  uint64_t time_now = get_absolute_time_in_ns();
  uint64_t time_since_start = time_now - ns_time_at_startup;
  uint64_t time_since_last_debug_message = time_now - ns_time_at_last_debug_message;
  ns_time_at_last_debug_message = time_now;
  pthread_mutex_unlock(&debug_timing_lock);
  char *basec = strdup(thefilename);
  char *filename = basename(basec);
  char *s = generate_preliminary_string(b, sizeof(b), 1.0 * time_since_start / 1000000000,
                                        1.0 * time_since_last_debug_message / 1000000000, filename,
                                        linenumber, " ");
  free(basec);
  va_list args;
  va_start(args, format);
  vsnprintf(s, sizeof(b) - (s - b), format, args);
  va_end(args);
  sps_log(LOG_INFO, b); // LOG_DEBUG is hard to read on macOS terminal
  pthread_setcancelstate(oldState, NULL);
}

void _inform(const char *thefilename, const int linenumber, const char *format, ...) {
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  char b[16384];
  b[0] = 0;
  char *s;
  if (debuglev) {
    pthread_mutex_lock(&debug_timing_lock);
    uint64_t time_now = get_absolute_time_in_ns();
    uint64_t time_since_start = time_now - ns_time_at_startup;
    uint64_t time_since_last_debug_message = time_now - ns_time_at_last_debug_message;
    ns_time_at_last_debug_message = time_now;
    pthread_mutex_unlock(&debug_timing_lock);
    char *basec = strdup(thefilename);
    char *filename = basename(basec);
    s = generate_preliminary_string(b, sizeof(b), 1.0 * time_since_start / 1000000000,
                                    1.0 * time_since_last_debug_message / 1000000000, filename,
                                    linenumber, " ");
    free(basec);
  } else {
    s = b;
  }
  va_list args;
  va_start(args, format);
  vsnprintf(s, sizeof(b) - (s - b), format, args);
  va_end(args);
  sps_log(LOG_INFO, "%s", b);
  pthread_setcancelstate(oldState, NULL);
}

void _debug_print_buffer(const char *thefilename, const int linenumber, int level, void *vbuf,
                         size_t buf_len) {
  if (level > debuglev)
    return;
  char *buf = (char *)vbuf;
  char *obf =
      malloc(buf_len * 4 + 1); // to be on the safe side -- 4 characters on average for each byte
  if (obf != NULL) {
    char *obfp = obf;
    unsigned int obfc;
    for (obfc = 0; obfc < buf_len; obfc++) {
      snprintf(obfp, 3, "%02X", buf[obfc]);
      obfp += 2;
      if (obfc != buf_len - 1) {
        if (obfc % 32 == 31) {
          snprintf(obfp, 5, " || ");
          obfp += 4;
        } else if (obfc % 16 == 15) {
          snprintf(obfp, 4, " | ");
          obfp += 3;
        } else if (obfc % 4 == 3) {
          snprintf(obfp, 2, " ");
          obfp += 1;
        }
      }
    };
    *obfp = 0;
    _debug(thefilename, linenumber, level, "%s", obf);
    free(obf);
  }
}

// The following two functions are adapted slightly and with thanks from Jonathan Leffler's sample
// code at
// https://stackoverflow.com/questions/675039/how-can-i-create-directory-tree-in-c-linux

int do_mkdir(const char *path, mode_t mode) {
  struct stat st;
  int status = 0;

  if (stat(path, &st) != 0) {
    /* Directory does not exist. EEXIST for race condition */
    if (mkdir(path, mode) != 0 && errno != EEXIST)
      status = -1;
  } else if (!S_ISDIR(st.st_mode)) {
    errno = ENOTDIR;
    status = -1;
  }

  return (status);
}

// mkpath - ensure all directories in path exist
// Algorithm takes the pessimistic view and works top-down to ensure
// each directory in path exists, rather than optimistically creating
// the last element and working backwards.

int mkpath(const char *path, mode_t mode) {
  char *pp;
  char *sp;
  int status;
  char *copypath = strdup(path);

  status = 0;
  pp = copypath;
  while (status == 0 && (sp = strchr(pp, '/')) != 0) {
    if (sp != pp) {
      /* Neither root nor double slash in path */
      *sp = '\0';
      status = do_mkdir(copypath, mode);
      *sp = '/';
    }
    pp = sp + 1;
  }
  if (status == 0)
    status = do_mkdir(path, mode);
  free(copypath);
  return (status);
}

#ifdef CONFIG_MBEDTLS
char *base64_enc(uint8_t *input, int length) {
  char *buf = NULL;
  size_t dlen = 0;
  int rc = mbedtls_base64_encode(NULL, 0, &dlen, input, length);
  if (rc && (rc != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL))
    debug(1, "Error %d getting length of base64 encode.", rc);
  else {
    buf = (char *)malloc(dlen);
    rc = mbedtls_base64_encode((unsigned char *)buf, dlen, &dlen, input, length);
    if (rc != 0)
      debug(1, "Error %d encoding base64.", rc);
  }
  return buf;
}

uint8_t *base64_dec(char *input, int *outlen) {
  // slight problem here is that Apple cut the padding off their challenges. We must restore it
  // before passing it in to the decoder, it seems
  uint8_t *buf = NULL;
  size_t dlen = 0;
  int inbufsize = ((strlen(input) + 3) / 4) * 4; // this is the size of the input buffer we will
                                                 // send to the decoder, but we need space for 3
                                                 // extra "="s and a NULL
  char *inbuf = malloc(inbufsize + 4);
  if (inbuf == 0)
    debug(1, "Can't malloc memory  for inbuf in base64_decode.");
  else {
    strcpy(inbuf, input);
    strcat(inbuf, "===");
    // debug(1,"base64_dec called with string \"%s\", length %d, filled string: \"%s\", length %d.",
    //		input,strlen(input),inbuf,inbufsize);
    int rc = mbedtls_base64_decode(NULL, 0, &dlen, (unsigned char *)inbuf, inbufsize);
    if (rc && (rc != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL))
      debug(1, "Error %d getting decode length, result is %d.", rc, dlen);
    else {
      // debug(1,"Decode size is %d.",dlen);
      buf = malloc(dlen);
      if (buf == 0)
        debug(1, "Can't allocate memory in base64_dec.");
      else {
        rc = mbedtls_base64_decode(buf, dlen, &dlen, (unsigned char *)inbuf, inbufsize);
        if (rc != 0)
          debug(1, "Error %d in base64_dec.", rc);
      }
    }
    free(inbuf);
  }
  *outlen = dlen;
  return buf;
}
#endif

#ifdef CONFIG_POLARSSL
char *base64_enc(uint8_t *input, int length) {
  char *buf = NULL;
  size_t dlen = 0;
  int rc = base64_encode(NULL, &dlen, input, length);
  if (rc && (rc != POLARSSL_ERR_BASE64_BUFFER_TOO_SMALL))
    debug(1, "Error %d getting length of base64 encode.", rc);
  else {
    buf = (char *)malloc(dlen);
    rc = base64_encode((unsigned char *)buf, &dlen, input, length);
    if (rc != 0)
      debug(1, "Error %d encoding base64.", rc);
  }
  return buf;
}

uint8_t *base64_dec(char *input, int *outlen) {
  // slight problem here is that Apple cut the padding off their challenges. We must restore it
  // before passing it in to the decoder, it seems
  uint8_t *buf = NULL;
  size_t dlen = 0;
  int inbufsize = ((strlen(input) + 3) / 4) * 4; // this is the size of the input buffer we will
                                                 // send to the decoder, but we need space for 3
                                                 // extra "="s and a NULL
  char *inbuf = malloc(inbufsize + 4);
  if (inbuf == 0)
    debug(1, "Can't malloc memory  for inbuf in base64_decode.");
  else {
    strcpy(inbuf, input);
    strcat(inbuf, "===");
    // debug(1,"base64_dec called with string \"%s\", length %d, filled string: \"%s\", length
    // %d.",input,strlen(input),inbuf,inbufsize);
    int rc = base64_decode(buf, &dlen, (unsigned char *)inbuf, inbufsize);
    if (rc && (rc != POLARSSL_ERR_BASE64_BUFFER_TOO_SMALL))
      debug(1, "Error %d getting decode length, result is %d.", rc, dlen);
    else {
      // debug(1,"Decode size is %d.",dlen);
      buf = malloc(dlen);
      if (buf == 0)
        debug(1, "Can't allocate memory in base64_dec.");
      else {
        rc = base64_decode(buf, &dlen, (unsigned char *)inbuf, inbufsize);
        if (rc != 0)
          debug(1, "Error %d in base64_dec.", rc);
      }
    }
    free(inbuf);
  }
  *outlen = dlen;
  return buf;
}
#endif

#ifdef CONFIG_OPENSSL
char *base64_enc(uint8_t *input, int length) {
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  BIO *bmem, *b64;
  BUF_MEM *bptr;
  b64 = BIO_new(BIO_f_base64());
  bmem = BIO_new(BIO_s_mem());
  b64 = BIO_push(b64, bmem);
  BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
  BIO_write(b64, input, length);
  (void)BIO_flush(b64);
  BIO_get_mem_ptr(b64, &bptr);

  char *buf = (char *)malloc(bptr->length);
  if (buf == NULL)
    die("could not allocate memory for buf in base64_enc");
  if (bptr->length) {
    memcpy(buf, bptr->data, bptr->length - 1);
    buf[bptr->length - 1] = 0;
  }

  BIO_free_all(b64);

  pthread_setcancelstate(oldState, NULL);
  return buf;
}

uint8_t *base64_dec(char *input, int *outlen) {
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  BIO *bmem, *b64;
  int inlen = strlen(input);

  b64 = BIO_new(BIO_f_base64());
  BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
  bmem = BIO_new(BIO_s_mem());
  b64 = BIO_push(b64, bmem);

  // Apple cut the padding off their challenges; restore it
  BIO_write(bmem, input, inlen);
  while (inlen++ & 3)
    BIO_write(bmem, "=", 1);
  (void)BIO_flush(bmem);

  int bufsize = strlen(input) * 3 / 4 + 1;
  uint8_t *buf = malloc(bufsize);
  int nread;

  nread = BIO_read(b64, buf, bufsize);

  BIO_free_all(b64);

  *outlen = nread;
  pthread_setcancelstate(oldState, NULL);
  return buf;
}
#endif

static char super_secret_key[] =
    "-----BEGIN RSA PRIVATE KEY-----\n"
    "MIIEpQIBAAKCAQEA59dE8qLieItsH1WgjrcFRKj6eUWqi+bGLOX1HL3U3GhC/j0Qg90u3sG/1CUt\n"
    "wC5vOYvfDmFI6oSFXi5ELabWJmT2dKHzBJKa3k9ok+8t9ucRqMd6DZHJ2YCCLlDRKSKv6kDqnw4U\n"
    "wPdpOMXziC/AMj3Z/lUVX1G7WSHCAWKf1zNS1eLvqr+boEjXuBOitnZ/bDzPHrTOZz0Dew0uowxf\n"
    "/+sG+NCK3eQJVxqcaJ/vEHKIVd2M+5qL71yJQ+87X6oV3eaYvt3zWZYD6z5vYTcrtij2VZ9Zmni/\n"
    "UAaHqn9JdsBWLUEpVviYnhimNVvYFZeCXg/IdTQ+x4IRdiXNv5hEewIDAQABAoIBAQDl8Axy9XfW\n"
    "BLmkzkEiqoSwF0PsmVrPzH9KsnwLGH+QZlvjWd8SWYGN7u1507HvhF5N3drJoVU3O14nDY4TFQAa\n"
    "LlJ9VM35AApXaLyY1ERrN7u9ALKd2LUwYhM7Km539O4yUFYikE2nIPscEsA5ltpxOgUGCY7b7ez5\n"
    "NtD6nL1ZKauw7aNXmVAvmJTcuPxWmoktF3gDJKK2wxZuNGcJE0uFQEG4Z3BrWP7yoNuSK3dii2jm\n"
    "lpPHr0O/KnPQtzI3eguhe0TwUem/eYSdyzMyVx/YpwkzwtYL3sR5k0o9rKQLtvLzfAqdBxBurciz\n"
    "aaA/L0HIgAmOit1GJA2saMxTVPNhAoGBAPfgv1oeZxgxmotiCcMXFEQEWflzhWYTsXrhUIuz5jFu\n"
    "a39GLS99ZEErhLdrwj8rDDViRVJ5skOp9zFvlYAHs0xh92ji1E7V/ysnKBfsMrPkk5KSKPrnjndM\n"
    "oPdevWnVkgJ5jxFuNgxkOLMuG9i53B4yMvDTCRiIPMQ++N2iLDaRAoGBAO9v//mU8eVkQaoANf0Z\n"
    "oMjW8CN4xwWA2cSEIHkd9AfFkftuv8oyLDCG3ZAf0vrhrrtkrfa7ef+AUb69DNggq4mHQAYBp7L+\n"
    "k5DKzJrKuO0r+R0YbY9pZD1+/g9dVt91d6LQNepUE/yY2PP5CNoFmjedpLHMOPFdVgqDzDFxU8hL\n"
    "AoGBANDrr7xAJbqBjHVwIzQ4To9pb4BNeqDndk5Qe7fT3+/H1njGaC0/rXE0Qb7q5ySgnsCb3DvA\n"
    "cJyRM9SJ7OKlGt0FMSdJD5KG0XPIpAVNwgpXXH5MDJg09KHeh0kXo+QA6viFBi21y340NonnEfdf\n"
    "54PX4ZGS/Xac1UK+pLkBB+zRAoGAf0AY3H3qKS2lMEI4bzEFoHeK3G895pDaK3TFBVmD7fV0Zhov\n"
    "17fegFPMwOII8MisYm9ZfT2Z0s5Ro3s5rkt+nvLAdfC/PYPKzTLalpGSwomSNYJcB9HNMlmhkGzc\n"
    "1JnLYT4iyUyx6pcZBmCd8bD0iwY/FzcgNDaUmbX9+XDvRA0CgYEAkE7pIPlE71qvfJQgoA9em0gI\n"
    "LAuE4Pu13aKiJnfft7hIjbK+5kyb3TysZvoyDnb3HOKvInK7vXbKuU4ISgxB2bB3HcYzQMGsz1qJ\n"
    "2gG0N5hvJpzwwhbhXqFKA4zaaSrw622wDniAK5MlIE0tIAKKP4yxNGjoD2QYjhBGuhvkWKY=\n"
    "-----END RSA PRIVATE KEY-----\0";

#ifdef CONFIG_OPENSSL
uint8_t *rsa_apply(uint8_t *input, int inlen, int *outlen, int mode) {
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  uint8_t *out = NULL;
  BIO *bmem = BIO_new_mem_buf(super_secret_key, -1);                  // 1.0.2
  EVP_PKEY *rsaKey = PEM_read_bio_PrivateKey(bmem, NULL, NULL, NULL); // 1.0.2
  BIO_free(bmem);
  size_t ol = 0;
  if (rsaKey != NULL) {
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(rsaKey, NULL); // 1.0.2
    if (ctx != NULL) {

      switch (mode) {
      case RSA_MODE_AUTH: {
        if (EVP_PKEY_sign_init(ctx) > 0) {                                                // 1.0.2
          if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING) > 0) {                 // 1.0.2
            if (EVP_PKEY_sign(ctx, NULL, &ol, (const unsigned char *)input, inlen) > 0) { // 1.0.2
              out = (unsigned char *)malloc(ol);
              if (EVP_PKEY_sign(ctx, out, &ol, (const unsigned char *)input, inlen) > 0) { // 1.0.2
                debug(3, "success with output length of %lu.", ol);
              } else {
                debug(1, "error 2 \"%s\" with EVP_PKEY_sign:",
                      ERR_error_string(ERR_get_error(), NULL));
              }
            } else {
              debug(1,
                    "error 1 \"%s\" with EVP_PKEY_sign:", ERR_error_string(ERR_get_error(), NULL));
            }
          } else {
            debug(1, "error \"%s\" with EVP_PKEY_CTX_set_rsa_padding:",
                  ERR_error_string(ERR_get_error(), NULL));
          }
        } else {
          debug(1,
                "error \"%s\" with EVP_PKEY_sign_init:", ERR_error_string(ERR_get_error(), NULL));
        }
      } break;
      case RSA_MODE_KEY: {
        if (EVP_PKEY_decrypt_init(ctx) > 0) {
          if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) > 0) {
            /* Determine buffer length */
            if (EVP_PKEY_decrypt(ctx, NULL, &ol, (const unsigned char *)input, inlen) > 0) {
              out = OPENSSL_malloc(ol);
              if (out != NULL) {
                if (EVP_PKEY_decrypt(ctx, out, &ol, (const unsigned char *)input, inlen) > 0) {
                  debug(3, "decrypt success");
                } else {
                  debug(1, "error \"%s\" with EVP_PKEY_decrypt:",
                        ERR_error_string(ERR_get_error(), NULL));
                }
              } else {
                debug(1, "OPENSSL_malloc failed");
              }
            } else {
              debug(1,
                    "error \"%s\" with EVP_PKEY_decrypt:", ERR_error_string(ERR_get_error(), NULL));
            }
          } else {
            debug(1, "error \"%s\" with EVP_PKEY_CTX_set_rsa_padding:",
                  ERR_error_string(ERR_get_error(), NULL));
          }
        } else {
          debug(1, "error \"%s\" with EVP_PKEY_decrypt_init:",
                ERR_error_string(ERR_get_error(), NULL));
        }
      } break;
      default:
        debug(1, "Unknown mode");
        break;
      }
      EVP_PKEY_CTX_free(ctx); // 1.0.2
    } else {
      printf("error \"%s\" with EVP_PKEY_CTX_new:\n", ERR_error_string(ERR_get_error(), NULL));
    }
    EVP_PKEY_free(rsaKey); // 1.0.2
  } else {
    printf("error \"%s\" with EVP_PKEY_new:\n", ERR_error_string(ERR_get_error(), NULL));
  }
  *outlen = ol;
  pthread_setcancelstate(oldState, NULL);
  return out;
}
#endif

#ifdef CONFIG_MBEDTLS
uint8_t *rsa_apply(uint8_t *input, int inlen, int *outlen, int mode) {
  mbedtls_pk_context pkctx;
  mbedtls_rsa_context *trsa;
  const char *pers = "rsa_encrypt";
  size_t olen = *outlen;
  int rc;

  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctr_drbg;

  mbedtls_entropy_init(&entropy);

  mbedtls_ctr_drbg_init(&ctr_drbg);
  mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)pers,
                        strlen(pers));

  mbedtls_pk_init(&pkctx);

#if MBEDTLS_VERSION_MAJOR == 3
  rc = mbedtls_pk_parse_key(&pkctx, (unsigned char *)super_secret_key, sizeof(super_secret_key),
                            NULL, 0, mbedtls_ctr_drbg_random, &ctr_drbg);
#else
  rc = mbedtls_pk_parse_key(&pkctx, (unsigned char *)super_secret_key, sizeof(super_secret_key), 
                            NULL, 0);

#endif
  if (rc != 0)
    debug(1, "Error %d reading the private key.", rc);

  uint8_t *outbuf = NULL;
  trsa = mbedtls_pk_rsa(pkctx);

  switch (mode) {
  case RSA_MODE_AUTH:
    mbedtls_rsa_set_padding(trsa, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_NONE);    
    outbuf = malloc(trsa->MBEDTLS_PRIVATE_V3_ONLY(len));
#if MBEDTLS_VERSION_MAJOR == 3
    rc = mbedtls_pk_sign(&pkctx, MBEDTLS_MD_NONE, input, inlen, outbuf, mbedtls_pk_get_len(&pkctx), &olen, mbedtls_ctr_drbg_random, &ctr_drbg);
    *outlen = olen;
#else
    rc = mbedtls_rsa_pkcs1_encrypt(trsa, mbedtls_ctr_drbg_random, &ctr_drbg, MBEDTLS_RSA_PRIVATE,
                                   inlen, input, outbuf);
    *outlen = trsa->len;
#endif
    if (rc != 0)
      debug(1, "mbedtls_pk_encrypt error %d.", rc);
    break;
  case RSA_MODE_KEY:
    mbedtls_rsa_set_padding(trsa, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA1);
    outbuf = malloc(trsa->MBEDTLS_PRIVATE_V3_ONLY(len));
#if MBEDTLS_VERSION_MAJOR == 3
    rc = mbedtls_rsa_pkcs1_decrypt(trsa, mbedtls_ctr_drbg_random, &ctr_drbg,
                                   &olen, input, outbuf, trsa->MBEDTLS_PRIVATE_V3_ONLY(len));
#else
    rc = mbedtls_rsa_pkcs1_decrypt(trsa, mbedtls_ctr_drbg_random, &ctr_drbg, MBEDTLS_RSA_PRIVATE,
                                   &olen, input, outbuf, trsa->len);
#endif
    if (rc != 0)
      debug(1, "mbedtls_pk_decrypt error %d.", rc);
    *outlen = olen;
    break;
  default:
    die("bad rsa mode");
  }

  mbedtls_ctr_drbg_free(&ctr_drbg);
  mbedtls_entropy_free(&entropy);
  mbedtls_pk_free(&pkctx);
  return outbuf;
}
#endif

#ifdef CONFIG_POLARSSL
uint8_t *rsa_apply(uint8_t *input, int inlen, int *outlen, int mode) {
  rsa_context trsa;
  const char *pers = "rsa_encrypt";
  int rc;

  entropy_context entropy;
  ctr_drbg_context ctr_drbg;
  entropy_init(&entropy);
  if ((rc = ctr_drbg_init(&ctr_drbg, entropy_func, &entropy, (const unsigned char *)pers,
                          strlen(pers))) != 0)
    debug(1, "ctr_drbg_init returned %d\n", rc);

  rsa_init(&trsa, RSA_PKCS_V21, POLARSSL_MD_SHA1); // padding and hash id get overwritten
  // BTW, this seems to reset a lot of parameters in the rsa_context
  rc = x509parse_key(&trsa, (unsigned char *)super_secret_key, strlen(super_secret_key), NULL, 0);
  if (rc != 0)
    debug(1, "Error %d reading the private key.");

  uint8_t *out = NULL;

  switch (mode) {
  case RSA_MODE_AUTH:
    trsa.padding = RSA_PKCS_V15;
    trsa.hash_id = POLARSSL_MD_NONE;
    debug(2, "rsa_apply encrypt");
    out = malloc(trsa.len);
    rc = rsa_pkcs1_encrypt(&trsa, ctr_drbg_random, &ctr_drbg, RSA_PRIVATE, inlen, input, out);
    if (rc != 0)
      debug(1, "rsa_pkcs1_encrypt error %d.", rc);
    *outlen = trsa.len;
    break;
  case RSA_MODE_KEY:
    debug(2, "rsa_apply decrypt");
    trsa.padding = RSA_PKCS_V21;
    trsa.hash_id = POLARSSL_MD_SHA1;
    out = malloc(trsa.len);
#if POLARSSL_VERSION_NUMBER >= 0x01020900
    rc = rsa_pkcs1_decrypt(&trsa, ctr_drbg_random, &ctr_drbg, RSA_PRIVATE, (size_t *)outlen, input,
                           out, trsa.len);
#else
    rc = rsa_pkcs1_decrypt(&trsa, RSA_PRIVATE, outlen, input, out, trsa.len);
#endif
    if (rc != 0)
      debug(1, "decrypt error %d.", rc);
    break;
  default:
    die("bad rsa mode");
  }
  rsa_free(&trsa);
  debug(2, "rsa_apply exit");
  return out;
}
#endif

int config_set_lookup_bool(config_t *cfg, char *where, int *dst) {
  const char *str = 0;
  if (config_lookup_string(cfg, where, &str)) {
    if (strcasecmp(str, "no") == 0) {
      (*dst) = 0;
      return 1;
    } else if (strcasecmp(str, "yes") == 0) {
      (*dst) = 1;
      return 1;
    } else {
      die("Invalid %s option choice \"%s\". It should be \"yes\" or \"no\"", where, str);
      return 0;
    }
  } else {
    return 0;
  }
}

void command_set_volume(double volume) {
  // this has a cancellation point if waiting is enabled
  if (config.cmd_set_volume) {
    /*Spawn a child to run the program.*/
    pid_t pid = fork();
    if (pid == 0) { /* child process */
      size_t command_buffer_size = strlen(config.cmd_set_volume) + 32;
      char *command_buffer = (char *)malloc(command_buffer_size);
      if (command_buffer == NULL) {
        inform("Couldn't allocate memory for set_volume argument string");
      } else {
        memset(command_buffer, 0, command_buffer_size);
        snprintf(command_buffer, command_buffer_size, "%s %f", config.cmd_set_volume, volume);
        // debug(1,"command_buffer is \"%s\".",command_buffer);
        int argC;
        char **argV;
        // debug(1,"set_volume command found.");
        if (poptParseArgvString(command_buffer, &argC, (const char ***)&argV) != 0) {
          // note that argV should be free()'d after use, but we expect this fork to exit
          // eventually.
          warn("Can't decipher on-set-volume command arguments \"%s\".", command_buffer);
          free(argV);
          free(command_buffer);
        } else {
          free(command_buffer);
          // debug(1,"Executing on-set-volume command %s with %d arguments.",argV[0],argC);
          execv(argV[0], argV);
          warn("Execution of on-set-volume command \"%s\" failed to start", config.cmd_set_volume);
          // debug(1, "Error executing on-set-volume command %s", config.cmd_set_volume);
          _exit(EXIT_FAILURE); /* only if execv fails */
        }
      }
      _exit(EXIT_SUCCESS);
    } else {
      if (config.cmd_blocking) { /* pid!=0 means parent process and if blocking is true, wait for
                                    process to finish */
        pid_t rc = waitpid(pid, 0, 0); /* wait for child to exit */
        if (rc != pid) {
          warn("Execution of on-set-volume command returned an error.");
          debug(1, "on-set-volume command %s finished with error %d", config.cmd_set_volume, errno);
        }
      }
      // debug(1,"Continue after on-set-volume command");
    }
  }
}

void command_start(void) {
  // this has a cancellation point if waiting is enabled or a response is awaited
  if (config.cmd_start) {
    pid_t pid;
    int pipes[2];

    if (config.cmd_start_returns_output && pipe(pipes) != 0) {
      warn("Unable to allocate pipe for popen of start command.");
      debug(1, "pipe finished with error %d", errno);
      return;
    }
    /*Spawn a child to run the program.*/
    pid = fork();
    if (pid == 0) { /* child process */
      int argC;
      char **argV;

      if (config.cmd_start_returns_output) {
        close(pipes[0]);
        if (dup2(pipes[1], 1) < 0) {
          warn("Unable to reopen pipe as stdout for popen of start command");
          debug(1, "dup2 finished with error %d", errno);
          close(pipes[1]);
          return;
        }
      }

      // debug(1,"on-start command found.");
      if (poptParseArgvString(config.cmd_start, &argC, (const char ***)&argV) !=
          0) // note that argV should be free()'d after use, but we expect this fork to exit
             // eventually.
        debug(1, "Can't decipher on-start command arguments");
      else {
        // debug(1,"Executing on-start command %s with %d arguments.",argV[0],argC);
        execv(argV[0], argV);
        warn("Execution of on-start command failed to start");
        debug(1, "Error executing on-start command %s", config.cmd_start);
        _exit(EXIT_FAILURE); /* only if execv fails */
      }
    } else {
      if (config.cmd_blocking || config.cmd_start_returns_output) { /* pid!=0 means parent process
                                    and if blocking is true, wait for
                                    process to finish */
        pid_t rc = waitpid(pid, 0, 0);                              /* wait for child to exit */
        if ((rc != pid) && (errno != ECHILD)) {
          // In this context, ECHILD means that the child process has already completed, I think!
          warn("Execution of on-start command returned an error.");
          debug(1, "on-start command %s finished with error %d", config.cmd_start, errno);
        }
        if (config.cmd_start_returns_output) {
          static char buffer[256];
          int len;
          close(pipes[1]);
          len = read(pipes[0], buffer, 255);
          close(pipes[0]);
          buffer[len] = '\0';
          if (buffer[len - 1] == '\n')
            buffer[len - 1] = '\0'; // strip trailing newlines
          debug(1, "received '%s' as the device to use from the on-start command", buffer);
#ifdef CONFIG_ALSA
          set_alsa_out_dev(buffer);
#endif
        }
      }
      // debug(1,"Continue after on-start command");
    }
  }
}
void command_execute(const char *command, const char *extra_argument, const int block) {
  // this has a cancellation point if waiting is enabled
  if (command) {
    char new_command_buffer[2048];
    char *full_command = (char *)command;
    if (extra_argument != NULL) {
      memset(new_command_buffer, 0, sizeof(new_command_buffer));
      snprintf(new_command_buffer, sizeof(new_command_buffer), "%s %s", command, extra_argument);
      full_command = new_command_buffer;
    }

    /*Spawn a child to run the program.*/
    pid_t pid = fork();
    if (pid == 0) { /* child process */
      int argC;
      char **argV;
      if (poptParseArgvString(full_command, &argC, (const char ***)&argV) !=
          0) // note that argV should be free()'d after use, but we expect this fork to exit
             // eventually.
        debug(1, "Can't decipher command arguments in \"%s\".", full_command);
      else {
        // debug(1,"Executing command %s",full_command);
        execv(argV[0], argV);
        warn("Execution of command \"%s\" failed to start", full_command);
        debug(1, "Error executing command \"%s\".", full_command);
        _exit(EXIT_FAILURE); /* only if execv fails */
      }
    } else {
      if (block) { /* pid!=0 means parent process and if blocking is true, wait for
                                    process to finish */
        pid_t rc = waitpid(pid, 0, 0); /* wait for child to exit */
        if ((rc != pid) && (errno != ECHILD)) {
          // In this context, ECHILD means that the child process has already completed, I think!
          warn("Execution of command \"%s\" returned an error.", full_command);
          debug(1, "Command \"%s\" finished with error %d", full_command, errno);
        }
      }
      // debug(1,"Continue after on-unfixable command");
    }
  }
}

void command_stop(void) {
  // this has a cancellation point if waiting is enabled
  if (config.cmd_stop)
    command_execute(config.cmd_stop, "", config.cmd_blocking);
}

// this is for reading an unsigned 32 bit number, such as an RTP timestamp

uint32_t uatoi(const char *nptr) {
  uint64_t llint = atoll(nptr);
  uint32_t r = llint;
  return r;
}

// clang-format off

// Given an AirPlay volume (0 to -30) and the highest and lowest attenuations available in the mixer,
// the *vol2attn functions return anmattenuation depending on the AirPlay volume
// and the function's transfer function.

// Note that the max_db and min_db are given as dB*100

// clang-format on

double flat_vol2attn(double vol, long max_db, long min_db) {
  // clang-format off

// This "flat" volume control profile has the property that a given change in the AirPlay volume
// always results in the same change in output dB. For example, if a change of AirPlay volume
// from 0 to -4 resulted in a 7 dB change, then a change in AirPlay volume from -20 to -24
// would also result in a 7 dB change.

  // clang-format on
  double vol_setting = min_db; // if all else fails, set this, for safety

  if ((vol <= 0.0) && (vol >= -30.0)) {
    vol_setting = ((max_db - min_db) * (30.0 + vol) / 30) + min_db;
    // debug(2, "Linear profile Volume Setting: %f in range %ld to %ld.", vol_setting, min_db,
    // max_db);
  } else if (vol != -144.0) {
    debug(1,
          "flat_vol2attn volume request value %f is out of range: should be from 0.0 to -30.0 or "
          "-144.0.",
          vol);
  }
  return vol_setting;
}

double dasl_tapered_vol2attn(double vol, long max_db, long min_db) {
  // clang-format off

// The "dasl_tapered" volume control profile has the property that halving the AirPlay volume (the "vol" parameter)
// reduces the output level by 10 dB, which corresponds to roughly halving the perceived volume.

// For example, if the AirPlay volume goes from 0.0 to -15.0, the output level will decrease by 10 dB.
// Halving the AirPlay volume again, from -15 to -22.5, will decrease output by a further 10 dB.
// Reducing the AirPlay volume by half again, this time from -22.5 to -25.25 decreases the output by a further 10 dB,
// meaning that at AirPlay volume -25.25, the volume is decreased 30 dB.

// If the attenuation range of the mixer is restricted -- for example, if it is just 30 dB --
// the output level would reach its minimum before the AirPlay volume reached its minimum.
// This would result in part of the AirPlay volume control's range where
// changing the AirPlay volume would make no difference to the output level.

// In the example of an attenuator with a range of 00.dB to -30.0dB, this
// "dead zone" would be from AirPlay volume -30.0 to -25.25,
// i.e. about one sixth of its -30.0 to 0.0 travel.

// To work around this, the "flat" output level is used if it gives a
// higher output dB value than the calculation described above.
// If the device's attenuation range is over about 50 dB,
// the flat output level will hardly be needed at all.

  // clang-format on
  double vol_setting = min_db; // if all else fails, set this, for safety

  if ((vol <= 0.0) && (vol >= -30.0)) {
    double vol_pct = 1 - (vol / -30.0); // This will be in the range [0, 1]
    if (vol_pct <= 0) {
      return min_db;
    }

    double flat_setting = min_db + (max_db - min_db) * vol_pct;
    vol_setting =
        max_db + 1000 * log10(vol_pct) / log10(2); // This will be in the range [-inf, max_db]
    if (vol_setting < flat_setting) {
      debug(3,
            "dasl_tapered_vol2attn returning a flat setting of %f for AirPlay volume %f instead of "
            "a tapered setting of %f in a range from %f to %f.",
            flat_setting, vol, vol_setting, 1.0 * min_db, 1.0 * max_db);
      return flat_setting;
    }
    if (vol_setting > max_db) {
      return max_db;
    }
    return vol_setting;
  } else if (vol != -144.0) {
    debug(1,
          "dasl_tapered volume request value %f is out of range: should be from 0.0 to -30.0 or "
          "-144.0.",
          vol);
  }
  return vol_setting;
}

double vol2attn(double vol, long max_db, long min_db) {

  // See http://tangentsoft.net/audio/atten.html for data on good attenuators.

  // We want a smooth attenuation function, like, for example, the ALPS RK27 Potentiometer transfer
  // functions referred to at the link above.

  // We use a little coordinate geometry to build a transfer function from the volume passed in to
  // the device's dynamic range. (See the diagram in the documents folder.) The x axis is the
  // "volume in" which will be from -30 to 0. The y axis will be the "volume out" which will be from
  // the bottom of the range to the top. We build the transfer function from one or more lines. We
  // characterise each line with two numbers: the first is where on x the line starts when y=0 (x
  // can be from 0 to -30); the second is where on y the line stops when when x is -30. thus, if the
  // line was characterised as {0,-30}, it would be an identity transfer. Assuming, for example, a
  // dynamic range of lv=-60 to hv=0 Typically we'll use three lines -- a three order transfer
  // function First: {0,30} giving a gentle slope -- the 30 comes from half the dynamic range
  // Second: {-5,-30-(lv+30)/2} giving a faster slope from y=0 at x=-12 to y=-42.5 at x=-30
  // Third: {-17,lv} giving a fast slope from y=0 at x=-19 to y=-60 at x=-30

#define order 3

  double vol_setting = 0;

  if ((vol <= 0.0) && (vol >= -30.0)) {
    long range_db = max_db - min_db; // this will be a positive number
    // debug(1,"Volume min %ddB, max %ddB, range %ddB.",min_db,max_db,range_db);
    // double first_slope = -3000.0; // this is the slope of the attenuation at the high end -- 30dB
    // for the full rotation.
    double first_slope =
        -range_db /
        2; // this is the slope of the attenuation at the high end -- 30dB for the full rotation.
    if (-range_db > first_slope)
      first_slope = range_db;
    double lines[order][2] = {
        {0, first_slope}, {-5, first_slope - (range_db + first_slope) / 2}, {-17, -range_db}};
    int i;
    for (i = 0; i < order; i++) {
      if (vol <= lines[i][0]) {
        if ((-30 - lines[i][0]) == 0.0)
          die("(-30 - lines[%d][0]) == 0.0!", i);
        double tvol = lines[i][1] * (vol - lines[i][0]) / (-30 - lines[i][0]);
        // debug(1,"On line %d, end point of %f, input vol %f yields output vol
        // %f.",i,lines[i][1],vol,tvol);
        if (tvol < vol_setting)
          vol_setting = tvol;
      }
    }
    vol_setting += max_db;
  } else if (vol != -144.0) {
    debug(1, "vol2attn request value %f is out of range: should be from 0.0 to -30.0 or -144.0.",
          vol);
    vol_setting = min_db; // for safety, return the lowest setting...
  } else {
    vol_setting = min_db; // for safety, return the lowest setting...
  }
  // debug(1,"returning an attenuation of %f.",vol_setting);
  // debug(2, "Standard profile Volume Setting for Airplay vol %f: %f in range %ld to %ld.", vol,
  //      vol_setting, min_db, max_db);
  return vol_setting;
}

uint64_t get_monotonic_time_in_ns() {
  uint64_t time_now_ns;

#ifdef COMPILE_FOR_LINUX_AND_FREEBSD_AND_CYGWIN_AND_OPENBSD
  struct timespec tn;
  clock_gettime(CLOCK_MONOTONIC, &tn);
  uint64_t tnnsec = tn.tv_sec;
  tnnsec = tnnsec * 1000000000;
  uint64_t tnjnsec = tn.tv_nsec;
  time_now_ns = tnnsec + tnjnsec;
#endif

#ifdef COMPILE_FOR_OSX
  uint64_t time_now_mach;
  static mach_timebase_info_data_t sTimebaseInfo = {0, 0};

  // this actually give you a monotonic clock
  // see https://news.ycombinator.com/item?id=6303755
  time_now_mach = mach_absolute_time();

  // If this is the first time we've run, get the timebase.
  // We can use denom == 0 to indicate that sTimebaseInfo is
  // uninitialised because it makes no sense to have a zero
  // denominator in a fraction.

  if (sTimebaseInfo.denom == 0) {
    debug(1, "Mac initialise timebase info.");
    (void)mach_timebase_info(&sTimebaseInfo);
  }

  if (sTimebaseInfo.denom == 0)
    die("could not initialise Mac timebase info in get_monotonic_time_in_ns().");

  // Do the maths. We hope that the multiplication doesn't
  // overflow; the price you pay for working in fixed point.

  // this gives us nanoseconds
  time_now_ns = time_now_mach * sTimebaseInfo.numer / sTimebaseInfo.denom;
#endif

  return time_now_ns;
}

#ifdef COMPILE_FOR_LINUX_AND_FREEBSD_AND_CYGWIN_AND_OPENBSD
// Not defined for macOS
uint64_t get_realtime_in_ns() {
  uint64_t time_now_ns;
  struct timespec tn;
  clock_gettime(CLOCK_REALTIME, &tn);
  uint64_t tnnsec = tn.tv_sec;
  tnnsec = tnnsec * 1000000000;
  uint64_t tnjnsec = tn.tv_nsec;
  time_now_ns = tnnsec + tnjnsec;
  return time_now_ns;
}
#endif

uint64_t get_absolute_time_in_ns() {
  // CLOCK_MONOTONIC_RAW/CLOCK_MONOTONIC in Linux/FreeBSD etc, monotonic in MacOSX
  uint64_t time_now_ns;

#ifdef COMPILE_FOR_LINUX_AND_FREEBSD_AND_CYGWIN_AND_OPENBSD
  struct timespec tn;
#ifdef CLOCK_MONOTONIC_RAW
  clock_gettime(CLOCK_MONOTONIC_RAW, &tn);
#else
  clock_gettime(CLOCK_MONOTONIC, &tn);
#endif
  uint64_t tnnsec = tn.tv_sec;
  tnnsec = tnnsec * 1000000000;
  uint64_t tnjnsec = tn.tv_nsec;
  time_now_ns = tnnsec + tnjnsec;
#endif

#ifdef COMPILE_FOR_OSX
  uint64_t time_now_mach;
  static mach_timebase_info_data_t sTimebaseInfo = {0, 0};

  // this actually give you a monotonic clock
  time_now_mach = mach_absolute_time();

  // If this is the first time we've run, get the timebase.
  // We can use denom == 0 to indicate that sTimebaseInfo is
  // uninitialised because it makes no sense to have a zero
  // denominator in a fraction.

  if (sTimebaseInfo.denom == 0) {
    debug(1, "Mac initialise timebase info.");
    (void)mach_timebase_info(&sTimebaseInfo);
  }

  // Do the maths. We hope that the multiplication doesn't
  // overflow; the price you pay for working in fixed point.

  if (sTimebaseInfo.denom == 0)
    die("could not initialise Mac timebase info in get_absolute_time_in_ns().");

  // this gives us nanoseconds
  time_now_ns = time_now_mach * sTimebaseInfo.numer / sTimebaseInfo.denom;
#endif

  return time_now_ns;
}

int try_to_open_pipe_for_writing(const char *pathname) {
  // tries to open the pipe in non-blocking mode first.
  // if it succeeds, it sets it to blocking.
  // if not, it returns -1.

  int fdis = open(pathname, O_WRONLY | O_NONBLOCK); // open it in non blocking mode first

  // we check that it's not a "real" error. From the "man 2 open" page:
  // "ENXIO  O_NONBLOCK | O_WRONLY is set, the named file is a FIFO, and no process has the FIFO
  // open for reading." Which is okay.
  // This is checked by the caller.

  if (fdis >= 0) {
    // now we switch to blocking mode
    int flags = fcntl(fdis, F_GETFL);
    if (flags == -1) {
      char errorstring[1024];
      strerror_r(errno, (char *)errorstring, sizeof(errorstring));
      debug(1, "try_to_open_pipe -- error %d (\"%s\") getting flags of pipe: \"%s\".", errno,
            (char *)errorstring, pathname);
    } else {
      flags = fcntl(fdis, F_SETFL, flags & ~O_NONBLOCK);
      if (flags == -1) {
        char errorstring[1024];
        strerror_r(errno, (char *)errorstring, sizeof(errorstring));
        debug(1, "try_to_open_pipe -- error %d (\"%s\") unsetting NONBLOCK of pipe: \"%s\".", errno,
              (char *)errorstring, pathname);
      }
    }
  }
  return fdis;
}

/* from
 * http://coding.debuntu.org/c-implementing-str_replace-replace-all-occurrences-substring#comment-722
 */

char *str_replace(const char *string, const char *substr, const char *replacement) {
  char *tok = NULL;
  char *newstr = NULL;
  char *oldstr = NULL;
  char *head = NULL;

  /* if either substr or replacement is NULL, duplicate string a let caller handle it */
  if (substr == NULL || replacement == NULL)
    return strdup(string);
  newstr = strdup(string);
  head = newstr;
  if (head) {
    while ((tok = strstr(head, substr))) {
      oldstr = newstr;
      newstr = malloc(strlen(oldstr) - strlen(substr) + strlen(replacement) + 1);
      /*failed to alloc mem, free old string and return NULL */
      if (newstr == NULL) {
        free(oldstr);
        return NULL;
      }
      memcpy(newstr, oldstr, tok - oldstr);
      memcpy(newstr + (tok - oldstr), replacement, strlen(replacement));
      memcpy(newstr + (tok - oldstr) + strlen(replacement), tok + strlen(substr),
             strlen(oldstr) - strlen(substr) - (tok - oldstr));
      memset(newstr + strlen(oldstr) - strlen(substr) + strlen(replacement), 0, 1);
      /* move back head right after the last replacement */
      head = newstr + (tok - oldstr) + strlen(replacement);
      free(oldstr);
    }
  } else {
    die("failed to allocate memory in str_replace.");
  }
  return newstr;
}

/* from http://burtleburtle.net/bob/rand/smallprng.html */

// this is not thread-safe, so we need a mutex on it to use it properly.
// always lock use this when accessing the fp_time_at_last_debug_message

pthread_mutex_t r64_mutex = PTHREAD_MUTEX_INITIALIZER;

// typedef uint64_t u8;
typedef struct ranctx {
  uint64_t a;
  uint64_t b;
  uint64_t c;
  uint64_t d;
} ranctx;

static struct ranctx rx;

#define rot(x, k) (((x) << (k)) | ((x) >> (64 - (k))))
uint64_t ranval(ranctx *x) {
  uint64_t e = x->a - rot(x->b, 7);
  x->a = x->b ^ rot(x->c, 13);
  x->b = x->c + rot(x->d, 37);
  x->c = x->d + e;
  x->d = e + x->a;
  return x->d;
}

void raninit(ranctx *x, uint64_t seed) {
  uint64_t i;
  x->a = 0xf1ea5eed, x->b = x->c = x->d = seed;
  for (i = 0; i < 20; ++i) {
    (void)ranval(x);
  }
}

void r64init(uint64_t seed) { raninit(&rx, seed); }

uint64_t r64u() { return (ranval(&rx)); }

int64_t r64i() { return (ranval(&rx) >> 1); }

uint32_t nctohl(const uint8_t *p) { // read 4 characters from *p and do ntohl on them
  // this is to avoid possible aliasing violations
  uint32_t holder;
  memcpy(&holder, p, sizeof(holder));
  return ntohl(holder);
}

uint16_t nctohs(const uint8_t *p) { // read 2 characters from *p and do ntohs on them
  // this is to avoid possible aliasing violations
  uint16_t holder;
  memcpy(&holder, p, sizeof(holder));
  return ntohs(holder);
}

uint64_t nctoh64(const uint8_t *p) {
  uint32_t landing = nctohl(p); // get the high order 32 bits
  uint64_t vl = landing;
  vl = vl << 32;                          // shift them into the correct location
  landing = nctohl(p + sizeof(uint32_t)); // and the low order 32 bits
  uint64_t ul = landing;
  vl = vl + ul;
  return vl;
}

pthread_mutex_t barrier_mutex = PTHREAD_MUTEX_INITIALIZER;

void memory_barrier() {
  pthread_mutex_lock(&barrier_mutex);
  pthread_mutex_unlock(&barrier_mutex);
}

void sps_nanosleep(const time_t sec, const long nanosec) {
  struct timespec req, rem;
  int result;
  req.tv_sec = sec;
  req.tv_nsec = nanosec;
  do {
    result = nanosleep(&req, &rem);
    rem = req;
  } while ((result == -1) && (errno == EINTR));
  if (result == -1)
    debug(1, "Error in sps_nanosleep of %d sec and %ld nanoseconds: %d.", sec, nanosec, errno);
}

// Mac OS X doesn't have pthread_mutex_timedlock
// Also note that timing must be relative to CLOCK_REALTIME

/*
#ifdef COMPILE_FOR_LINUX_AND_FREEBSD_AND_CYGWIN_AND_OPENBSD
int sps_pthread_mutex_timedlock(pthread_mutex_t *mutex, useconds_t dally_time) {

  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  struct timespec timeoutTime;
  uint64_t wait_until_time = dally_time * 1000; // to nanoseconds
  uint64_t start_time = get_realtime_in_ns();   // this is from CLOCK_REALTIME
  wait_until_time = wait_until_time + start_time;
  uint64_t wait_until_sec = wait_until_time / 1000000000;
  uint64_t wait_until_nsec = wait_until_time % 1000000000;
  timeoutTime.tv_sec = wait_until_sec;
  timeoutTime.tv_nsec = wait_until_nsec;
  int r = pthread_mutex_timedlock(mutex, &timeoutTime);
  pthread_setcancelstate(oldState, NULL);
  return r;
}
#endif
#ifdef COMPILE_FOR_OSX
*/
int sps_pthread_mutex_timedlock(pthread_mutex_t *mutex, useconds_t dally_time) {

  // this would not be not pthread_cancellation safe because is contains a cancellation point
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  int time_to_wait = dally_time;
  int r = pthread_mutex_trylock(mutex);
  while ((r == EBUSY) && (time_to_wait > 0)) {
    int st = time_to_wait;
    if (st > 1000)
      st = 1000;
    sps_nanosleep(0, st * 1000); // this contains a cancellation point
    time_to_wait -= st;
    r = pthread_mutex_trylock(mutex);
  }
  pthread_setcancelstate(oldState, NULL);
  return r;
}
// #endif

int _debug_mutex_lock(pthread_mutex_t *mutex, useconds_t dally_time, const char *mutexname,
                      const char *filename, const int line, int debuglevel) {
  if ((debuglevel > debuglev) || (debuglevel == 0))
    return pthread_mutex_lock(mutex);
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  if (debuglevel != 0)
    _debug(filename, line, 3, "mutex_lock \"%s\".", mutexname); // only if you really ask for it!
  int result = sps_pthread_mutex_timedlock(mutex, dally_time);
  if (result == ETIMEDOUT) {
    _debug(
        filename, line, debuglevel,
        "mutex_lock \"%s\" failed to lock after %f ms -- now waiting unconditionally to lock it.",
        mutexname, dally_time * 1E-3);
    result = pthread_mutex_lock(mutex);
    if (result == 0)
      _debug(filename, line, debuglevel, " ...mutex_lock \"%s\" locked successfully.", mutexname);
    else
      _debug(filename, line, debuglevel, " ...mutex_lock \"%s\" exited with error code: %u",
             mutexname, result);
  }
  pthread_setcancelstate(oldState, NULL);
  return result;
}

int _debug_mutex_unlock(pthread_mutex_t *mutex, const char *mutexname, const char *filename,
                        const int line, int debuglevel) {
  if ((debuglevel > debuglev) || (debuglevel == 0))
    return pthread_mutex_unlock(mutex);
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  char dstring[1000];
  char errstr[512];
  memset(dstring, 0, sizeof(dstring));
  snprintf(dstring, sizeof(dstring), "%s:%d", filename, line);
  debug(debuglevel, "mutex_unlock \"%s\" at \"%s\".", mutexname, dstring);
  int r = pthread_mutex_unlock(mutex);
  if ((debuglevel != 0) && (r != 0))
    debug(1, "error %d: \"%s\" unlocking mutex \"%s\" at \"%s\".", r,
          strerror_r(r, errstr, sizeof(errstr)), mutexname, dstring);
  pthread_setcancelstate(oldState, NULL);
  return r;
}

void malloc_cleanup(void *arg) {
  // debug(1, "malloc cleanup freeing %" PRIxPTR ".", arg);
  free(arg);
}

#ifdef CONFIG_AIRPLAY_2
void plist_cleanup(void *arg) {
  // debug(1, "plist cleanup called.");
  plist_free((plist_t)arg);
}
#endif

void socket_cleanup(void *arg) {
  intptr_t fdp = (intptr_t)arg;
  debug(3, "socket_cleanup called for socket: %" PRIdPTR ".", fdp);
  close(fdp);
}

void cv_cleanup(void *arg) {
  // debug(1, "cv_cleanup called.");
  pthread_cond_t *cv = (pthread_cond_t *)arg;
  pthread_cond_destroy(cv);
}

void mutex_cleanup(void *arg) {
  // debug(1, "mutex_cleanup called.");
  pthread_mutex_t *mutex = (pthread_mutex_t *)arg;
  pthread_mutex_destroy(mutex);
}

void mutex_unlock(void *arg) { pthread_mutex_unlock((pthread_mutex_t *)arg); }

void rwlock_unlock(void *arg) { pthread_rwlock_unlock((pthread_rwlock_t *)arg); }

void thread_cleanup(void *arg) {
  debug(3, "thread_cleanup called.");
  pthread_t *thread = (pthread_t *)arg;
  pthread_cancel(*thread);
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  pthread_join(*thread, NULL);
  pthread_setcancelstate(oldState, NULL);
  debug(3, "thread_cleanup done.");
}

void pthread_cleanup_debug_mutex_unlock(void *arg) { pthread_mutex_unlock((pthread_mutex_t *)arg); }

char *get_version_string() {
  char *version_string = malloc(1024);
  if (version_string) {
#ifdef CONFIG_USE_GIT_VERSION_STRING
    if (git_version_string[0] != '\0')
      strcpy(version_string, git_version_string);
    else
#endif
      strcpy(version_string, PACKAGE_VERSION);
#ifdef CONFIG_AIRPLAY_2
    strcat(version_string, "-AirPlay2");
    char smiv[1024];
    snprintf(smiv, 1024, "-smi%u", NQPTP_SHM_STRUCTURES_VERSION);
    strcat(version_string, smiv);
#endif
#ifdef CONFIG_APPLE_ALAC
    strcat(version_string, "-alac");
#endif
#ifdef CONFIG_LIBDAEMON
    strcat(version_string, "-libdaemon");
#endif
#ifdef CONFIG_MBEDTLS
    strcat(version_string, "-mbedTLS");
#endif
#ifdef CONFIG_POLARSSL
    strcat(version_string, "-PolarSSL");
#endif
#ifdef CONFIG_OPENSSL
    strcat(version_string, "-OpenSSL");
#endif
#ifdef CONFIG_TINYSVCMDNS
    strcat(version_string, "-tinysvcmdns");
#endif
#ifdef CONFIG_AVAHI
    strcat(version_string, "-Avahi");
#endif
#ifdef CONFIG_DNS_SD
    strcat(version_string, "-dns_sd");
#endif
#ifdef CONFIG_EXTERNAL_MDNS
    strcat(version_string, "-external_mdns");
#endif
#ifdef CONFIG_ALSA
    strcat(version_string, "-ALSA");
#endif
#ifdef CONFIG_SNDIO
    strcat(version_string, "-sndio");
#endif
#ifdef CONFIG_JACK
    strcat(version_string, "-jack");
#endif
#ifdef CONFIG_AO
    strcat(version_string, "-ao");
#endif
#ifdef CONFIG_PA
    strcat(version_string, "-pa");
#endif
#ifdef CONFIG_PW
    strcat(version_string, "-pw");
#endif
#ifdef CONFIG_SOUNDIO
    strcat(version_string, "-soundio");
#endif
#ifdef CONFIG_DUMMY
    strcat(version_string, "-dummy");
#endif
#ifdef CONFIG_STDOUT
    strcat(version_string, "-stdout");
#endif
#ifdef CONFIG_PIPE
    strcat(version_string, "-pipe");
#endif
#ifdef CONFIG_SOXR
    strcat(version_string, "-soxr");
#endif
#ifdef CONFIG_CONVOLUTION
    strcat(version_string, "-convolution");
#endif
#ifdef CONFIG_METADATA
    strcat(version_string, "-metadata");
#endif
#ifdef CONFIG_MQTT
    strcat(version_string, "-mqtt");
#endif
#ifdef CONFIG_DBUS_INTERFACE
    strcat(version_string, "-dbus");
#endif
#ifdef CONFIG_MPRIS_INTERFACE
    strcat(version_string, "-mpris");
#endif
    strcat(version_string, "-sysconfdir:");
    strcat(version_string, SYSCONFDIR);
  }
  return version_string;
}

int64_t generate_zero_frames(char *outp, size_t number_of_frames, sps_format_t format,
                             int with_dither, int64_t random_number_in) {
  // return the last random number used
  // assuming the buffer has been assigned

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

  int64_t previous_random_number = random_number_in;
  char *p = outp;
  size_t sample_number;
  r64_lock; // the random number generator is not thread safe, so we need to lock it while using it
  for (sample_number = 0; sample_number < number_of_frames * 2; sample_number++) {

    int64_t hyper_sample = 0;
    int64_t r = r64i();

    int64_t tpdf = (r & dither_mask) - (previous_random_number & dither_mask);

    // add dither if permitted -- no need to check for clipping, as the sample is, uh, zero

    if (with_dither != 0)
      hyper_sample += tpdf;

    // move the result to the desired position in the int64_t
    char *op = p;
    int sample_length; // this is the length of the sample

    switch (format) {
    case SPS_FORMAT_S32:
      hyper_sample >>= (64 - 32);
      *(int32_t *)op = hyper_sample;
      sample_length = 4;
      break;
    case SPS_FORMAT_S32_LE:
      *op++ = (uint8_t)(hyper_sample >> (64 - 32));      // 32 bits, ls byte
      *op++ = (uint8_t)(hyper_sample >> (64 - 32 + 8));  // 32 bits, less significant middle byte
      *op++ = (uint8_t)(hyper_sample >> (64 - 32 + 16)); // 32 bits, more significant middle byte
      *op = (uint8_t)(hyper_sample >> (64 - 32 + 24));   // 32 bits, ms byte
      sample_length = 4;
      break;
    case SPS_FORMAT_S32_BE:
      *op++ = (uint8_t)(hyper_sample >> (64 - 32 + 24)); // 32 bits, ms byte
      *op++ = (uint8_t)(hyper_sample >> (64 - 32 + 16)); // 32 bits, more significant middle byte
      *op++ = (uint8_t)(hyper_sample >> (64 - 32 + 8));  // 32 bits, less significant middle byte
      *op = (uint8_t)(hyper_sample >> (64 - 32));        // 32 bits, ls byte
      sample_length = 4;
      break;
    case SPS_FORMAT_S24_3LE:
      *op++ = (uint8_t)(hyper_sample >> (64 - 24));     // 24 bits, ls byte
      *op++ = (uint8_t)(hyper_sample >> (64 - 24 + 8)); // 24 bits, middle byte
      *op = (uint8_t)(hyper_sample >> (64 - 24 + 16));  // 24 bits, ms byte
      sample_length = 3;
      break;
    case SPS_FORMAT_S24_3BE:
      *op++ = (uint8_t)(hyper_sample >> (64 - 24 + 16)); // 24 bits, ms byte
      *op++ = (uint8_t)(hyper_sample >> (64 - 24 + 8));  // 24 bits, middle byte
      *op = (uint8_t)(hyper_sample >> (64 - 24));        // 24 bits, ls byte
      sample_length = 3;
      break;
    case SPS_FORMAT_S24:
      hyper_sample >>= (64 - 24);
      *(int32_t *)op = hyper_sample;
      sample_length = 4;
      break;
    case SPS_FORMAT_S24_LE:
      *op++ = (uint8_t)(hyper_sample >> (64 - 24));      // 24 bits, ls byte
      *op++ = (uint8_t)(hyper_sample >> (64 - 24 + 8));  // 24 bits, middle byte
      *op++ = (uint8_t)(hyper_sample >> (64 - 24 + 16)); // 24 bits, ms byte
      *op = 0;
      sample_length = 4;
      break;
    case SPS_FORMAT_S24_BE:
      *op++ = 0;
      *op++ = (uint8_t)(hyper_sample >> (64 - 24 + 16)); // 24 bits, ms byte
      *op++ = (uint8_t)(hyper_sample >> (64 - 24 + 8));  // 24 bits, middle byte
      *op = (uint8_t)(hyper_sample >> (64 - 24));        // 24 bits, ls byte
      sample_length = 4;
      break;
    case SPS_FORMAT_S16_LE:
      *op++ = (uint8_t)(hyper_sample >> (64 - 16));
      *op++ = (uint8_t)(hyper_sample >> (64 - 16 + 8)); // 16 bits, ms byte
      sample_length = 2;
      break;
    case SPS_FORMAT_S16_BE:
      *op++ = (uint8_t)(hyper_sample >> (64 - 16 + 8)); // 16 bits, ms byte
      *op = (uint8_t)(hyper_sample >> (64 - 16));
      sample_length = 2;
      break;
    case SPS_FORMAT_S16:
      *(int16_t *)op = (int16_t)(hyper_sample >> (64 - 16));
      sample_length = 2;
      break;
    case SPS_FORMAT_S8:
      *op = (int8_t)(hyper_sample >> (64 - 8));
      sample_length = 1;
      break;
    case SPS_FORMAT_U8:
      *op = 128 + (uint8_t)(hyper_sample >> (64 - 8));
      sample_length = 1;
      break;
    default:
      sample_length = 0; // stop a compiler warning
      die("Unexpected SPS_FORMAT_* with index %d while outputting silence", format);
    }
    p += sample_length;
    previous_random_number = r;
  }
  r64_unlock;
  return previous_random_number;
}

// This will check the incoming string "s" of length "len" with the existing NUL-terminated string
// "str" and update "flag" accordingly.

// Note: if the incoming string length is zero, then the a NULL is used; i.e. no zero-length strings
// are stored.

// If the strings are different, the str is free'd and replaced by a pointer
// to a newly strdup'd string and the flag is set
// If they are the same, the flag is cleared

int string_update_with_size(char **str, int *flag, char *s, size_t len) {
  if (*str) {
    if ((s) && (len)) {
      if ((len != strlen(*str)) || (strncmp(*str, s, len) != 0)) {
        free(*str);
        //*str = strndup(s, len); // it seems that OpenWrt 12 doesn't have this
        char *p = malloc(len + 1);
        memcpy(p, s, len);
        p[len] = '\0';
        *str = p;
        *flag = 1;
      } else {
        *flag = 0;
      }
    } else {
      // old string is non-NULL, new string is NULL or length 0
      free(*str);
      *str = NULL;
      *flag = 1;
    }
  } else { // old string is NULL
    if ((s) && (len)) {
      //*str = strndup(s, len); // it seems that OpenWrt 12 doesn't have this
      char *p = malloc(len + 1);
      memcpy(p, s, len);
      p[len] = '\0';
      *str = p;
      *flag = 1;
    } else {
      // old string is NULL and new string is NULL or length 0
      *flag = 0; // so no change
    }
  }
  return *flag;
}

// from https://stackoverflow.com/questions/13663617/memdup-function-in-c, with thanks
void *memdup(const void *mem, size_t size) {
  void *out = malloc(size);

  if (out != NULL)
    memcpy(out, mem, size);

  return out;
}

// This will allocate memory and place the NUL-terminated hex character equivalent of
// the bytearray passed in whose length is given.
char *debug_malloc_hex_cstring(void *packet, size_t nread) {
  char *response = malloc(nread * 3 + 1);
  unsigned char *q = packet;
  char *obfp = response;
  size_t obfc;
  for (obfc = 0; obfc < nread; obfc++) {
    snprintf(obfp, 4, "%02x ", *q);
    obfp += 3; // two digit characters and a space
    q++;
  };
  obfp--; // overwrite the last space with a NUL
  *obfp = 0;
  return response;
}

// the difference between two unsigned 32-bit modulo values as a signed 32-bit result
// now, if the two numbers are constrained to be within 2^(n-1)-1 of one another,
// we can use their as a signed 2^n bit number which will be positive
// if the first number is the same or "after" the second, and
// negative otherwise

int32_t mod32Difference(uint32_t a, uint32_t b) {
  int32_t result = a - b;
  return result;
}

int get_device_id(uint8_t *id, int int_length) {

  uint64_t wait_time = 10000000000L; // wait up to this (ns) long to get a MAC address

  int response = -1;
  struct ifaddrs *ifaddr = NULL;
  struct ifaddrs *ifa = NULL;

  int i = 0;
  uint8_t *t = id;
  for (i = 0; i < int_length; i++) {
    *t++ = 0;
  }

  uint64_t wait_until = get_absolute_time_in_ns();
  wait_until = wait_until + wait_time;

  int64_t time_to_wait;
  do {
    if (getifaddrs(&ifaddr) == 0) {
      t = id;
      int found = 0;

      for (ifa = ifaddr; ((ifa != NULL) && (found == 0)); ifa = ifa->ifa_next) {
#ifdef AF_PACKET
        if ((ifa->ifa_addr) && (ifa->ifa_addr->sa_family == AF_PACKET)) {
          struct sockaddr_ll *s = (struct sockaddr_ll *)ifa->ifa_addr;
          if ((strcmp(ifa->ifa_name, "lo") != 0)) {
            found = 1;
            response = 0;
            for (i = 0; ((i < s->sll_halen) && (i < int_length)); i++) {
              *t++ = s->sll_addr[i];
            }
          }
        }
#else
#ifdef AF_LINK
        struct sockaddr_dl *sdl = (struct sockaddr_dl *)ifa->ifa_addr;
        if ((sdl) && (sdl->sdl_family == AF_LINK)) {
          if (sdl->sdl_type == IFT_ETHER) {
            found = 1;
            response = 0;
            uint8_t *s = (uint8_t *)LLADDR(sdl);
            for (i = 0; ((i < sdl->sdl_alen) && (i < int_length)); i++) {
              *t++ = *s++;
            }
          }
        }
#endif
#endif
      }
      freeifaddrs(ifaddr);
    }
    // wait a little time if we haven't got a response
    if (response != 0) {
      usleep(100000);
    }
    time_to_wait = wait_until - get_absolute_time_in_ns();
  } while ((response != 0) && (time_to_wait > 0));
  if (response != 0)
    warn("Can't create a device ID -- no valid MAC address can be found.");
  return response;
}
