#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "rtsp_message_utilities.h"
#include "../rtsp.h"
#include "../common.h"

#ifdef CONFIG_AIRPLAY_2
#include <plist/plist.h>
#ifdef HAVE_LIBPLIST_GE_2_3_0
#define plist_from_memory(plist_data, length, plist)                                               \
  plist_from_memory((plist_data), (length), (plist), NULL)
#endif
#endif

// every time we want to retain or release a reference count, lock it with this
// if a reference count is read as zero, it means the it's being deallocated.
static pthread_mutex_t reference_counter_lock = PTHREAD_MUTEX_INITIALIZER;
static int msg_indexes = 1;

void msg_retain(rtsp_message *msg) {
  int rc = debug_mutex_lock(&reference_counter_lock, 500000, 4);
  if (rc)
    debug(1, "Error %d locking reference counter lock", rc);
  if (msg > (rtsp_message *)0x00010000) {
    msg->referenceCount++;
    debug(4, "msg_free increment reference counter message %d to %d.", msg->index_number,
          msg->referenceCount);
    // debug(1,"msg_retain -- item %d reference count %d.", msg->index_number, msg->referenceCount);
    rc = pthread_mutex_unlock(&reference_counter_lock);
    if (rc)
      debug(1, "Error %d unlocking reference counter lock", rc);
  } else {
    debug(1, "invalid rtsp_message pointer 0x%" PRIxPTR " passed to retain", (uintptr_t)msg);
  }
}

rtsp_message *msg_init(void) {
  // no thread cancellation points here
  int rc = debug_mutex_lock(&reference_counter_lock, 500000, 4);
  if (rc)
    debug(1, "Error %d locking reference counter lock", rc);

  rtsp_message *msg = malloc(sizeof(rtsp_message));
  if (msg) {
    memset(msg, 0, sizeof(rtsp_message));
    msg->referenceCount = 1; // from now on, any access to this must be protected with the lock
    msg->index_number = msg_indexes++;
    debug(4, "msg_init message %d", msg->index_number);
  } else {
    die("msg_init -- can not allocate memory for rtsp_message %d.", msg_indexes);
  }
  // debug(1,"msg_init -- create item %d.", msg->index_number);

  rc = pthread_mutex_unlock(&reference_counter_lock);
  if (rc)
    debug(1, "Error %d unlocking reference counter lock", rc);

  return msg;
}

int msg_add_header(rtsp_message *msg, char *name, char *value) {
  if (msg->nheaders >= sizeof(msg->name) / sizeof(char *)) {
    warn("too many headers?!");
    return 1;
  }

  msg->name[msg->nheaders] = strdup(name);
  msg->value[msg->nheaders] = strdup(value);
  msg->nheaders++;

  return 0;
}

char *msg_get_header(rtsp_message *msg, char *name) {
  unsigned int i;
  for (i = 0; i < msg->nheaders; i++)
    if (!strcasecmp(msg->name[i], name))
      return msg->value[i];
  return NULL;
}

void _debug_print_msg_headers(rtsp_conn_info *conn, const char *filename, const int linenumber, int level,
                              rtsp_message *msg) {
  unsigned int i;
  if (conn != NULL) {
    if (msg->respcode != 0)
      _debug(filename, linenumber, level, "Connection: %d,  Response Code: %d.", conn->connection_number, msg->respcode);
    for (i = 0; i < msg->nheaders; i++) {
      _debug(filename, linenumber, level, "Connection: %d,  Type: \"%s\", content: \"%s\"", conn->connection_number, msg->name[i],
             msg->value[i]);
    }
  } else {
    if (msg->respcode != 0)
      _debug(filename, linenumber, level, "  Response Code: %d.", msg->respcode);
    for (i = 0; i < msg->nheaders; i++) {
      _debug(filename, linenumber, level, "  Type: \"%s\", content: \"%s\"", msg->name[i],
             msg->value[i]);
    }
  }
}

void msg_free(rtsp_message **msgh) {
  debug_mutex_lock(&reference_counter_lock, 1000, 0);
  if (*msgh > (rtsp_message *)0x00010000) {
    rtsp_message *msg = *msgh;
    msg->referenceCount--;
    if (msg->referenceCount)
      debug(4, "msg_free decrement reference counter message %d to %d", msg->index_number,
            msg->referenceCount);
    if (msg->referenceCount == 0) {
      unsigned int i;
      for (i = 0; i < msg->nheaders; i++) {
        free(msg->name[i]);
        free(msg->value[i]);
      }
      if (msg->content)
        free(msg->content);
      // debug(1,"msg_free item %d -- free.",msg->index_number);
      uintptr_t index = (msg->index_number) & 0xFFFF;
      if (index == 0)
        index = 0x10000; // ensure it doesn't fold to zero.
      *msgh =
          (rtsp_message *)(index); // put a version of the index number of the freed message in here
      debug(4, "msg_free freed message %d", msg->index_number);
      free(msg);
    } else {
      // debug(1,"msg_free item %d -- decrement reference to
      // %d.",msg->index_number,msg->referenceCount);
    }
  } else if (*msgh != NULL) {
    debug(1,
          "msg_free: error attempting to free an allocated but already-freed rtsp_message, number "
          "%" PRIxPTR ".",
          (uintptr_t)*msgh);
  }
  debug_mutex_unlock(&reference_counter_lock, 0);
}

