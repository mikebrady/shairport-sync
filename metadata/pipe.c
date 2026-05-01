  #include "pipe.h"
  #include "core.h"
  #include "pc_queue.h"
  
  #include <stdlib.h>
  #include <sys/stat.h>
  #include <string.h>
  #include <errno.h>

static int fd = -1; // the pipe file descriptor
  
pc_queue metadata_queue;
#define metadata_queue_size 500
metadata_package metadata_queue_items[metadata_queue_size];
pthread_t metadata_thread;

void metadata_open(void) {
  if (config.metadata_enabled == 0)
    return;

  size_t pl = strlen(config.metadata_pipename) + 1;

  char *path = malloc(pl + 1);
  snprintf(path, pl + 1, "%s", config.metadata_pipename);

  fd = try_to_open_pipe_for_writing(path);
  free(path);
}

static void metadata_close(void) {
  if (fd < 0)
    return;
  close(fd);
  fd = -1;
}

void metadata_process(uint32_t type, uint32_t code, char *data, uint32_t length) {
  // debug(1, "Process metadata with type %x, code %x and length %u.", type, code, length);
  int ret = 0;
  // readers may go away and come back

  if (fd < 0)
    metadata_open();
  if (fd < 0)
    return;
  char thestring[1024];
  snprintf(thestring, 1024, "<item><type>%x</type><code>%x</code><length>%u</length>", type, code,
           length);
  // ret = non_blocking_write(fd, thestring, strlen(thestring));
  ret = write(fd, thestring, strlen(thestring));
  if (ret < 0) {
    // debug(1,"metadata_process error %d exit 1",ret);
    return;
  }
  if ((data != NULL) && (length > 0)) {
    snprintf(thestring, 1024, "\n<data encoding=\"base64\">\n");
    // ret = non_blocking_write(fd, thestring, strlen(thestring));
    ret = write(fd, thestring, strlen(thestring));
    if (ret < 0) {
      // debug(1,"metadata_process error %d exit 2",ret);
      return;
    }
    // here, we write the data in base64 form using our nice base64 encoder
    // but, we break it into lines of 76 output characters, except for the last
    // one.
    // thus, we send groups of (76/4)*3 =  57 bytes to the encoder at a time
    size_t remaining_count = length;
    char *remaining_data = data;
    // size_t towrite_count;
    char outbuf[76];
    while ((remaining_count) && (ret >= 0)) {
      size_t towrite_count = remaining_count;
      if (towrite_count > 57)
        towrite_count = 57;
      size_t outbuf_size = 76; // size of output buffer on entry, length of result on exit
      if (base64_encode_so((unsigned char *)remaining_data, towrite_count, outbuf, &outbuf_size) ==
          NULL)
        debug(1, "Error encoding base64 data.");
      // debug(1,"Remaining count: %d ret: %d, outbuf_size:
      // %d.",remaining_count,ret,outbuf_size);
      // ret = non_blocking_write(fd, outbuf, outbuf_size);
      ret = write(fd, outbuf, outbuf_size);
      if (ret < 0) {
        // debug(1,"metadata_process error %d exit 3",ret);
        return;
      }
      remaining_data += towrite_count;
      remaining_count -= towrite_count;
    }
    snprintf(thestring, 1024, "</data>");
    // ret = non_blocking_write(fd, thestring, strlen(thestring));
    ret = write(fd, thestring, strlen(thestring));
    if (ret < 0) {
      // debug(1,"metadata_process error %d exit 4",ret);
      return;
    }
  }
  snprintf(thestring, 1024, "</item>\n");
  // ret = non_blocking_write(fd, thestring, strlen(thestring));
  ret = write(fd, thestring, strlen(thestring));
  if (ret < 0) {
    // debug(1,"metadata_process error %d exit 5",ret);
    return;
  }
}

void metadata_thread_cleanup_function(__attribute__((unused)) void *arg) {
  // debug(2, "metadata_thread_cleanup_function called");
  metadata_close();
}

void *metadata_thread_function(__attribute__((unused)) void *ignore) {
  //  #include <syscall.h>
  //  debug(1, "metadata_thread_function PID %d", syscall(SYS_gettid));
  metadata_package pack;
  pthread_cleanup_push(metadata_thread_cleanup_function, NULL);
  while (1) {
    pc_queue_get_item(&metadata_queue, &pack);
    pthread_cleanup_push(metadata_pack_cleanup_function, (void *)&pack);
    if (config.metadata_enabled) {
      if (pack.carrier) {
        debug(4, "     pipe: type %x, code %x, length %u, message %d.", pack.type, pack.code,
              pack.length, pack.carrier->index_number);
      } else {
        debug(4, "     pipe: type %x, code %x, length %u.", pack.type, pack.code, pack.length);
      }
      metadata_process(pack.type, pack.code, pack.data, pack.length);
      debug(4, "     pipe: done.");
    }
    pthread_cleanup_pop(1);
  }
  pthread_cleanup_pop(1); // will never happen
  pthread_exit(NULL);
}

void metadata_pipe_queue_init() {
  // create the metadata pipe, if necessary
  size_t pl = strlen(config.metadata_pipename) + 1;
  char *path = malloc(pl + 1);
  snprintf(path, pl + 1, "%s", config.metadata_pipename);
  mode_t oldumask = umask(000);
  if (mkfifo(path, 0666) && errno != EEXIST)
    die("Could not create metadata pipe \"%s\".", path);
  umask(oldumask);
  debug(1, "metadata pipe name is \"%s\".", path);

  // try to open it
  fd = try_to_open_pipe_for_writing(path);
  // we check that it's not a "real" error. From the "man 2 open" page:
  // "ENXIO  O_NONBLOCK | O_WRONLY is set, the named file is a FIFO, and no process has the FIFO
  // open for reading." Which is okay.
  if ((fd == -1) && (errno != ENXIO)) {
    char errorstring[1024];
    strerror_r(errno, (char *)errorstring, sizeof(errorstring));
    debug(1, "metadata_init -- error %d (\"%s\") opening pipe: \"%s\".", errno,
          (char *)errorstring, path);
    warn("can not open metadata pipe -- error %d (\"%s\") opening pipe: \"%s\".", errno,
         (char *)errorstring, path);
  }
  free(path);

  // initialise the metadata queues first, otherwise the might be a race condition
  // create a pc_queue for the metadata pipe
  pc_queue_init(&metadata_queue, (char *)&metadata_queue_items, sizeof(metadata_package),
                metadata_queue_size, "pipe");

  if (named_pthread_create(&metadata_thread, NULL, metadata_thread_function, NULL,
                           "metadata pipe") != 0)
    debug(1, "Failed to create metadata thread!");
}

void metadata_pipe_queue_stop() {
  if (metadata_thread) {
    // debug(2, "metadata stop metadata_thread thread.");
    pthread_cancel(metadata_thread);
    pthread_join(metadata_thread, NULL);
    pc_queue_delete(&metadata_queue);
    // debug(2, "metadata_stop finished successfully.");
  }
}

int send_metadata_to_pipe_queue(const uint32_t type, const uint32_t code,
                           const char *data, const uint32_t length, rtsp_message *carrier,
                           int block) {
    return send_metadata_to_queue(&metadata_queue, type, code, data, length, carrier, block);
}
