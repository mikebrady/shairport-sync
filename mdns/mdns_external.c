/*
 * mDNS registration handler. This file is part of Shairport.
 * Copyright (c) Paul Lietar 2013
 * Amendments and updates copyright (c) Mike Brady 2014--2026
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Registry-driven "external" backends: each record in the mDNS registry is
 * advertised by its own child process, running
 *
 *   external-avahi:   avahi-publish-service <name> <type> <port> <key=value>...
 *                     (or, if that is not installed, mDNSPublish with the same arguments)
 *   external-dns-sd:  dns-sd -R <name> <type> local <port> <key=value>...
 *
 * A child process can not have its text changed, so when a record changes the
 * child is stopped and a new one started; the service goes away and comes back.
 * When a record is removed its child is stopped.
 *
 * The registry serialises its publisher callbacks, and mdns_start / mdns_stop are
 * called when no callback can be running, so the table needs no lock of its own.
 */

#include "common.h"
#include "mdns.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef enum { STYLE_AVAHI, STYLE_DNS_SD } style_t;

typedef struct {
  int used;
  char type[32];
  pid_t pid;
} child_t;

static child_t children[MDNS_MAX_RECORDS];
static const char *program = NULL; // the command found by mdns_start
static style_t style = STYLE_AVAHI;

/*
 * Do a fork followed by a execvp, handling execvp errors correctly.
 * Return the pid of the new process upon success, or -1 if something failed.
 * Check errno for error details.
 */
static int fork_execvp(const char *file, char *const argv[]) {
  int execpipe[2];
  int response = -1;
  if (pipe(execpipe) >= 0) {

    if (fcntl(execpipe[1], F_SETFD, fcntl(execpipe[1], F_GETFD) | FD_CLOEXEC) < 0) {
      close(execpipe[0]);
      close(execpipe[1]);
    } else {

      int pid = fork();
      if (pid < 0) {
        close(execpipe[0]);
        close(execpipe[1]);
      } else if (pid == 0) { // Child
        close(execpipe[0]);  // Close the read end
        execvp(file, argv);
        // If we reach this point then execve has failed.
        // Write erno's value into the pipe and exit.
        if (write(execpipe[1], &errno, sizeof(errno)) != sizeof(errno))
          debug(1,
                "Execve has failed and there was a further error writing an error message, duh.");
        _exit(127); // not die(): this is a forked copy of a multi-threaded program
      } else {              // Parent
        close(execpipe[1]); // Close the write end

        int childErrno;
        // Block until child closes the pipe or sends errno.
        if (read(execpipe[0], &childErrno, sizeof(childErrno)) ==
            sizeof(childErrno)) { // We received errno
          errno = childErrno;
          waitpid(pid, NULL, 0); // reap the child that failed to exec
        } else {
          response = pid;
        }
        close(execpipe[0]);
      }
    }
  }
  return response;
}

// True if the command can be run: a path that is executable, or a name found in $PATH.
static int program_available(const char *name) {
  if (strchr(name, '/') != NULL)
    return access(name, X_OK) == 0;
  const char *path = getenv("PATH");
  if (path == NULL)
    path = "/usr/local/bin:/usr/bin:/bin";
  char *copy = strdup(path);
  if (copy == NULL)
    return 0;
  int found = 0;
  char *save = NULL;
  for (char *dir = strtok_r(copy, ":", &save); dir != NULL && !found;
       dir = strtok_r(NULL, ":", &save)) {
    char full[1024];
    snprintf(full, sizeof full, "%s/%s", dir, name);
    found = (access(full, X_OK) == 0);
  }
  free(copy);
  return found;
}

static child_t *find_child(const char *type) {
  for (int i = 0; i < MDNS_MAX_RECORDS; i++)
    if (children[i].used && (strcasecmp(children[i].type, type) == 0))
      return &children[i];
  return NULL;
}

