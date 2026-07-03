#ifndef SHAIRPORT_COMPAT_SYSLOG_H
#define SHAIRPORT_COMPAT_SYSLOG_H

#include <stdarg.h>
#include <stdio.h>

#define LOG_EMERG 0
#define LOG_ALERT 1
#define LOG_CRIT 2
#define LOG_ERR 3
#define LOG_WARNING 4
#define LOG_NOTICE 5
#define LOG_INFO 6
#define LOG_DEBUG 7
#define LOG_DAEMON 0

#define LOG_PID 0
#define LOG_CONS 0
#define LOG_NDELAY 0
#define LOG_NOWAIT 0
#define LOG_USER 0

#define LOG_UPTO(priority) ((1 << ((priority) + 1)) - 1)

static inline int setlogmask(int maskpri) {
  return maskpri;
}

static inline void openlog(const char *ident, int option, int facility) {
  (void)ident;
  (void)option;
  (void)facility;
}

static inline void closelog(void) {
}

static inline void syslog(int priority, const char *format, ...) {
  (void)priority;
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
}

#endif
