#ifndef SHAIRPORT_COMPAT_NET_IF_H
#define SHAIRPORT_COMPAT_NET_IF_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#ifndef IFF_UP
#define IFF_UP 0x1
#endif
#ifndef IFF_RUNNING
#define IFF_RUNNING 0x40
#endif
#ifndef IFF_LOOPBACK
#define IFF_LOOPBACK 0x8
#endif

#endif
