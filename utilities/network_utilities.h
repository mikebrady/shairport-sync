#ifndef _NETWORK_UTILITIES_H
#define _NETWORK_UTILITIES_H

#include <sys/socket.h>

#define restrict

int eintr_checked_accept(int sockfd, struct sockaddr *addr,
                  socklen_t *addrlen);
#endif // _NETWORK_UTILITIES_H
