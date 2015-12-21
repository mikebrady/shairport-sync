/*
 * Shairport, an Apple Airplay receiver
 * Copyright (c) James Laird 2013
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

#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <memory.h>
#include <sys/wait.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <popt.h>
#include <libgen.h>
#include <libconfig.h>

#include "config.h"

#ifdef HAVE_LIBPOLARSSL
#include <polarssl/md5.h>
#endif

#ifdef HAVE_LIBSSL
#include <openssl/md5.h>
#endif

#include "common.h"
#include "rtsp.h"
#include "rtp.h"
#include "mdns.h"

#include <libdaemon/dfork.h>
#include <libdaemon/dsignal.h>
#include <libdaemon/dlog.h>
#include <libdaemon/dpid.h>
#include <libdaemon/dexec.h>

static int shutting_down = 0;
static char *appName = NULL;
char configuration_file_path[4096+1];
char actual_configuration_file_path[4096+1];

void shairport_shutdown() {
  if (shutting_down)
    return;
  shutting_down = 1;
  mdns_unregister();
  rtsp_shutdown_stream();
  if (config.output)
    config.output->deinit();
}

static void sig_ignore(int foo, siginfo_t *bar, void *baz) {}
static void sig_shutdown(int foo, siginfo_t *bar, void *baz) {
  debug(1, "shutdown requested...");
  shairport_shutdown();
  daemon_log(LOG_NOTICE, "exit...");
  daemon_retval_send(255);
  daemon_pid_file_remove();
  exit(0);
}

static void sig_child(int foo, siginfo_t *bar, void *baz) {
  pid_t pid;
  while ((pid = waitpid((pid_t)-1, 0, WNOHANG)) > 0) {
    if (pid == mdns_pid && !shutting_down) {
      die("MDNS child process died unexpectedly!");
    }
  }
}

static void sig_disconnect_audio_output(int foo, siginfo_t *bar, void *baz) {
  debug(1, "disconnect audio output requested.");
  set_requested_connection_state_to_output(0);
}

static void sig_connect_audio_output(int foo, siginfo_t *bar, void *baz) {
  debug(1, "connect audio output requested.");
  set_requested_connection_state_to_output(1);
}

void print_version(void) {
  char version_string[200];
  strcpy(version_string, PACKAGE_VERSION);
#ifdef HAVE_LIBPOLARSSL
  strcat(version_string, "-polarssl");
#endif
#ifdef HAVE_LIBSSL
  strcat(version_string, "-openssl");
#endif
#ifdef CONFIG_TINYSVCMDNS
  strcat(version_string, "-tinysvcmdns");
#endif
#ifdef CONFIG_AVAHI
  strcat(version_string, "-Avahi");
#endif
#ifdef CONFIG_DNS_SD
  strcat(version_string, "-dns_sd");
#endif
#ifdef CONFIG_ALSA
  strcat(version_string, "-ALSA");
#endif
#ifdef CONFIG_SNDIO
  strcat(version_string, "-sndio");
#endif
#ifdef CONFIG_AO
  strcat(version_string, "-ao");
#endif
#ifdef CONFIG_PULSE
  strcat(version_string, "-pulse");
#endif
#ifdef CONFIG_DUMMY
  strcat(version_string, "-dummy");
#endif
#ifdef CONFIG_STDOUT
  strcat(version_string, "-stdout");
#endif
#ifdef CONFIG_PIPE
  strcat(version_string, "-pipe");
#endif
#ifdef HAVE_LIBSOXR
  strcat(version_string, "-soxr");
#endif
#ifdef CONFIG_METADATA
  strcat(version_string, "-metadata");
#endif
  printf("%s\n", version_string);
}

void usage(char *progname) {
  printf("Usage: %s [options...]\n", progname);
  printf("  or:  %s [options...] -- [audio output-specific options]\n", progname);
  printf("\n");
  printf("Options:\n");
  printf("    -h, --help              show this help.\n");
  printf("    -d, --daemon            daemonise.\n");
  printf("    -V, --version           show version information.\n");
  printf("    -k, --kill              kill the existing shairport daemon.\n");
  printf("    -D, --disconnectFromOutput  disconnect immediately from the output device.\n");
  printf("    -R, --reconnectToOutput  reconnect to the output device.\n");
  printf("    -c, --configfile=FILE   read configuration settings from FILE. Default is "
         "/etc/shairport-sync.conf.\n");

  printf("\n");
  printf("The following general options are for backward compatability. These and all new options have settings in the configuration file, by default /etc/shairport-sync.conf:\n");
  printf("    -v, --verbose           -v print debug information; -vv more; -vvv lots.\n");
  printf("    -p, --port=PORT         set RTSP listening port.\n");
  printf("    -a, --name=NAME         set advertised name.\n");
  printf(
      "    -A, --AirPlayLatency=FRAMES [Deprecated] Set the latency for audio sent from an AirPlay device.\n");
  printf("                            The default is to set it automatically.\n");
  printf(
      "    -i, --iTunesLatency=FRAMES [Deprecated] Set the latency for audio sent from iTunes 10 or later.\n");
  printf("                            The default is to set it automatically.\n");
  printf("    -L, --latency=FRAMES    [Deprecated] Set the latency for audio sent from an unknown device.\n");
  printf("                            The default is to set it automatically.\n");
  printf("    --forkedDaapdLatency=FRAMES [Deprecated] Set the latency for audio sent from forked-daapd.\n");
  printf("                            The default is to set it automatically.\n");
  printf("    -S, --stuffing=MODE set how to adjust current latency to match desired latency, where \n");
  printf("                            \"basic\" (default) inserts or deletes audio frames from "
         "packet frames with low processor overhead, and \n");
  printf("                            \"soxr\" uses libsoxr to minimally resample packet frames -- "
         "moderate processor overhead.\n");
  printf(
      "                            \"soxr\" option only available if built with soxr support.\n");
  printf("    -B, --on-start=PROGRAM  run PROGRAM when playback is about to begin.\n");
  printf("    -E, --on-stop=PROGRAM   run PROGRAM when playback has ended.\n");
  printf("                            For -B and -E options, specify the full path to the program, "
         "e.g. /usr/bin/logger.\n");
  printf("                            Executable scripts work, but must have #!/bin/sh (or "
         "whatever) in the headline.\n");
  printf("    -w, --wait-cmd          wait until the -B or -E programs finish before continuing.\n");
  printf("    -o, --output=BACKEND    select audio output method.\n");
  printf("    -m, --mdns=BACKEND      force the use of BACKEND to advertize the service.\n");
  printf("                            if no mdns provider is specified,\n");
  printf("                            shairport tries them all until one works.\n");
  printf("    -r, --resync=THRESHOLD  resync if error exceeds this number of frames. Set to 0 to "
         "stop resyncing.\n");
  printf("    -t, --timeout=SECONDS   go back to idle mode from play mode after a break in "
         "communications of this many seconds (default 120). Set to 0 never to exit play mode.\n");
  printf("    --statistics            print some interesting statistics -- output to the logfile "
         "if running as a daemon.\n");
  printf("    --tolerance=TOLERANCE   allow a synchronization error of TOLERANCE frames (default "
         "88) before trying to correct it.\n");
  printf("    --password=PASSWORD     require PASSWORD to connect. Default is not to require a "
         "password.\n");
#ifdef CONFIG_METADATA
  printf("    --metadata-pipename=PIPE send metadata to PIPE, e.g. "
         "--metadata-pipename=/tmp/shairport-sync-metadata.\n");
  printf("                            The default is /tmp/shairport-sync-metadata.\n");
  printf("    --get-coverart          send cover art through the metadata pipe.\n");
#endif
  printf("\n");
  mdns_ls_backends();
  printf("\n");
  audio_ls_outputs();
}

int parse_options(int argc, char **argv) {
  char *stuffing = NULL; /* used for picking up the stuffing option */
  signed char c;      /* used for argument parsing */
  int i = 0;          /* used for tracking options */
  poptContext optCon; /* context for parsing command-line options */
  struct poptOption optionsTable[] = {
      {"verbose", 'v', POPT_ARG_NONE, NULL, 'v', NULL},
      {"disconnectFromOutput", 'D', POPT_ARG_NONE, NULL, 0, NULL},
      {"reconnectToOutput", 'R', POPT_ARG_NONE, NULL, 0, NULL},
      {"kill", 'k', POPT_ARG_NONE, NULL, 0, NULL},
      {"daemon", 'd', POPT_ARG_NONE, &config.daemonise, 0, NULL},
      {"configfile", 'c', POPT_ARG_STRING, &config.configfile, 0, NULL},
      {"statistics", 0, POPT_ARG_NONE, &config.statistics_requested, 0, NULL},
      {"version", 'V', POPT_ARG_NONE, NULL, 0, NULL},
      {"port", 'p', POPT_ARG_INT, &config.port, 0, NULL},
      {"name", 'a', POPT_ARG_STRING, &config.apname, 0, NULL},
      {"output", 'o', POPT_ARG_STRING, &config.output_name, 0, NULL},
      {"on-start", 'B', POPT_ARG_STRING, &config.cmd_start, 0, NULL},
      {"on-stop", 'E', POPT_ARG_STRING, &config.cmd_stop, 0, NULL},
      {"wait-cmd", 'w', POPT_ARG_NONE, &config.cmd_blocking, 0, NULL},
      {"mdns", 'm', POPT_ARG_STRING, &config.mdns_name, 0, NULL},
      {"latency", 'L', POPT_ARG_INT, &config.userSuppliedLatency, 0, NULL},
      {"AirPlayLatency", 'A', POPT_ARG_INT, &config.AirPlayLatency, 0, NULL},
      {"iTunesLatency", 'i', POPT_ARG_INT, &config.iTunesLatency, 0, NULL},
      {"forkedDaapdLatency", 0, POPT_ARG_INT, &config.ForkedDaapdLatency, 0, NULL},
      {"stuffing", 'S', POPT_ARG_STRING, &stuffing, 'S', NULL},
      {"resync", 'r', POPT_ARG_INT, &config.resyncthreshold, 0, NULL},
      {"timeout", 't', POPT_ARG_INT, &config.timeout, 't', NULL},
      {"password", 0, POPT_ARG_STRING, &config.password, 0, NULL},
      {"tolerance", 0, POPT_ARG_INT, &config.tolerance, 0, NULL},
#ifdef CONFIG_METADATA
      {"metadata-pipename", 'M', POPT_ARG_STRING, &config.metadata_pipename, 'M', NULL},
      {"get-coverart", 'g', POPT_ARG_NONE, &config.get_coverart, 'g', NULL},
#endif
      POPT_AUTOHELP
      {NULL, 0, 0, NULL, 0}};

