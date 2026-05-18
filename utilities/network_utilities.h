#pragma once

#include <sys/socket.h>

#define restrict
int eintr_checked_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

int _safe_socket_close(const char *filename, const int linenumber, int *sockfd);

#define safe_socket_close(sockfd) _safe_socket_close(__FILE__, __LINE__, sockfd)
