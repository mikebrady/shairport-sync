#ifndef SHAIRPORT_MINGW_COMPAT_H
#define SHAIRPORT_MINGW_COMPAT_H

#include <fcntl.h>
#include <errno.h>
#include <direct.h>
#include <io.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

unsigned long if_nametoindex(const char *ifname);
int shairport_mingw_get_device_id(uint8_t *id, int int_length);

#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif
#ifndef F_GETFL
#define F_GETFL 3
#endif
#ifndef F_SETFL
#define F_SETFL 4
#endif
#ifndef F_GETFD
#define F_GETFD 1
#endif
#ifndef F_SETFD
#define F_SETFD 2
#endif
#ifndef FD_CLOEXEC
#define FD_CLOEXEC 1
#endif

static inline char *shairport_mingw_realpath(const char *path, char *resolved_path) {
  return _fullpath(resolved_path, path, 0);
}

static inline int shairport_mingw_pipe(int handles[2]) {
  return _pipe(handles, 4096, _O_BINARY);
}

static inline int shairport_mingw_dprintf(int fd, const char *format, ...) {
  char buffer[16384];
  va_list args;
  va_start(args, format);
  int needed = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  if (needed < 0)
    return needed;
  int count = needed < (int)sizeof(buffer) ? needed : (int)sizeof(buffer) - 1;
  return _write(fd, buffer, (unsigned int)count);
}

static inline int shairport_mingw_strerror_r(int errnum, char *buf, size_t buflen) {
  return strerror_s(buf, buflen, errnum);
}

static inline int shairport_mingw_fcntl(int fd, int cmd, ...) {
  (void)fd;
  if (cmd == F_GETFL)
    return 0;
  return 0;
}

static inline int shairport_mingw_winsock_init(void) {
  WSADATA wsa;
  return WSAStartup(MAKEWORD(2, 2), &wsa);
}

static inline void shairport_mingw_winsock_cleanup(void) { WSACleanup(); }

static inline char *strsep(char **stringp, const char *delim) {
  if ((stringp == NULL) || (*stringp == NULL))
    return NULL;

  char *start = *stringp;
  char *cursor = start;
  while (*cursor) {
    if (strchr(delim, *cursor)) {
      *cursor = '\0';
      *stringp = cursor + 1;
      return start;
    }
    cursor++;
  }
  *stringp = NULL;
  return start;
}

#define realpath(path, resolved_path) shairport_mingw_realpath((path), (resolved_path))
#define pipe(handles) shairport_mingw_pipe((handles))
#define dprintf(fd, format, ...) shairport_mingw_dprintf((fd), (format), __VA_ARGS__)
#define strerror_r(errnum, buf, buflen) shairport_mingw_strerror_r((errnum), (buf), (buflen))
#define mkdir(path, mode) _mkdir((path))
#define fcntl(fd, cmd, ...) shairport_mingw_fcntl((fd), (cmd), ##__VA_ARGS__)
#define drand48() ((double)rand() / ((double)RAND_MAX + 1.0))
#define srand48(seed) srand((unsigned int)(seed))
#define initstate(seed, state, size) (srand((unsigned int)(seed)), (char *)(state))

#endif