// we have to parse the command line arguments to look for a config file
  int optind;
  optind = argc;
  int j;
  for (j = 0; j < argc; j++)
    if (strcmp(argv[j], "--") == 0)
      optind = j;

  optCon = poptGetContext(NULL, optind, (const char **)argv, optionsTable, 0);
  poptSetOtherOptionHelp(optCon, "[OPTIONS]* ");
  
   /* Now do options processing just to get a debug level */
  debuglev = 0;
  while ((c = poptGetNextOpt(optCon)) >= 0) {
    switch (c) {
    case 'v':
      debuglev++;
      break;
    }
  }
  if (c < -1) {
    die("%s: %s", poptBadOption(optCon, POPT_BADOPTION_NOALIAS), poptStrerror(c));
  }

  config_setting_t *setting;
  const char *str;
  int value;

  debug(1,"Looking for the configuration file \"%s\".",config.configfile);
  
  config_init(&config_file_stuff);
  
  char *config_file_real_path = realpath(config.configfile, NULL);
  if (config_file_real_path==NULL) {
    debug(2,"Can't resolve the configuration file \"%s\".",config.configfile);
  } else {
    debug(2, "Looking for configuration file at full path \"%s\"", config_file_real_path);
    /* Read the file. If there is an error, report it and exit. */
    if (config_read_file(&config_file_stuff, config_file_real_path)) {
      // make config.cfg point to it
      config.cfg = &config_file_stuff;
      /* Get the Service Name. */
      if (config_lookup_string(config.cfg, "general.name", &str))
        config.apname = (char *)str;

      /* Get the Daemonize setting. */
      if (config_lookup_string(config.cfg, "general.daemonize", &str)) {
        if (strcasecmp(str, "no") == 0)
          config.daemonise = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.daemonise = 1;
        else
          die("Invalid daemonize option choice \"%s\". It should be \"yes\" or \"no\"");
      }

      /* Get the mdns_backend setting. */
      if (config_lookup_string(config.cfg, "general.mdns_backend", &str))
        config.mdns_name = (char *)str;

      /* Get the output_backend setting. */
      if (config_lookup_string(config.cfg, "general.output_backend", &str))
        config.output_name = (char *)str;

      /* Get the port setting. */
      if (config_lookup_int(config.cfg, "general.port", &value)) {
        if ((value < 0) || (value > 65535))
          die("Invalid port number  \"%sd\". It should be between 0 and 65535, default is 5000",
              value);
        else
          config.port = value;
      }

      /* Get the udp port base setting. */
      if (config_lookup_int(config.cfg, "general.udp_port_base", &value)) {
        if ((value < 0) || (value > 65535))
          die("Invalid port number  \"%sd\". It should be between 0 and 65535, default is 6001",
              value);
        else
          config.udp_port_base = value;
      }

      /* Get the udp port range setting. This is number of ports that will be tried for free ports , starting at the port base. Only three ports are needed. */
      if (config_lookup_int(config.cfg, "general.udp_port_range", &value)) {
        if ((value < 0) || (value > 65535))
          die("Invalid port range  \"%sd\". It should be between 0 and 65535, default is 100",
              value);
        else
          config.udp_port_range = value;
      }

      /* Get the password setting. */
      if (config_lookup_string(config.cfg, "general.password", &str))
        config.password = (char *)str;

      if (config_lookup_string(config.cfg, "general.interpolation", &str)) {
        if (strcasecmp(str, "basic") == 0)
          config.packet_stuffing = ST_basic;
        else if (strcasecmp(str, "soxr") == 0)
          config.packet_stuffing = ST_soxr;
        else
          die("Invalid interpolation option choice \"%s\". It should be \"basic\" or \"soxr\"");
      }

      /* Get the statistics setting. */
      if (config_lookup_string(config.cfg, "general.statistics", &str)) {
        if (strcasecmp(str, "no") == 0)
          config.statistics_requested = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.statistics_requested = 1;
        else
          die("Invalid statistics option choice \"%s\". It should be \"yes\" or \"no\"");
      }

      /* Get the drift tolerance setting. */
      if (config_lookup_int(config.cfg, "general.drift", &value))
        config.tolerance = value;

      /* Get the resync setting. */
      if (config_lookup_int(config.cfg, "general.resync_threshold", &value))
        config.resyncthreshold = value;

      /* Get the verbosity setting. */
      if (config_lookup_int(config.cfg, "general.log_verbosity", &value)) {
        if ((value >= 0) && (value <= 3))
          debuglev = value;
        else
          die("Invalid log verbosity setting option choice \"%d\". It should be between 0 and 3, "
              "inclusive.",
              value);
      }

      /* Get the ignore_volume_control setting. */
      if (config_lookup_string(config.cfg, "general.ignore_volume_control", &str)) {
        if (strcasecmp(str, "no") == 0)
          config.ignore_volume_control = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.ignore_volume_control = 1;
        else
          die("Invalid ignore_volume_control option choice \"%s\". It should be \"yes\" or \"no\"");
      }

      /* Get the volume range, in dB, that should be used If not set, it means you just use the range set by the mixer. */
      if (config_lookup_int(config.cfg, "general.volume_range_db", &value)) {
        if ((value < 30) || (value > 150))
          die("Invalid volume range  \"%sd\". It should be between 30 and 150 dB. Zero means use the mixer's native range",
              value);
        else
          config.volume_range_db = value;
      }

/* Get the default latency. Deprecated! */
      if (config_lookup_int(config.cfg, "latencies.default", &value))
        config.userSuppliedLatency = value;

      /* Get the itunes latency. Deprecated! */
      if (config_lookup_int(config.cfg, "latencies.itunes", &value))
        config.iTunesLatency = value;

      /* Get the AirPlay latency. Deprecated! */
      if (config_lookup_int(config.cfg, "latencies.airplay", &value))
        config.AirPlayLatency = value;

      /* Get the forkedDaapd latency. Deprecated! */
      if (config_lookup_int(config.cfg, "latencies.forkedDaapd", &value))
        config.ForkedDaapdLatency = value;

  #ifdef CONFIG_METADATA
      /* Get the metadata setting. */
      if (config_lookup_string(config.cfg, "metadata.enabled", &str)) {
        if (strcasecmp(str, "no") == 0)
          config.metadata_enabled = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.metadata_enabled = 1;
        else
          die("Invalid metadata enabled option choice \"%s\". It should be \"yes\" or \"no\"");
      }

      if (config_lookup_string(config.cfg, "metadata.include_cover_art", &str)) {
        if (strcasecmp(str, "no") == 0)
          config.get_coverart = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.get_coverart = 1;
        else
          die("Invalid metadata include_cover_art option choice \"%s\". It should be \"yes\" or "
              "\"no\"");
      }

      if (config_lookup_string(config.cfg, "metadata.pipe_name", &str)) {
        config.metadata_pipename = (char *)str;
      }
  #endif

      if (config_lookup_string(config.cfg, "sessioncontrol.run_this_before_play_begins", &str)) {
        config.cmd_start = (char *)str;
      }

      if (config_lookup_string(config.cfg, "sessioncontrol.run_this_after_play_ends", &str)) {
        config.cmd_stop = (char *)str;
      }

      if (config_lookup_string(config.cfg, "sessioncontrol.wait_for_completion", &str)) {
        if (strcasecmp(str, "no") == 0)
          config.cmd_blocking = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.cmd_blocking = 1;
        else
          die("Invalid session control wait_for_completion option choice \"%s\". It should be "
              "\"yes\" or \"no\"");
      }

      if (config_lookup_string(config.cfg, "sessioncontrol.allow_session_interruption", &str)) {
        config.dont_check_timeout = 0; // this is for legacy -- only set by -t 0
        if (strcasecmp(str, "no") == 0)
          config.allow_session_interruption = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.allow_session_interruption = 1;
        else
          die("Invalid session control allow_interruption option choice \"%s\". It should be \"yes\" "
              "or \"no\"");
      }

      if (config_lookup_int(config.cfg, "sessioncontrol.session_timeout", &value)) {
        config.timeout = value;
        config.dont_check_timeout = 0; // this is for legacy -- only set by -t 0
      }

    } else {
      if (config_error_type(&config_file_stuff) == CONFIG_ERR_FILE_IO)
        debug(1, "Error reading configuration file \"%s\": \"%s\".",
              config_error_file(&config_file_stuff), config_error_text(&config_file_stuff));
      else {
        die("Line %d of the configuration file \"%s\":\n%s", config_error_line(&config_file_stuff),
            config_error_file(&config_file_stuff), config_error_text(&config_file_stuff));
      }
    }
    free(config_file_real_path);
  }

