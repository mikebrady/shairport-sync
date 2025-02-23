/*
 * mDNS registration handler. This file is part of Shairport.
 * Copyright (c) Paul Lietar 2013
 * Amendments and updates copyright (c) Mike Brady 2014--2025
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

#include "common.h"
#include "mdns.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int mdns_pid = 0;

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
        die("mdns_external: execve has failed.");
      } else {              // Parent
        close(execpipe[1]); // Close the write end

        int childErrno;
        // Block until child closes the pipe or sends errno.
        if (read(execpipe[0], &childErrno, sizeof(childErrno)) ==
            sizeof(childErrno)) { // We received errno
          errno = childErrno;
        } else {
          response = pid;
        }
      }
    }
  }
  return response;
}

static int mdns_external_avahi_register(char *ap1name, __attribute__((unused)) char *ap2name,
                                        __attribute__((unused)) int port,
                                        __attribute__((unused)) char **txt_records,
                                        __attribute__((unused)) char **secondary_txt_records) {
  char mdns_port[6];
  snprintf(mdns_port, sizeof(mdns_port), "%d", config.port);

  char *argvwithoutmetadata[] = {
      NULL, ap1name, config.regtype, mdns_port, MDNS_RECORD_WITHOUT_METADATA, NULL};
#ifdef CONFIG_METADATA
  char *argvwithmetadata[] = {NULL, ap1name, config.regtype, mdns_port, MDNS_RECORD_WITH_METADATA,
                              NULL};
#endif
  char **argv;

#ifdef CONFIG_METADATA
  if (config.metadata_enabled)
    argv = argvwithmetadata;
  else
#endif
    argv = argvwithoutmetadata;

  argv[0] = "avahi-publish-service";
  int pid = fork_execvp(argv[0], argv);
  if (pid >= 0) {
    mdns_pid = pid;
    return 0;
  } else
    warn("Calling %s failed !", argv[0]);

  argv[0] = "mDNSPublish";
  pid = fork_execvp(argv[0], argv);
  if (pid >= 0) {
    mdns_pid = pid;
    return 0;
  } else
    warn("Calling %s failed !", argv[0]);

  // If we reach here, both execvp calls failed.
  return -1;
}

static int mdns_external_dns_sd_register(char *ap1name, __attribute__((unused)) char *ap2name,
                                         __attribute__((unused)) int port,
                                         __attribute__((unused)) char **txt_records,
                                         __attribute__((unused)) char **secondary_txt_records) {
  char mdns_port[6];
  snprintf(mdns_port, sizeof(mdns_port), "%d", config.port);

  char *argvwithoutmetadata[] = {
      NULL, ap1name, config.regtype, mdns_port, MDNS_RECORD_WITHOUT_METADATA, NULL};

#ifdef CONFIG_METADATA
  char *argvwithmetadata[] = {NULL, ap1name, config.regtype, mdns_port, MDNS_RECORD_WITH_METADATA,
                              NULL};
#endif

  char **argv;
#ifdef CONFIG_METADATA
  if (config.metadata_enabled)
    argv = argvwithmetadata;
  else
#endif

    argv = argvwithoutmetadata;

  int pid = fork_execvp(argv[0], argv);
  if (pid >= 0) {
    mdns_pid = pid;
    return 0;
  } else
    warn("Calling %s failed !", argv[0]);

  return -1;
}

static void kill_mdns_child(void) {
  if (mdns_pid)
    kill(mdns_pid, SIGTERM);
  mdns_pid = 0;
}

mdns_backend mdns_external_avahi = {.name = "external-avahi",
                                    .mdns_register = mdns_external_avahi_register,
                                    .mdns_unregister = kill_mdns_child,
                                    .mdns_dacp_monitor_start = NULL,
                                    .mdns_dacp_monitor_set_id = NULL,
                                    .mdns_dacp_monitor_stop = NULL};

mdns_backend mdns_external_dns_sd = {.name = "external-dns-sd",
                                     .mdns_register = mdns_external_dns_sd_register,
                                     .mdns_unregister = kill_mdns_child,
                                     .mdns_dacp_monitor_start = NULL,
                                     .mdns_dacp_monitor_set_id = NULL,
                                     .mdns_dacp_monitor_stop = NULL};
