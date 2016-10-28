/*
 * rokid audio bridge
 */

#if defined(__android__)

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "common.h"
#include "audio.h"

#define LOG_TAG "shairport_rktube"
#include "cutils/log.h"

static int fd = -1;

char *pipename = NULL;

static void start(int sample_rate) {
  // this will leave fd as -1 if a reader hasn't been attached
  fd = open(pipename, O_WRONLY | O_NONBLOCK);
  ALOGI("rktube audio output started at Fs=%d Hz\n", sample_rate);
}

static void play(short buf[], int samples) {
  // if the file is not open, try to open it.
  if (fd == -1) {
     fd = open(pipename, O_WRONLY | O_NONBLOCK); 
  }
  // if it's got a reader, write to it.
    int ignore;
  if (fd != -1) {
    //int ignore = non_blocking_write(fd, buf, samples * 4);
    ignore = non_blocking_write(fd, buf, samples * 4);
    if (ignore != samples)
    	ALOGI("rktube audio play buffer length =%d, ignore=%d\n", samples,ignore);
  } 
  ALOGI("rktube audio play buffer length =%d, ignore=%d\n", samples,ignore);
}

static void stop(void) { 
ALOGI("rktube audio stopped\n"); 
}

static int init(int argc, char **argv) {
  debug(1, "rktube init");
  const char *str;
  int value;
  
  config.audio_backend_buffer_desired_length = 44100; // one second.
  config.audio_backend_latency_offset = 0;

  if (config.cfg != NULL) {
    /* Get the Output Pipename. */
    const char *str;
    if (config_lookup_string(config.cfg, "rktube.name", &str)) {
      pipename = (char *)str;
    }
 
    if ((pipename) && (strcasecmp(pipename, "STDOUT") == 0))
      die("Can't use \"pipe\" backend for STDOUT. Use the \"stdout\" backend instead.");

    /* Get the desired buffer size setting. */
    if (config_lookup_int(config.cfg, "rktube.audio_backend_buffer_desired_length", &value)) {
      if ((value < 0) || (value > 132300))
        die("Invalid rktube pipe audio backend buffer desired length \"%d\". It should be between 0 and 132300, default is 44100",
            value);
      else
        config.audio_backend_buffer_desired_length = value;
    }

    /* Get the latency offset. */
    if (config_lookup_int(config.cfg, "rktube.audio_backend_latency_offset", &value)) {
      if ((value < -66150) || (value > 66150))
        die("Invalid rktube pipe audio backend buffer latency offset \"%d\". It should be between -66150 and +66150, default is 0",
            value);
      else
        config.audio_backend_latency_offset = value;
    }
  }
  
  if ((pipename == NULL) && (argc != 1))
    die("bad or missing argument(s) to rktube pipe");

  if (argc == 1)
    pipename = strdup(argv[0]);
  
  
  // here, create the pipe
  if (mkfifo(pipename, 0644) && errno != EEXIST)
    die("Could not create output pipe \"%s\"", pipename);

  debug(1, "Pipename is \"%s\"", pipename);

  return 0;
}

static void deinit(void) {
   if (fd > 0)
    close(fd);
}

static void help(void) { 
  printf("    rktube module using pipe,so takes 1 argument: the name of the FIFO to write to.\n");
}

audio_output audio_rktube = {.name = "rktube",
                            .help = &help,
                            .init = &init,
                            .deinit = &deinit,
                            .start = &start,
                            .stop = &stop,
                            .flush = NULL,
                            .delay = NULL,
                            .play = &play,
                            .volume = NULL,
                            .parameters = NULL,
                            .mute = NULL};


#endif // defined(__android)__