// now, do the command line options again, but this time do them fully -- it's a unix convention that command line
// arguments have precedence over configuration file settings.

  optind = argc;
  for (j = 0; j < argc; j++)
    if (strcmp(argv[j], "--") == 0)
      optind = j;

  optCon = poptGetContext(NULL, optind, (const char **)argv, optionsTable, 0);
  poptSetOtherOptionHelp(optCon, "[OPTIONS]* ");

  /* Now do options processing, get portname */
  int tdebuglev = 0;
  while ((c = poptGetNextOpt(optCon)) >= 0) {
    switch (c) {
    case 'v':
      tdebuglev++;
      break;
    case 't':
      if (config.timeout == 0) {
        config.dont_check_timeout = 1;
        config.allow_session_interruption = 1;
      } else {
        config.dont_check_timeout = 0;
        config.allow_session_interruption = 0;
      }
      break;
#ifdef CONFIG_METADATA
    case 'M':
      config.metadata_enabled = 1;
      break;
    case 'g':
      if (config.metadata_enabled == 0)
        die("If you want to get cover art, you must also select the --metadata-pipename option.");
      break;
#endif
    case 'S':
      if (strcmp(stuffing, "basic") == 0)
        config.packet_stuffing = ST_basic;
      else if (strcmp(stuffing, "soxr") == 0)
#ifdef HAVE_LIBSOXR
        config.packet_stuffing = ST_soxr;
#else
        die("soxr option not available -- this version of shairport-sync was built without libsoxr "
            "support");
#endif
      else
        die("Illegal stuffing option \"%s\" -- must be \"basic\" or \"soxr\"", stuffing);
      break;
    }
  }
  if (c < -1) {
    die("%s: %s", poptBadOption(optCon, POPT_BADOPTION_NOALIAS), poptStrerror(c));
  }
  
