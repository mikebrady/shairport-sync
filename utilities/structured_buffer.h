#ifndef _STRUCTURED_BUFFER_H
#define _STRUCTURED_BUFFER_H

typedef struct {
  char *buf;
  size_t buf_size;
  size_t buf_pos;
} structured_buffer;

structured_buffer *sbuf_new(size_t size);
int sbuf_clear(structured_buffer *sbuf);
void sbuf_free(structured_buffer *sbuf);
void sbuf_cleanup(void *arg);
int sbuf_printf(structured_buffer *sbuf, const char *format, ...);
int sbuf_append(structured_buffer *sbuf, char *plistString, uint32_t plistStringLength);
int sbuf_buf_and_length(structured_buffer *sbuf, char **b, size_t *l);

#endif // _STRUCTURED_BUFFER_H