int msg_handle_line(rtsp_message **pmsg, char *line) {
  rtsp_message *msg = *pmsg;

  if (!msg) {
    msg = msg_init();
    *pmsg = msg;
    char *sp, *p;
    sp = NULL; // this is to quieten a compiler warning

    debug(4, "RTSP/HTTP Message Received: \"%s\".", line);

    p = strtok_r(line, " ", &sp);
    if (!p)
      goto fail;
    strncpy(msg->method, p, sizeof(msg->method) - 1);

    p = strtok_r(NULL, " ", &sp);
    if (!p)
      goto fail;
    strncpy(msg->path, p, sizeof(msg->path) - 1);

    p = strtok_r(NULL, " ", &sp);
    if (!p)
      goto fail;
    if ((strcmp(p, "RTSP/1.0") != 0) && (strcmp(p, "HTTP/1.1") != 0)) {
      debug(1, "Problem with Message: \"%s\"", p);
      goto fail;
    }

    return -1;
  }

  if (strlen(line)) {
    char *p;
    p = strstr(line, ": ");
    if (!p) {
      warn("bad header: >>%s<<", line);
      goto fail;
    }
    *p = 0;
    p += 2;
    msg_add_header(msg, line, p);
    debug(4, "    %s: %s.", line, p);
    return -1;
  } else {
    char *cl = msg_get_header(msg, "Content-Length");
    if (cl)
      return atoi(cl);
    else
      return 0;
  }

fail:
  debug(1, "msg_handle_line fail");
  msg_free(pmsg);
  *pmsg = NULL;
  return 0;
}

#ifdef CONFIG_AIRPLAY_2

int rtsp_message_contains_plist(rtsp_message *message) {
  int reply = 0; // assume there is no plist in the message
  if ((message->contentlength >= strlen("bplist00")) &&
      (strstr(message->content, "bplist00") == message->content))
    reply = 1;
  return reply;
}

plist_t plist_from_rtsp_content(rtsp_message *message) {
  plist_t the_plist = NULL;
  if (rtsp_message_contains_plist(message)) {
    plist_from_memory(message->content, message->contentlength, &the_plist);
  }
  return the_plist;
}

char *plist_as_xml_text(plist_t the_plist) {
  // caller must free the returned character buffer
  // convert it to xml format
  uint32_t size;
  char *plist_out = NULL;
  plist_to_xml(the_plist, &plist_out, &size);

  // put it into a NUL-terminated string
  char *reply = malloc(size + 1);
  if (reply) {
    memcpy(reply, plist_out, size);
    reply[size] = '\0';
  }
  if (plist_out)
    free(plist_out);
  return reply;
}

// caller must free the returned character buffer
char *rtsp_plist_content(rtsp_message *message) {
  char *reply = NULL;
  // first, check if it has binary plist content
  if (rtsp_message_contains_plist(message))
    reply = plist_as_xml_text(plist_from_rtsp_content(message));
  return reply;
}

#endif

void _debug_log_rtsp_message(rtsp_conn_info *conn, const char *filename, const int linenumber, int level, char *prompt,
                             rtsp_message *message) {
  if (level > debug_level())
    return;
  if ((prompt) && (*prompt != '\0')) // okay to pass NULL or an empty list...
    _debug(filename, linenumber, level, "%s", prompt);
  _debug_print_msg_headers(conn, filename, linenumber, level, message);
#ifdef CONFIG_AIRPLAY_2
  char *plist_content = rtsp_plist_content(message);
  if (plist_content) {
    _debug(filename, linenumber, level, "  Content length: %u. Content Plist (as XML):\n--\n%s--", message->contentlength, plist_content);
    free(plist_content);
  } else
#endif
  {
    _debug(filename, linenumber, level, "  Content length: %u.",
           message->contentlength);
    if (message->contentlength > 0) {
      _debug_print_buffer(filename, linenumber, level, message->content, message->contentlength);
    }
  }
}