#ifdef CONFIG_METADATA
  if ((config.metadata_enabled == 1) && (config.metadata_pipename == NULL))
    config.metadata_pipename=strdup("/tmp/shairport-sync-metadata");
#endif

  
  if (tdebuglev!=0)
    debuglev = tdebuglev;
  return optind + 1;
}

void signal_setup(void) {
  // mask off all signals before creating threads.
  // this way we control which thread gets which signals.
  // for now, we don't care which thread gets the following.
  sigset_t set;
  sigfillset(&set);
  sigdelset(&set, SIGINT);
  sigdelset(&set, SIGTERM);
  sigdelset(&set, SIGHUP);
  sigdelset(&set, SIGSTOP);
  sigdelset(&set, SIGCHLD);
  sigdelset(&set, SIGUSR2);
  pthread_sigmask(SIG_BLOCK, &set, NULL);

  // setting this to SIG_IGN would prevent signalling any threads.
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_flags = SA_SIGINFO;
  sa.sa_sigaction = &sig_ignore;
  sigaction(SIGUSR1, &sa, NULL);

  sa.sa_flags = SA_SIGINFO | SA_RESTART;
  sa.sa_sigaction = &sig_shutdown;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  sa.sa_sigaction = &sig_disconnect_audio_output;
  sigaction(SIGUSR2, &sa, NULL);

  sa.sa_sigaction = &sig_connect_audio_output;
  sigaction(SIGHUP, &sa, NULL);

  sa.sa_sigaction = &sig_child;
  sigaction(SIGCHLD, &sa, NULL);
}

