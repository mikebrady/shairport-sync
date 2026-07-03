#ifndef SHAIRPORT_COMPAT_POLL_H
#define SHAIRPORT_COMPAT_POLL_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>

typedef WSAPOLLFD pollfd;

#define poll(fds, nfds, timeout) WSAPoll((LPWSAPOLLFD)(fds), (ULONG)(nfds), (timeout))

#endif
