#ifndef _DEFINITIONS_H
#define _DEFINITIONS_H

#include <sys/socket.h>

#include "config.h"


// for generating our own UUIDs
#define SHAIRPORT_SYNC_DEVICE_NAMESPACE "01d7c137-6316-455d-a52f-dfb529f26adf"

#if defined(__APPLE__) && defined(__MACH__)
/* Apple OSX and iOS (Darwin). ------------------------------ */
#include <TargetConditionals.h>
#if TARGET_OS_MAC == 1
/* OSX */
#define COMPILE_FOR_OSX 1
#endif
#endif

#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__CYGWIN__)
#define COMPILE_FOR_LINUX_AND_FREEBSD_AND_CYGWIN_AND_OPENBSD 1
#endif

#if defined(__linux__)
#define COMPILE_FOR_LINUX 1
#endif

#if defined(__FreeBSD__) || defined(__OpenBSD__)
#define COMPILE_FOR_BSD 1
#endif

#if defined(__FreeBSD__)
#define COMPILE_FOR_FREEBSD 1
#endif

#if defined(__OpenBSD__)
#define COMPILE_FOR_OPENBSD 1
#endif

// struct sockaddr_in6 is bigger than struct sockaddr. derp
#ifdef AF_INET6
#define SOCKADDR struct sockaddr_storage
#define SAFAMILY ss_family
#else
#define SOCKADDR struct sockaddr
#define SAFAMILY sa_family
#endif

#endif // _DEFINITIONS_H