// Stops the child and waits for it, so no zombie is left.
static void stop_child(child_t *c) {
  if (c->pid > 0) {
    kill(c->pid, SIGTERM);
    int done = 0;
    for (int i = 0; i < 100 && !done; i++) { // up to two seconds
      pid_t r = waitpid(c->pid, NULL, WNOHANG);
      if ((r == c->pid) || (r < 0)) // reaped, or someone else has (e.g. SIGCHLD is ignored)
        done = 1;
      else
        usleep(20000);
    }
    if (!done) {
      debug(1, "mdns_external: the %s process ignored SIGTERM.", c->type);
      kill(c->pid, SIGKILL);
      waitpid(c->pid, NULL, 0);
    }
  }
  memset(c, 0, sizeof(child_t));
}

// Starts the child process for a record. Returns its pid, or -1.
static int spawn(const mdns_service_record *rec) {
  char port[8];
  snprintf(port, sizeof port, "%u", (unsigned)rec->port);

  const char *argv[MDNS_MAX_KV + 8];
  int n = 0;
  argv[n++] = program;
  if (style == STYLE_DNS_SD)
    argv[n++] = "-R";
  argv[n++] = rec->instance_name;
  argv[n++] = rec->service_type;
  if (style == STYLE_DNS_SD)
    argv[n++] = "local";
  argv[n++] = port;
  for (size_t i = 0; i < rec->n; i++)
    argv[n++] = mdns_record_get(rec, i);
  argv[n] = NULL;
  return fork_execvp(program, (char *const *)argv);
}

static void external_publish(const mdns_service_record *rec, mdns_put_result why, void *ctx) {
  (void)why; // added or replaced: either way, the child has to describe the record as it is now
  (void)ctx;
  child_t *c = find_child(rec->service_type);
  if (c != NULL)
    stop_child(c); // a child can not be changed: replace it
  else {
    for (int i = 0; i < MDNS_MAX_RECORDS; i++)
      if (!children[i].used) {
        c = &children[i];
        break;
      }
    if (c == NULL) {
      warn("mdns_external: no free slot for a record of type %s.", rec->service_type);
      return;
    }
  }

  int pid = spawn(rec);
  if (pid < 0) {
    warn("Calling %s for \"%s\" (%s) failed!", program, rec->instance_name, rec->service_type);
    memset(c, 0, sizeof(child_t));
    return;
  }
  c->used = 1;
  c->pid = pid;
  snprintf(c->type, sizeof c->type, "%s", rec->service_type);
}

static void external_unpublish(const mdns_service_record *rec, void *ctx) {
  (void)ctx;
  child_t *c = find_child(rec->service_type);
  if (c != NULL)
    stop_child(c);
}

static int start_with(style_t s, const char *const candidates[]) {
  memset(children, 0, sizeof(children));
  for (int i = 0; candidates[i] != NULL; i++)
    if (program_available(candidates[i])) {
      program = candidates[i];
      style = s;
      return 0;
    }
  warn("mdns_external: none of the commands needed is installed (%s%s).", candidates[0],
       candidates[1] != NULL ? ", ..." : "");
  return -1;
}

static int external_avahi_start(void) {
  static const char *const candidates[] = {"avahi-publish-service", "mDNSPublish", NULL};
  return start_with(STYLE_AVAHI, candidates);
}

static int external_dns_sd_start(void) {
  static const char *const candidates[] = {"dns-sd", NULL};
  return start_with(STYLE_DNS_SD, candidates);
}

// Called after every record has been unpublished and the publisher removed.
static void external_stop(void) {
  for (int i = 0; i < MDNS_MAX_RECORDS; i++)
    if (children[i].used)
      stop_child(&children[i]);
}

mdns_backend mdns_external_avahi = {.name = "external-avahi",
                                    .mdns_start = external_avahi_start,
                                    .mdns_stop = external_stop,
                                    .mdns_publish = external_publish,
                                    .mdns_unpublish = external_unpublish,
                                    .mdns_dacp_monitor_start = NULL,
                                    .mdns_dacp_monitor_set_id = NULL,
                                    .mdns_dacp_monitor_stop = NULL};

mdns_backend mdns_external_dns_sd = {.name = "external-dns-sd",
                                     .mdns_start = external_dns_sd_start,
                                     .mdns_stop = external_stop,
                                     .mdns_publish = external_publish,
                                     .mdns_unpublish = external_unpublish,
                                     .mdns_dacp_monitor_start = NULL,
                                     .mdns_dacp_monitor_set_id = NULL,
                                     .mdns_dacp_monitor_stop = NULL};