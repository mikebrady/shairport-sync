#ifndef SHAIRPORT_COMPAT_NETINET_TCP_H
#define SHAIRPORT_COMPAT_NETINET_TCP_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#ifndef SOL_TCP
#define SOL_TCP IPPROTO_TCP
#endif

#endif