// forked daemon lets the spawner know it's up and running OK
// should be called only once!
void shairport_startup_complete(void) {
  if (config.daemonise) {
    //        daemon_ready();
  }
}

#ifdef USE_CUSTOM_PID_DIR

const char *pid_file_proc(void) {
#ifdef HAVE_ASPRINTF
  static char *fn = NULL;
  free(fn);
  asprintf(&fn, "%s/%s.pid", PIDDIR, daemon_pid_file_ident ? daemon_pid_file_ident : "unknown");
#else
  static char fn[8192];
  snprintf(fn, sizeof(fn), "%s/%s.pid", PIDDIR,
           daemon_pid_file_ident ? daemon_pid_file_ident : "unknown");
#endif

  return fn;
}
#endif

void exit_function() {
  if (config.cfg)
    config_destroy(config.cfg);
}

int main(int argc, char **argv) {

  daemon_set_verbosity(LOG_DEBUG);
  memset(&config, 0, sizeof(config)); // also clears all strings, BTW
  atexit(exit_function);

  // this is a bit weird, but apparently necessary
  char *basec = strdup(argv[0]);
  char *bname = basename(basec);
  appName = strdup(bname);
  free(basec);

  // set defaults
  
  
  // get thje endianness
  union {
   uint32_t u32;
   uint8_t arr[4];
  } xn;

  xn.arr[0] = 0x44;     /* Lowest-address byte */
  xn.arr[1] = 0x33;
  xn.arr[2] = 0x22;
  xn.arr[3] = 0x11;     /* Highest-address byte */
  
  if (xn.u32==0x11223344)
    endianness = __LITTLE_ENDIAN;
  else if (xn.u32==0x33441122)
    endianness = __PDP_ENDIAN;
  else if (xn.u32==0x44332211)
    endianness = __BIG_ENDIAN;
  else die("Can not recognise the endianness of the processor.");
  
  strcpy(configuration_file_path, "/etc/");
  strcat(configuration_file_path, appName);
  strcat(configuration_file_path, ".conf");
  config.configfile = configuration_file_path;

  config.statistics_requested = 0; // don't print stats in the log
  config.latency = -1;             // -1 means not set. 88200 works well. This is also reset in rtsp.c when play is about to start
  config.userSuppliedLatency = 0;  // zero means none supplied
  config.iTunesLatency = -1;       // -1 means not supplied. 99400 seems to work pretty well for iTunes from Version 10 (?)
                                   // upwards-- two left-ear headphones, one from the iMac jack, one
                                   // from an NSLU2 running a cheap "3D Sound" USB Soundcard
  config.AirPlayLatency = -1;      // -1 means not set. 88200 seems to work well for AirPlay -- Syncs sound and
                                   // vision on AppleTV, but also used for iPhone/iPod/iPad sources
  config.ForkedDaapdLatency = -1;  // -1 means not set. 99400 seems to be right
  config.resyncthreshold = 441 * 5;  // this number of frames is 50 ms
  config.timeout = 120; // this number of seconds to wait for [more] audio before switching to idle.
  config.tolerance = 88; // this number of frames of error before attempting to correct it.
  config.buffer_start_fill = 220;
  config.port = 5000;
  config.packet_stuffing = ST_basic; // simple interpolation or deletion
  char hostname[100];
  gethostname(hostname, 100);
  config.apname = malloc(20 + 100);
  snprintf(config.apname, 20 + 100, "Shairport Sync on %s", hostname);
  set_requested_connection_state_to_output(1); // we expect to be able to connect to the output device
  config.audio_backend_buffer_desired_length = 6615; // 0.15 seconds.
  config.udp_port_base = 6001;
  config.udp_port_range = 100;

  /* Check if we are called with -V or --version parameter */
  if (argc >= 2 && ((strcmp(argv[1], "-V") == 0) || (strcmp(argv[1], "--version") == 0))) {
    print_version();
    exit(1);
  }

  /* Check if we are called with -h or --help parameter */
  if (argc >= 2 && ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0))) {
    usage(argv[0]);
    exit(1);
  }

  pid_t pid;

  /* Reset signal handlers */
  if (daemon_reset_sigs(-1) < 0) {
    daemon_log(LOG_ERR, "Failed to reset all signal handlers: %s", strerror(errno));
    return 1;
  }

  /* Unblock signals */
  if (daemon_unblock_sigs(-1) < 0) {
    daemon_log(LOG_ERR, "Failed to unblock all signals: %s", strerror(errno));
    return 1;
  }

