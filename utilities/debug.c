/*
MIT License

Copyright (c) 2023--2025 Mike Brady 4265913+mikebrady@users.noreply.github.com

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "debug.h"
#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

static int debuglev = 0;
int debugger_show_elapsed_time = 0;
int debugger_show_relative_time = 0;
int debugger_show_file_and_line = 1;

static uint64_t ns_time_at_startup = 0;
static uint64_t ns_time_at_last_debug_message;

// always lock use this when accessing the ns_time_at_last_debug_message
static pthread_mutex_t debug_timing_lock = PTHREAD_MUTEX_INITIALIZER;

uint64_t debug_get_absolute_time_in_ns() {
  uint64_t time_now_ns;
  struct timespec tn;
  // CLOCK_REALTIME because PTP uses it.
  clock_gettime(CLOCK_REALTIME, &tn);
  uint64_t tnnsec = tn.tv_sec;
  tnnsec = tnnsec * 1000000000;
  uint64_t tnjnsec = tn.tv_nsec;
  time_now_ns = tnnsec + tnjnsec;
  return time_now_ns;
}

void debug_init(int level, int show_elapsed_time, int show_relative_time, int show_file_and_line) {
  ns_time_at_startup = debug_get_absolute_time_in_ns();
  ns_time_at_last_debug_message = ns_time_at_startup;
  debuglev = level;
  debugger_show_elapsed_time = show_elapsed_time;
  debugger_show_relative_time = show_relative_time;
  debugger_show_file_and_line = show_file_and_line;
}

int debug_level() {
  return debuglev;
};

void set_debug_level(int level) {
  debuglev = level;
}

void increase_debug_level() {
  if (debuglev < 3)
    debuglev++;
}

void decrease_debug_level() {
  if (debuglev > 0)
    debuglev--;
}

int get_show_elapsed_time() {
  return debugger_show_elapsed_time;
  
}
void set_show_elapsed_time(int setting) {
  debugger_show_elapsed_time = setting;
}
int get_show_relative_timel() {
  return debugger_show_relative_time;
}

void set_show_relative_time(int setting) {
  debugger_show_relative_time = setting;
}

int get_show_file_and_line() {
  return debugger_show_file_and_line;
}

void set_show_file_and_line(int setting) {
  debugger_show_file_and_line = setting;
}

char *generate_preliminary_string(char *buffer, size_t buffer_length, double tss, double tsl,
                                  const char *filename, const int linenumber, const char *prefix) {
  size_t space_remaining = buffer_length;
  char *insertion_point = buffer;
  if (debugger_show_elapsed_time) {
    snprintf(insertion_point, space_remaining, "% 20.9f", tss);
    insertion_point = insertion_point + strlen(insertion_point);
    space_remaining = space_remaining - strlen(insertion_point);
  }
  if (debugger_show_relative_time) {
    snprintf(insertion_point, space_remaining, "% 20.9f", tsl);
    insertion_point = insertion_point + strlen(insertion_point);
    space_remaining = space_remaining - strlen(insertion_point);
  }
  if (debugger_show_file_and_line) {
    snprintf(insertion_point, space_remaining, " \"%s:%d\"", filename, linenumber);
    insertion_point = insertion_point + strlen(insertion_point);
    space_remaining = space_remaining - strlen(insertion_point);
  }
  if (prefix) {
    snprintf(insertion_point, space_remaining, "%s", prefix);
    insertion_point = insertion_point + strlen(insertion_point);
    space_remaining = space_remaining - strlen(insertion_point);
  }
  return insertion_point;
}

void _die(const char *filename, const int linenumber, const char *format, ...) {
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  char b[1024];
  b[0] = 0;
  char *s;
  if (debuglev) {
    pthread_mutex_lock(&debug_timing_lock);
    uint64_t time_now = debug_get_absolute_time_in_ns();
    uint64_t time_since_start = time_now - ns_time_at_startup;
    uint64_t time_since_last_debug_message = time_now - ns_time_at_last_debug_message;
    ns_time_at_last_debug_message = time_now;
    pthread_mutex_unlock(&debug_timing_lock);
    s = generate_preliminary_string(b, sizeof(b), 1.0 * time_since_start / 1000000000,
                                    1.0 * time_since_last_debug_message / 1000000000, filename,
                                    linenumber, " *fatal error: ");
  } else {
    strncpy(b, "fatal error: ", sizeof(b));
    s = b + strlen(b);
  }
  va_list args;
  va_start(args, format);
  vsnprintf(s, sizeof(b) - (s - b), format, args);
  va_end(args);
  // syslog(LOG_ERR, "%s", b);
  fprintf(stderr, "%s\n", b);
  pthread_setcancelstate(oldState, NULL);
  exit(EXIT_FAILURE);
}

void _warn(const char *filename, const int linenumber, const char *format, ...) {
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  char b[1024];
  b[0] = 0;
  char *s;
  if (debuglev) {
    pthread_mutex_lock(&debug_timing_lock);
    uint64_t time_now = debug_get_absolute_time_in_ns();
    uint64_t time_since_start = time_now - ns_time_at_startup;
    uint64_t time_since_last_debug_message = time_now - ns_time_at_last_debug_message;
    ns_time_at_last_debug_message = time_now;
    pthread_mutex_unlock(&debug_timing_lock);
    s = generate_preliminary_string(b, sizeof(b), 1.0 * time_since_start / 1000000000,
                                    1.0 * time_since_last_debug_message / 1000000000, filename,
                                    linenumber, " *warning: ");
  } else {
    strncpy(b, "warning: ", sizeof(b));
    s = b + strlen(b);
  }
  va_list args;
  va_start(args, format);
  vsnprintf(s, sizeof(b) - (s - b), format, args);
  va_end(args);
  // syslog(LOG_WARNING, "%s", b);
  fprintf(stderr, "%s\n", b);
  pthread_setcancelstate(oldState, NULL);
}

void _debug(const char *filename, const int linenumber, int level, const char *format, ...) {
  if (level > debuglev)
    return;
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  char b[1024];
  b[0] = 0;
  pthread_mutex_lock(&debug_timing_lock);
  uint64_t time_now = debug_get_absolute_time_in_ns();
  uint64_t time_since_start = time_now - ns_time_at_startup;
  uint64_t time_since_last_debug_message = time_now - ns_time_at_last_debug_message;
  ns_time_at_last_debug_message = time_now;
  pthread_mutex_unlock(&debug_timing_lock);
  char *s = generate_preliminary_string(b, sizeof(b), 1.0 * time_since_start / 1000000000,
                                        1.0 * time_since_last_debug_message / 1000000000, filename,
                                        linenumber, " ");
  va_list args;
  va_start(args, format);
  vsnprintf(s, sizeof(b) - (s - b), format, args);
  va_end(args);
  // syslog(LOG_DEBUG, "%s", b);
  fprintf(stderr, "%s\n", b);
  pthread_setcancelstate(oldState, NULL);
}

void _inform(const char *filename, const int linenumber, const char *format, ...) {
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  char b[1024];
  b[0] = 0;
  char *s;
  if (debuglev) {
    pthread_mutex_lock(&debug_timing_lock);
    uint64_t time_now = debug_get_absolute_time_in_ns();
    uint64_t time_since_start = time_now - ns_time_at_startup;
    uint64_t time_since_last_debug_message = time_now - ns_time_at_last_debug_message;
    ns_time_at_last_debug_message = time_now;
    pthread_mutex_unlock(&debug_timing_lock);
    s = generate_preliminary_string(b, sizeof(b), 1.0 * time_since_start / 1000000000,
                                    1.0 * time_since_last_debug_message / 1000000000, filename,
                                    linenumber, " ");
  } else {
    s = b;
  }
  va_list args;
  va_start(args, format);
  vsnprintf(s, sizeof(b) - (s - b), format, args);
  va_end(args);
  // syslog(LOG_INFO, "%s", b);
  fprintf(stderr, "%s\n", b);
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