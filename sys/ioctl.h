#ifndef SHAIRPORT_COMPAT_SYS_IOCTL_H
#define SHAIRPORT_COMPAT_SYS_IOCTL_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>

#define ioctl(s, cmd, argp) ioctlsocket((s), (cmd), (u_long *)(argp))

#endif