#if USE_CUSTOM_PID_DIR
  debug(1, "Locating custom pid dir at \"%s\"", PIDDIR);
  /* Point to a function to help locate where the PID file will go */
  daemon_pid_file_proc = pid_file_proc;
#endif

  /* Set indentification string for the daemon for both syslog and PID file */
  daemon_pid_file_ident = daemon_log_ident = daemon_ident_from_argv0(argv[0]);

  /* Check if we are called with -D or --disconnectFromOutput parameter */
  if (argc >= 2 &&
      ((strcmp(argv[1], "-D") == 0) || (strcmp(argv[1], "--disconnectFromOutput") == 0))) {
    if ((pid = daemon_pid_file_is_running()) >= 0) {
      if (kill(pid, SIGUSR2) != 0) { // try to send the signal
        daemon_log(LOG_WARNING,
                   "Failed trying to send disconnectFromOutput command to daemon pid: %d: %s", pid,
                   strerror(errno));
      }
    } else {
      daemon_log(LOG_WARNING,
                 "Can't send a disconnectFromOutput request -- Failed to find daemon: %s",
                 strerror(errno));
    }
    exit(1);
  }

  /* Check if we are called with -R or --reconnectToOutput parameter */
  if (argc >= 2 &&
      ((strcmp(argv[1], "-R") == 0) || (strcmp(argv[1], "--reconnectToOutput") == 0))) {
    if ((pid = daemon_pid_file_is_running()) >= 0) {
      if (kill(pid, SIGHUP) != 0) { // try to send the signal
        daemon_log(LOG_WARNING,
                   "Failed trying to send reconnectToOutput command to daemon pid: %d: %s", pid,
                   strerror(errno));
      }
    } else {
      daemon_log(LOG_WARNING, "Can't send a reconnectToOutput request -- Failed to find daemon: %s",
                 strerror(errno));
    }
    exit(1);
  }

  /* Check if we are called with -k or --kill parameter */
  if (argc >= 2 && ((strcmp(argv[1], "-k") == 0) || (strcmp(argv[1], "--kill") == 0))) {
    int ret;

    /* Kill daemon with SIGTERM */
    /* Check if the new function daemon_pid_file_kill_wait() is available, if it is, use it. */
    if ((ret = daemon_pid_file_kill_wait(SIGTERM, 5)) < 0)
      daemon_log(LOG_WARNING, "Failed to kill daemon: %s", strerror(errno));
    else
      daemon_pid_file_remove();
    return ret < 0 ? 1 : 0;
  }

  /* Check that the daemon is not running twice at the same time */
  if ((pid = daemon_pid_file_is_running()) >= 0) {
    daemon_log(LOG_ERR, "Daemon already running on PID file %u", pid);
    return 1;
  }

  // parse arguments into config
  int audio_arg = parse_options(argc, argv);

  // mDNS supports maximum of 63-character names (we append 13).
  if (strlen(config.apname) > 50)
    die("Supplied name too long (max 50 characters)");

  /* here, daemonise with libdaemon */

  if (config.daemonise) {
    /* Prepare for return value passing from the initialization procedure of the daemon process */
    if (daemon_retval_init() < 0) {
      daemon_log(LOG_ERR, "Failed to create pipe.");
      return 1;
    }

    /* Do the fork */
    if ((pid = daemon_fork()) < 0) {

      /* Exit on error */
      daemon_retval_done();
      return 1;

    } else if (pid) { /* The parent */
      int ret;

      /* Wait for 20 seconds for the return value passed from the daemon process */
      if ((ret = daemon_retval_wait(20)) < 0) {
        daemon_log(LOG_ERR, "Could not receive return value from daemon process: %s",
                   strerror(errno));
        return 255;
      }

      if (ret != 0)
        daemon_log(ret != 0 ? LOG_ERR : LOG_INFO, "Daemon returned %i as return value.", ret);
      return ret;

    } else { /* The daemon */

      /* Close FDs */
      if (daemon_close_all(-1) < 0) {
        daemon_log(LOG_ERR, "Failed to close all file descriptors: %s", strerror(errno));

        /* Send the error condition to the parent process */
        daemon_retval_send(1);
        goto finish;
      }

      /* Create the PID file */
      if (daemon_pid_file_create() < 0) {
        daemon_log(LOG_ERR, "Could not create PID file (%s).", strerror(errno));
        daemon_retval_send(2);
        goto finish;
      }

      /* Send OK to parent process */
      daemon_retval_send(0);
    }
    /* end libdaemon stuff */
  }

  signal_setup();

  // make sure the program can create files that group and world can read
  umask(S_IWGRP | S_IWOTH);

  config.output = audio_get_output(config.output_name);
  if (!config.output) {
    audio_ls_outputs();
    die("Invalid audio output specified!");
  }
  config.output->init(argc - audio_arg, argv + audio_arg);

  daemon_log(LOG_NOTICE, "startup");
  
  switch (endianness) {
    case __LITTLE_ENDIAN:
      debug(2,"The processor is running little-endian.");
      break;
    case __BIG_ENDIAN:
      debug(2,"The processor is running big-endian.");
      break;
    case __PDP_ENDIAN:
      debug(2,"The processor is running pdp-endian.");
      break; 
  }
  
  /* Mess around with the latency options */
  // Basically, we used to rely on static latencies -- 99400 for iTunes 10 or later and forkedDaapd, 88200 for everything else
  // Nowadays we allow the source to set the latency, which works out at 99651 for iTunes 10 and forkedDaapd and 88220 for everything else
  // What we want to do here is allow the source to set the latency unless the user has specified an non-standard latency.
  // If the user has specified a standard latency, we suggest to them to stop doing it.
  // If they specify a non-standard latency, we suggest the user to use the audio_backend_latency_offset instead.
  
  if (config.AirPlayLatency!=-1) {
    if (config.AirPlayLatency==88200) {
      inform("It is not necessary to set the AirPlay latency to 88200 -- you should remove this setting or configuration option, as it is deprecated.");
      config.AirPlayLatency= -1;
    } else {
      inform("The AirPlay latency setting is deprecated, as Shairport Sync can now get the correct latency from the source.");
      inform("Please remove this setting and use the relevant audio_backend_latency_offset setting, if necessary, to compensate for delays elsewhere.");
    }
  }
  
  if (config.iTunesLatency!=-1) {
    if (config.iTunesLatency==99400) {
      inform("It is not necessary to set the iTunes latency to 99400 -- you should remove this setting or configuration option, as it is deprecated and ignored.");
      config.iTunesLatency= -1;
    } else {
      inform("The iTunes latency setting is deprecated, as Shairport Sync can now get the correct latency from the source.");
      inform("Please remove this setting and use the relevant audio_backend_latency_offset setting, if necessary, to compensate for delays elsewhere.");
    }
  }
  
  if (config.ForkedDaapdLatency!=-1) {
    if (config.ForkedDaapdLatency==99400) {
      inform("It is not necessary to set the forkedDaapd latency to 99400 -- you should remove this setting or configuration option, as it is deprecated and ignored.");
      config.ForkedDaapdLatency= -1;
    } else {
      inform("The forkedDaapd latency setting is deprecated, as Shairport Sync can now get the correct latency from the source.");
      inform("Please remove this setting and use the relevant audio_backend_latency_offset setting, if necessary, to compensate for delays elsewhere.");
    }
  }

  if (config.userSuppliedLatency) {
    inform("The default latency setting is deprecated, as Shairport Sync can now get the correct latency from the source.");
    inform("Please remove this setting and use the relevant audio_backend_latency_offset setting, if necessary, to compensate for delays elsewhere.");
  }
  
  
  /* Print out options */

  debug(2, "statistics_requester status is %d.", config.statistics_requested);
  debug(2, "daemon status is %d.", config.daemonise);
  debug(2, "rtsp listening port is %d.", config.port);
  debug(2, "udp base port is %d.", config.udp_port_base);
  debug(2, "udp port range is %d.", config.udp_port_range);
  debug(2, "Shairport Sync player name is \"%s\".", config.apname);
  debug(2, "Audio Output name is \"%s\".", config.output_name);
  debug(2, "on-start action is \"%s\".", config.cmd_start);
  debug(2, "on-stop action is \"%s\".", config.cmd_stop);
  debug(2, "wait-cmd status is %d.", config.cmd_blocking);
  debug(2, "mdns backend \"%s\".", config.mdns_name);
  debug(2, "userSuppliedLatency is %d.", config.userSuppliedLatency);
  debug(2, "AirPlayLatency is %d.", config.AirPlayLatency);
  debug(2, "iTunesLatency is %d.", config.iTunesLatency);
  debug(2, "forkedDaapdLatency is %d.", config.ForkedDaapdLatency);
  debug(2, "stuffing option is \"%d\".", config.packet_stuffing);
  debug(2, "resync time is %d.", config.resyncthreshold);
  debug(2, "allow a session to be interrupted: %d.", config.allow_session_interruption);
  debug(2, "busy timeout time is %d.", config.timeout);
  debug(2, "tolerance is %d frames.", config.tolerance);
  debug(2, "password is \"%s\".", config.password);
  debug(2, "ignore_volume_control is %d.", config.ignore_volume_control);
  debug(2, "audio backend desired buffer length is %d.",
        config.audio_backend_buffer_desired_length);
  debug(2, "audio backend latency offset is %d.", config.audio_backend_latency_offset);
  debug(2, "volume range in dB (zero means use the range specified by the mixer): %u.", config.volume_range_db);
  
  char *realConfigPath = realpath(config.configfile,NULL);
  if (realConfigPath) {
    debug(2, "configuration file name \"%s\" resolves to \"%s\".", config.configfile,realConfigPath);
    free(realConfigPath);
  } else {
     debug(2, "configuration file name \"%s\" can not be resolved.", config.configfile);
  }   
#ifdef CONFIG_METADATA
  debug(2, "metdata enabled is %d.", config.metadata_enabled);
  debug(2, "metadata pipename is \"%s\".", config.metadata_pipename);
  debug(2, "get-coverart is %d.", config.get_coverart);
#endif

  uint8_t ap_md5[16];

#ifdef HAVE_LIBSSL
  MD5_CTX ctx;
  MD5_Init(&ctx);
  MD5_Update(&ctx, config.apname, strlen(config.apname));
  MD5_Final(ap_md5, &ctx);
#endif

#ifdef HAVE_LIBPOLARSSL
  md5_context tctx;
  md5_starts(&tctx);
  md5_update(&tctx, (unsigned char *)config.apname, strlen(config.apname));
  md5_finish(&tctx, ap_md5);
#endif
  memcpy(config.hw_addr, ap_md5, sizeof(config.hw_addr));
#ifdef CONFIG_METADATA
  metadata_init(); // create the metadata pipe if necessary
#endif
  rtsp_listen_loop();

  // should not reach this...
  shairport_shutdown();
finish:
  daemon_log(LOG_NOTICE, "Unexpected exit...");
  daemon_retval_send(255);
  daemon_pid_file_remove();
  return 1;
}
