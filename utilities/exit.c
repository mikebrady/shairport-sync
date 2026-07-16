/*
MIT License

Copyright (c) 2023--2026 Mike Brady 4265913+mikebrady@users.noreply.github.com

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/


// exit_init() starts a thread what waits for the exit_request_flag to
// be set to non-zero.
// It then runs the standard exit(exit_status) to
// terminate the program, running all the atexit handlers first.

// exit_request() can be called from anywhere in the program 
// including a SIG handler and 
// the thread will take care of terminating the program cleanly.
// pass in EXIT_SUCCESS or EXIT_FAILURE in the request.

#include <signal.h> // for sig_atomic_t
#include <stdlib.h> // for EXIT_SUCCESS
#include <string.h> // for memset
#include <unistd.h> // for usleep
#include <pthread.h>

#include "exit.h"
#include "common.h"

volatile sig_atomic_t exit_request_flag = 0;
volatile sig_atomic_t exit_status = EXIT_SUCCESS;

pthread_t exit_manager_thread;

void *exit_manager(__attribute__((unused)) void *arg) {
  while(exit_request_flag == 0) {
    usleep(100000);
  }
  exit(exit_status);
  return NULL;
}

void exit_init() {
  memset(&exit_manager_thread, 0, sizeof(pthread_t));
  named_pthread_create(&exit_manager_thread, NULL, &exit_manager, NULL, "exit_manager");
}

void exit_request(const int exit_status_requested) {
  exit_status = exit_status_requested; // EXIT_SUCCESS or EXIT_FAILURE
  exit_request_flag = 1; // ask for exit
}