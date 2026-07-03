#ifndef SHAIRPORT_COMPAT_SYS_SOCKET_H
#define SHAIRPORT_COMPAT_SYS_SOCKET_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

typedef int socklen_t;

#ifndef SHUT_RDWR
#define SHUT_RDWR SD_BOTH
#endif

#define recv(s, buf, len, flags) recv((s), (char *)(buf), (int)(len), (flags))
#define recvfrom(s, buf, len, flags, from, fromlen)                                               \
  recvfrom((s), (char *)(buf), (int)(len), (flags), (from), (fromlen))
#define send(s, buf, len, flags) send((s), (const char *)(buf), (int)(len), (flags))
#define sendto(s, buf, len, flags, to, tolen)                                                     \
  sendto((s), (const char *)(buf), (int)(len), (flags), (to), (int)(tolen))
#define setsockopt(s, level, optname, optval, optlen)                                             \
  setsockopt((s), (level), (optname), (const char *)(optval), (int)(optlen))

#endif
