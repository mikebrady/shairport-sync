#ifndef _BUFFERED_READ_H
#define _BUFFERED_READ_H

typedef struct {
  int closed;
  int error_code;
  int sock_fd;
  char *buffer;
  char *toq;
  char *eoq;
  size_t buffer_max_size;
  size_t buffer_occupancy;
  pthread_mutex_t mutex;
  pthread_cond_t not_empty_cv;
  pthread_cond_t not_full_cv;
} buffered_tcp_desc;

void *buffered_tcp_reader(void *arg);

// read the number of bytes specified by "count".
ssize_t read_sized_block(buffered_tcp_desc *descriptor, void *buf, size_t count,
                          size_t *bytes_remaining);
                          
#endif // _BUFFERED_READ_H
