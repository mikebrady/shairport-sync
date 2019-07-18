/*
 * Shairport, an Apple Airplay receiver
 * Copyright (c) James Laird 2013
 * All rights reserved.
 * Modifications and additions (c) Mike Brady 2014--2019
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

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libconfig.h>
#include <libgen.h>
#include <memory.h>
#include <popt.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <net/if.h>
#include <unistd.h>


#include "config.h"

#ifdef CONFIG_MBEDTLS
#include <mbedtls/md5.h>
#include <mbedtls/version.h>
#endif

#ifdef CONFIG_POLARSSL
#include <polarssl/md5.h>
#endif

#ifdef CONFIG_OPENSSL
#include <openssl/md5.h>
#endif

#if defined(CONFIG_DBUS_INTERFACE)
#include <glib.h>
#endif

#include "activity_monitor.h"
#include "common.h"
#include "rtp.h"
#include "rtsp.h"

#if defined(CONFIG_DACP_CLIENT)
#include "dacp.h"
#endif

#if defined(CONFIG_METADATA_HUB)
#include "metadata_hub.h"
#endif

#ifdef CONFIG_DBUS_INTERFACE
#include "dbus-service.h"
#endif

#ifdef CONFIG_MQTT
#include "mqtt.h"
#endif

#ifdef CONFIG_MPRIS_INTERFACE
#include "mpris-service.h"
#endif

#ifdef CONFIG_LIBDAEMON
#include <libdaemon/dexec.h>
#include <libdaemon/dfork.h>
#include <libdaemon/dlog.h>
#include <libdaemon/dpid.h>
#include <libdaemon/dsignal.h>
#else
#include <syslog.h>
#endif

#ifdef CONFIG_SOXR
#include <soxr.h>
#include <math.h>
#endif

#ifdef CONFIG_CONVOLUTION
#include <FFTConvolver/convolver.h>
#endif


#ifdef CONFIG_LIBDAEMON
pid_t pid;
#endif

int killOption = 0;
int daemonisewith = 0;
int daemonisewithout = 0;

// static int shutting_down = 0;
char configuration_file_path[4096 + 1];
char actual_configuration_file_path[4096 + 1];

static void sig_ignore(__attribute__((unused)) int foo, __attribute__((unused)) siginfo_t *bar,
                       __attribute__((unused)) void *baz) {}
static void sig_shutdown(__attribute__((unused)) int foo, __attribute__((unused)) siginfo_t *bar,
                         __attribute__((unused)) void *baz) {
  debug(2, "shutdown requested...");
#ifdef CONFIG_LIBDAEMON
  if (pid == 0) {
    daemon_retval_send(255);
    daemon_pid_file_remove();
  }
#endif
  exit(EXIT_SUCCESS);
}

static void sig_child(__attribute__((unused)) int foo, __attribute__((unused)) siginfo_t *bar,
                      __attribute__((unused)) void *baz) {
  // wait for child processes to exit
  pid_t pid;
  while ((pid = waitpid((pid_t)-1, 0, WNOHANG)) > 0) {
  }
}

static void sig_disconnect_audio_output(__attribute__((unused)) int foo,
                                        __attribute__((unused)) siginfo_t *bar,
                                        __attribute__((unused)) void *baz) {
  debug(1, "disconnect audio output requested.");
  set_requested_connection_state_to_output(0);
}

static void sig_connect_audio_output(__attribute__((unused)) int foo,
                                     __attribute__((unused)) siginfo_t *bar,
                                     __attribute__((unused)) void *baz) {
  debug(1, "connect audio output requested.");
  set_requested_connection_state_to_output(1);
}

void print_version(void) {
  char *version_string = get_version_string();
  if (version_string) {
    printf("%s\n", version_string);
    free(version_string);
  } else {
    debug(1, "Can't print version string!");
  }
}

#ifdef CONFIG_SOXR
pthread_t	soxr_time_check_thread;
void* soxr_time_check(__attribute__((unused)) void *arg) {
  const int buffer_length = 352;
  int32_t inbuffer[buffer_length*2];
  int32_t outbuffer[(buffer_length+1)*2];
  
  //int32_t *outbuffer = (int32_t*)malloc((buffer_length+1)*2*sizeof(int32_t));
  //int32_t *inbuffer = (int32_t*)malloc((buffer_length)*2*sizeof(int32_t));
  
  // generate a sample signal
  const double frequency = 440; // 
  
  int i;
     
  int number_of_iterations = 0;
  uint64_t soxr_start_time = get_absolute_time_in_fp();
  uint64_t loop_until_time = (uint64_t)0x180000000 + soxr_start_time; // loop for a second and a half, max
  while (get_absolute_time_in_fp() < loop_until_time) {
  
    number_of_iterations++;
    for (i = 0; i < buffer_length ; i++) {
      double w = sin(i * (frequency + number_of_iterations * 2) * 2 * M_PI/44100);
      int32_t wint = (int32_t)(w * INT32_MAX);
      inbuffer[i * 2] = wint;
      inbuffer[i * 2 + 1] = wint;
    }
  
    soxr_io_spec_t io_spec;
    io_spec.itype = SOXR_INT32_I;
    io_spec.otype = SOXR_INT32_I;
    io_spec.scale = 1.0; // this seems to crash if not = 1.0
    io_spec.e = NULL;
    io_spec.flags = 0;

    size_t odone;

    soxr_oneshot(buffer_length, buffer_length + 1, 2, // Rates and # of chans.
                                      inbuffer, buffer_length, NULL,        // Input.
                                      outbuffer, buffer_length + 1, &odone, // Output.
                                      &io_spec,    // Input, output and transfer spec.
                                      NULL, NULL); // Default configuration.
                                     
    io_spec.itype = SOXR_INT32_I;
    io_spec.otype = SOXR_INT32_I;
    io_spec.scale = 1.0; // this seems to crash if not = 1.0
    io_spec.e = NULL;
    io_spec.flags = 0;

    soxr_oneshot(buffer_length, buffer_length - 1, 2, // Rates and # of chans.
                                      inbuffer, buffer_length, NULL,        // Input.
                                      outbuffer, buffer_length - 1, &odone, // Output.
                                      &io_spec,    // Input, output and transfer spec.
                                      NULL, NULL); // Default configuration.
  	
  }

  double soxr_execution_time_us =
    (((get_absolute_time_in_fp() - soxr_start_time) * 1000000) >> 32) * 1.0;
  // free(outbuffer);
  // free(inbuffer);
  config.soxr_delay_index = (int)(0.9 + soxr_execution_time_us/(number_of_iterations *1000));
  debug(2,"soxr_delay_index: %d.", config.soxr_delay_index);
  if ((config.packet_stuffing == ST_soxr) && (config.soxr_delay_index > config.soxr_delay_threshold))
  	inform("Note: this device may be too slow for \"soxr\" interpolation. Consider choosing the \"basic\" or \"auto\" interpolation setting.");
  if (config.packet_stuffing == ST_auto)
    debug(1,"\"%s\" interpolation has been chosen.", config.soxr_delay_index <= config.soxr_delay_threshold ? "soxr" : "basic");
	pthread_exit(NULL);
}

#endif

void usage(char *progname) {
  printf("Usage: %s [options...]\n", progname);
  printf("  or:  %s [options...] -- [audio output-specific options]\n", progname);
  printf("\n");
  printf("Options:\n");
  printf("    -h, --help              show this help.\n");
#ifdef CONFIG_LIBDAEMON
  printf("    -d, --daemon            daemonise.\n");
  printf("    -j, --justDaemoniseNoPIDFile            daemonise without a PID file.\n");
  printf("    -k, --kill              kill the existing shairport daemon.\n");
#endif
  printf("    -V, --version           show version information.\n");
  printf("    -c, --configfile=FILE   read configuration settings from FILE. Default is "
         "/etc/shairport-sync.conf.\n");

  printf("\n");
  printf("The following general options are for backward compatibility. These and all new options "
         "have settings in the configuration file, by default /etc/shairport-sync.conf:\n");
  printf("    -v, --verbose           -v print debug information; -vv more; -vvv lots.\n");
  printf("    -p, --port=PORT         set RTSP listening port.\n");
  printf("    -a, --name=NAME         set advertised name.\n");
  printf("    -L, --latency=FRAMES    [Deprecated] Set the latency for audio sent from an unknown "
         "device.\n");
  printf("                            The default is to set it automatically.\n");
  printf("    -S, --stuffing=MODE set how to adjust current latency to match desired latency, "
         "where \n");
  printf("                            \"basic\" inserts or deletes audio frames from "
         "packet frames with low processor overhead, and \n");
  printf("                            \"soxr\" uses libsoxr to minimally resample packet frames -- "
         "moderate processor overhead.\n");
  printf(
      "                            \"soxr\" option only available if built with soxr support.\n");
  printf("    -B, --on-start=PROGRAM  run PROGRAM when playback is about to begin.\n");
  printf("    -E, --on-stop=PROGRAM   run PROGRAM when playback has ended.\n");
  printf("                            For -B and -E options, specify the full path to the program, "
         "e.g. /usr/bin/logger.\n");
  printf(
      "                            Executable scripts work, but must have the appropriate shebang "
      "(#!/bin/sh) in the headline.\n");
  printf(
      "    -w, --wait-cmd          wait until the -B or -E programs finish before continuing.\n");
  printf("    -o, --output=BACKEND    select audio output method.\n");
  printf("    -m, --mdns=BACKEND      force the use of BACKEND to advertize the service.\n");
  printf("                            if no mdns provider is specified,\n");
  printf("                            shairport tries them all until one works.\n");
  printf("    -r, --resync=THRESHOLD  [Deprecated] resync if error exceeds this number of frames. "
         "Set to 0 to "
         "stop resyncing.\n");
  printf("    -t, --timeout=SECONDS   go back to idle mode from play mode after a break in "
         "communications of this many seconds (default 120). Set to 0 never to exit play mode.\n");
  printf("    --statistics            print some interesting statistics -- output to the logfile "
         "if running as a daemon.\n");
  printf("    --tolerance=TOLERANCE   [Deprecated] allow a synchronization error of TOLERANCE "
         "frames (default "
         "88) before trying to correct it.\n");
  printf("    --password=PASSWORD     require PASSWORD to connect. Default is not to require a "
         "password.\n");
  printf("    --logOutputLevel        log the output level setting -- useful for setting maximum "
         "volume.\n");
#ifdef CONFIG_METADATA
  printf("    -M, --metadata-enable   ask for metadata from the source and process it.\n");
  printf("    --metadata-pipename=PIPE send metadata to PIPE, e.g. "
         "--metadata-pipename=/tmp/shairport-sync-metadata.\n");
  printf("                            The default is /tmp/shairport-sync-metadata.\n");
  printf("    --get-coverart          send cover art through the metadata pipe.\n");
#endif
  printf("    -u, --use-stderr        log messages through STDERR rather than syslog.\n");
  printf("\n");
  mdns_ls_backends();
  printf("\n");
  audio_ls_outputs();
}

int parse_options(int argc, char **argv) {
  // there are potential memory leaks here -- it's called a second time, previously allocated
  // strings will dangle.
  char *raw_service_name = NULL; /* Used to pick up the service name before possibly expanding it */
  char *stuffing = NULL;         /* used for picking up the stuffing option */
  signed char c;                 /* used for argument parsing */
  // int i = 0;                     /* used for tracking options */
  int fResyncthreshold = (int)(config.resyncthreshold * 44100);
  int fTolerance = (int)(config.tolerance * 44100);
  poptContext optCon; /* context for parsing command-line options */
  struct poptOption optionsTable[] = {
      {"verbose", 'v', POPT_ARG_NONE, NULL, 'v', NULL, NULL},
      {"kill", 'k', POPT_ARG_NONE, &killOption, 0, NULL, NULL},
      {"daemon", 'd', POPT_ARG_NONE, &daemonisewith, 0, NULL, NULL},
      {"justDaemoniseNoPIDFile", 'j', POPT_ARG_NONE, &daemonisewithout, 0, NULL, NULL},
      {"configfile", 'c', POPT_ARG_STRING, &config.configfile, 0, NULL, NULL},
      {"statistics", 0, POPT_ARG_NONE, &config.statistics_requested, 0, NULL, NULL},
      {"logOutputLevel", 0, POPT_ARG_NONE, &config.logOutputLevel, 0, NULL, NULL},
      {"version", 'V', POPT_ARG_NONE, NULL, 0, NULL, NULL},
      {"port", 'p', POPT_ARG_INT, &config.port, 0, NULL, NULL},
      {"name", 'a', POPT_ARG_STRING, &raw_service_name, 0, NULL, NULL},
      {"output", 'o', POPT_ARG_STRING, &config.output_name, 0, NULL, NULL},
      {"on-start", 'B', POPT_ARG_STRING, &config.cmd_start, 0, NULL, NULL},
      {"on-stop", 'E', POPT_ARG_STRING, &config.cmd_stop, 0, NULL, NULL},
      {"wait-cmd", 'w', POPT_ARG_NONE, &config.cmd_blocking, 0, NULL, NULL},
      {"mdns", 'm', POPT_ARG_STRING, &config.mdns_name, 0, NULL, NULL},
      {"latency", 'L', POPT_ARG_INT, &config.userSuppliedLatency, 0, NULL, NULL},
      {"stuffing", 'S', POPT_ARG_STRING, &stuffing, 'S', NULL, NULL},
      {"resync", 'r', POPT_ARG_INT, &fResyncthreshold, 0, NULL, NULL},
      {"timeout", 't', POPT_ARG_INT, &config.timeout, 't', NULL, NULL},
      {"password", 0, POPT_ARG_STRING, &config.password, 0, NULL, NULL},
      {"tolerance", 'z', POPT_ARG_INT, &fTolerance, 0, NULL, NULL},
      {"use-stderr", 'u', POPT_ARG_NONE, NULL, 'u', NULL, NULL},
#ifdef CONFIG_METADATA
      {"metadata-enable", 'M', POPT_ARG_NONE, &config.metadata_enabled, 'M', NULL, NULL},
      {"metadata-pipename", 0, POPT_ARG_STRING, &config.metadata_pipename, 0, NULL, NULL},
      {"get-coverart", 'g', POPT_ARG_NONE, &config.get_coverart, 'g', NULL, NULL},
#endif
      POPT_AUTOHELP{NULL, 0, 0, NULL, 0, NULL, NULL}};

  // we have to parse the command line arguments to look for a config file
  int optind;
  optind = argc;
  int j;
  for (j = 0; j < argc; j++)
    if (strcmp(argv[j], "--") == 0)
      optind = j;

  optCon = poptGetContext(NULL, optind, (const char **)argv, optionsTable, 0);
  if (optCon == NULL)
    die("Can not get a secondary popt context.");
  poptSetOtherOptionHelp(optCon, "[OPTIONS]* ");

  /* Now do options processing just to get a debug level */
  debuglev = 0;
  while ((c = poptGetNextOpt(optCon)) >= 0) {
    switch (c) {
    case 'v':
      debuglev++;
      break;
    case 'u':
      log_to_stderr();
      break;
    case 'D':
      inform("Warning: the option -D or --disconnectFromOutput is deprecated.");
      break;
    case 'R':
      inform("Warning: the option -R or --reconnectToOutput is deprecated.");
      break;
    case 'A':
      inform("Warning: the option -A or --AirPlayLatency is deprecated and ignored. This setting "
             "is now "
             "automatically received from the AirPlay device.");
      break;
    case 'i':
      inform("Warning: the option -i or --iTunesLatency is deprecated and ignored. This setting is "
             "now "
             "automatically received from iTunes");
      break;
    case 'f':
      inform(
          "Warning: the option --forkedDaapdLatency is deprecated and ignored. This setting is now "
          "automatically received from forkedDaapd");
      break;
    case 'r':
      inform("Warning: the option -r or --resync is deprecated. Please use the "
             "\"resync_threshold_in_seconds\" setting in the config file instead.");
      break;
    case 'z':
      inform("Warning: the option --tolerance is deprecated. Please use the "
             "\"drift_tolerance_in_seconds\" setting in the config file instead.");
      break;
    }
  }
  if (c < -1) {
    die("%s: %s", poptBadOption(optCon, POPT_BADOPTION_NOALIAS), poptStrerror(c));
  }

  poptFreeContext(optCon);

#ifdef CONFIG_LIBDAEMON
  if ((daemonisewith) && (daemonisewithout))
    die("Select either daemonize_with_pid_file or daemonize_without_pid_file -- you have selected "
        "both!");
  if ((daemonisewith) || (daemonisewithout)) {
    config.daemonise = 1;
    if (daemonisewith)
      config.daemonise_store_pid = 1;
  };
#endif

  config.resyncthreshold = 1.0 * fResyncthreshold / 44100;
  config.tolerance = 1.0 * fTolerance / 44100;
  config.audio_backend_silent_lead_in_time = -1.0; // flag to indicate it has not been set
  config.airplay_volume = -18.0; // if no volume is ever set, default to initial default value if
                                 // nothing else comes in first.
  config.fixedLatencyOffset = 11025; // this sounds like it works properly.
  config.diagnostic_drop_packet_fraction = 0.0;
  config.active_state_timeout = 10.0;
  config.soxr_delay_threshold = 30; // the soxr measurement time (milliseconds) of two oneshots must not exceed this if soxr interpolation is to be chosen automatically.
  config.volume_range_hw_priority = 0; // if combining software and hardware volume control, give the software priority
// i.e. when reducing volume, reduce the sw first before reducing the software.
// this is because some hw mixers mute at the bottom of their range, and they don't always advertise this fact

#ifdef CONFIG_METADATA_HUB
  config.cover_art_cache_dir = "/tmp/shairport-sync/.cache/coverart";
  config.scan_interval_when_active =
      1; // number of seconds between DACP server scans when playing something
  config.scan_interval_when_inactive =
      3; // number of seconds between DACP server scans playing nothing
  config.scan_max_bad_response_count =
      5; // number of successive bad results to ignore before giving up
  config.scan_max_inactive_count =
      (15 * 60) / config.scan_interval_when_inactive; // number of scans to do before stopping if
                                                      // not made active again (15 minutes)
//  config.scan_max_inactive_count = 5; // number of scans to do before stopping if not made active
//  again (15 minutes )
#endif

  // config_setting_t *setting;
  const char *str = 0;
  int value = 0;
  double dvalue = 0.0;

  // debug(1, "Looking for the configuration file \"%s\".", config.configfile);

  config_init(&config_file_stuff);

  char *config_file_real_path = realpath(config.configfile, NULL);
  if (config_file_real_path == NULL) {
    debug(2, "Can't resolve the configuration file \"%s\".", config.configfile);
  } else {
    debug(2, "Looking for configuration file at full path \"%s\"", config_file_real_path);
    /* Read the file. If there is an error, report it and exit. */
    if (config_read_file(&config_file_stuff, config_file_real_path)) {
      free(config_file_real_path);
      config_set_auto_convert(&config_file_stuff,
                              1); // allow autoconversion from int/float to int/float
      // make config.cfg point to it
      config.cfg = &config_file_stuff;
      /* Get the Service Name. */
      if (config_lookup_string(config.cfg, "general.name", &str)) {
        raw_service_name = strdup(str);
      }
#ifdef CONFIG_LIBDAEMON
      /* Get the Daemonize setting. */
      config_set_lookup_bool(config.cfg, "sessioncontrol.daemonize_with_pid_file", &daemonisewith);

      /* Get the Just_Daemonize setting. */
      config_set_lookup_bool(config.cfg, "sessioncontrol.daemonize_without_pid_file",
                             &daemonisewithout);

      /* Get the directory path for the pid file created when the program is daemonised. */
      if (config_lookup_string(config.cfg, "sessioncontrol.daemon_pid_dir", &str))
        config.piddir = (char *)str;
#endif

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

      /* Get the udp port range setting. This is number of ports that will be tried for free ports ,
       * starting at the port base. Only three ports are needed. */
      if (config_lookup_int(config.cfg, "general.udp_port_range", &value)) {
        if ((value < 3) || (value > 65535))
          die("Invalid port range  \"%sd\". It should be between 3 and 65535, default is 10",
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
        else if (strcasecmp(str, "auto") == 0)
          config.packet_stuffing = ST_auto;
        else if (strcasecmp(str, "soxr") == 0)
#ifdef CONFIG_SOXR
          config.packet_stuffing = ST_soxr;
#else
          warn("The soxr option not available because this version of shairport-sync was built "
              "without libsoxr "
              "support. Change the \"general/interpolation\" setting in the configuration file.");
#endif
        else
          die("Invalid interpolation option choice. It should be \"auto\", \"basic\" or \"soxr\"");
      }
      
#ifdef CONFIG_SOXR
      /* Get the soxr_delay_threshold setting. */
      if (config_lookup_int(config.cfg, "general.soxr_delay_threshold", &value)) {
        if ((value >= 0) && (value <= 100))
          config.soxr_delay_threshold = value;
        else
          warn("Invalid general soxr_delay_threshold setting option choice \"%d\". It should be "
              "between 0 and 100, "
              "inclusive. Default is %d (milliseconds).",
              value, config.soxr_delay_threshold);
      }
#endif

      /* Get the statistics setting. */
      if (config_set_lookup_bool(config.cfg, "general.statistics",
                                 &(config.statistics_requested))) {
        warn("The \"general\" \"statistics\" setting is deprecated. Please use the \"diagnostics\" "
             "\"statistics\" setting instead.");
      }

      /* The old drift tolerance setting. */
      if (config_lookup_int(config.cfg, "general.drift", &value)) {
        inform("The drift setting is deprecated. Use "
               "drift_tolerance_in_seconds instead");
        config.tolerance = 1.0 * value / 44100;
      }

      /* The old resync setting. */
      if (config_lookup_int(config.cfg, "general.resync_threshold", &value)) {
        inform("The resync_threshold setting is deprecated. Use "
               "resync_threshold_in_seconds instead");
        config.resyncthreshold = 1.0 * value / 44100;
      }

      /* Get the drift tolerance setting. */
      if (config_lookup_float(config.cfg, "general.drift_tolerance_in_seconds", &dvalue))
        config.tolerance = dvalue;

      /* Get the resync setting. */
      if (config_lookup_float(config.cfg, "general.resync_threshold_in_seconds", &dvalue))
        config.resyncthreshold = dvalue;

      /* Get the verbosity setting. */
      if (config_lookup_int(config.cfg, "general.log_verbosity", &value)) {
        warn("The \"general\" \"log_verbosity\" setting is deprecated. Please use the "
             "\"diagnostics\" \"log_verbosity\" setting instead.");
        if ((value >= 0) && (value <= 3))
          debuglev = value;
        else
          die("Invalid log verbosity setting option choice \"%d\". It should be between 0 and 3, "
              "inclusive.",
              value);
      }

      /* Get the verbosity setting. */
      if (config_lookup_int(config.cfg, "diagnostics.log_verbosity", &value)) {
        if ((value >= 0) && (value <= 3))
          debuglev = value;
        else
          die("Invalid diagnostics log_verbosity setting option choice \"%d\". It should be "
              "between 0 and 3, "
              "inclusive.",
              value);
      }

      /* Get the show elapsed time in debug messages setting. */
      if (config_lookup_string(config.cfg, "diagnostics.log_show_time_since_startup", &str)) {
        if (strcasecmp(str, "no") == 0)
          config.debugger_show_elapsed_time = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.debugger_show_elapsed_time = 1;
        else
          die("Invalid diagnostics log_show_time_since_startup option choice \"%s\". It should be "
              "\"yes\" or \"no\"");
      }

      /* Get the show relative time in debug messages setting. */
      if (config_lookup_string(config.cfg, "diagnostics.log_show_time_since_last_message", &str)) {
        if (strcasecmp(str, "no") == 0)
          config.debugger_show_relative_time = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.debugger_show_relative_time = 1;
        else
          die("Invalid diagnostics log_show_time_since_last_message option choice \"%s\". It "
              "should be \"yes\" or \"no\"");
      }

      /* Get the statistics setting. */
      if (config_lookup_string(config.cfg, "diagnostics.statistics", &str)) {
        if (strcasecmp(str, "no") == 0)
          config.statistics_requested = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.statistics_requested = 1;
        else
          die("Invalid diagnostics statistics option choice \"%s\". It should be \"yes\" or "
              "\"no\"");
      }

      /* Get the disable_resend_requests setting. */
      if (config_lookup_string(config.cfg, "diagnostics.disable_resend_requests", &str)) {
        config.disable_resend_requests = 0; // this is for legacy -- only set by -t 0
        if (strcasecmp(str, "no") == 0)
          config.disable_resend_requests = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.disable_resend_requests = 1;
        else
          die("Invalid diagnostic disable_resend_requests option choice \"%s\". It should be "
              "\"yes\" "
              "or \"no\"");
      }

      /* Get the drop packets setting. */
      if (config_lookup_float(config.cfg, "diagnostics.drop_this_fraction_of_audio_packets",
                              &dvalue)) {
        if ((dvalue >= 0.0) && (dvalue <= 3.0))
          config.diagnostic_drop_packet_fraction = dvalue;
        else
          die("Invalid diagnostics drop_this_fraction_of_audio_packets setting \"%d\". It should "
              "be "
              "between 0.0 and 1.0, "
              "inclusive.",
              dvalue);
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

      /* Get the optional volume_max_db setting. */
      if (config_lookup_float(config.cfg, "general.volume_max_db", &dvalue)) {
        // debug(1, "Max volume setting of %f dB", dvalue);
        config.volume_max_db = dvalue;
        config.volume_max_db_set = 1;
      }

      if (config_lookup_string(config.cfg, "general.run_this_when_volume_is_set", &str)) {
        config.cmd_set_volume = (char *)str;
      }

      /* Get the playback_mode setting */
      if (config_lookup_string(config.cfg, "general.playback_mode", &str)) {
        if (strcasecmp(str, "stereo") == 0)
          config.playback_mode = ST_stereo;
        else if (strcasecmp(str, "mono") == 0)
          config.playback_mode = ST_mono;
        else if (strcasecmp(str, "reverse stereo") == 0)
          config.playback_mode = ST_reverse_stereo;
        else if (strcasecmp(str, "both left") == 0)
          config.playback_mode = ST_left_only;
        else if (strcasecmp(str, "both right") == 0)
          config.playback_mode = ST_right_only;
        else
          die("Invalid playback_mode choice \"%s\". It should be \"stereo\" (default), \"mono\", "
              "\"reverse stereo\", \"both left\", \"both right\"");
      }

      /* Get the volume control profile setting -- "standard" or "flat" */
      if (config_lookup_string(config.cfg, "general.volume_control_profile", &str)) {
        if (strcasecmp(str, "standard") == 0)
          config.volume_control_profile = VCP_standard;
        else if (strcasecmp(str, "flat") == 0)
          config.volume_control_profile = VCP_flat;
        else
          die("Invalid volume_control_profile choice \"%s\". It should be \"standard\" (default) "
              "or \"flat\"");
      }

      config_set_lookup_bool(config.cfg, "general.volume_control_combined_hardware_priority",
                             &config.volume_range_hw_priority);

      /* Get the interface to listen on, if specified Default is all interfaces */
      /* we keep the interface name and the index */

      if (config_lookup_string(config.cfg, "general.interface", &str))
        config.interface = strdup(str);

      if (config_lookup_string(config.cfg, "general.interface", &str)) {
        int specified_interface_found = 0;

        struct if_nameindex *if_ni, *i;

        if_ni = if_nameindex();
        if (if_ni == NULL) {
          debug(1, "Can't get a list of interface names.");
        } else {
          for (i = if_ni; !(i->if_index == 0 && i->if_name == NULL); i++) {
            // printf("%u: %s\n", i->if_index, i->if_name);
            if (strcmp(i->if_name, str) == 0) {
              config.interface_index = i->if_index;
              specified_interface_found = 1;
            }
          }
        }

        if_freenameindex(if_ni);

        if (specified_interface_found == 0) {
          inform(
              "The mdns service interface \"%s\" was not found, so the setting has been ignored.",
              config.interface);
          free(config.interface);
          config.interface = NULL;
          config.interface_index = 0;
        }
      }

      /* Get the regtype -- the service type and protocol, separated by a dot. Default is
       * "_raop._tcp" */
      if (config_lookup_string(config.cfg, "general.regtype", &str))
        config.regtype = strdup(str);

      /* Get the volume range, in dB, that should be used If not set, it means you just use the
       * range set by the mixer. */
      if (config_lookup_int(config.cfg, "general.volume_range_db", &value)) {
        if ((value < 30) || (value > 150))
          die("Invalid volume range  \"%sd\". It should be between 30 and 150 dB. Zero means use "
              "the mixer's native range",
              value);
        else
          config.volume_range_db = value;
      }

      /* Get the alac_decoder setting. */
      if (config_lookup_string(config.cfg, "general.alac_decoder", &str)) {
        if (strcasecmp(str, "hammerton") == 0)
          config.use_apple_decoder = 0;
        else if (strcasecmp(str, "apple") == 0) {
          if ((config.decoders_supported & 1 << decoder_apple_alac) != 0)
            config.use_apple_decoder = 1;
          else
            inform("Support for the Apple ALAC decoder has not been compiled into this version of "
                   "Shairport Sync. The default decoder will be used.");
        } else
          die("Invalid alac_decoder option choice \"%s\". It should be \"hammerton\" or \"apple\"");
      }

      /* Get the default latency. Deprecated! */
      if (config_lookup_int(config.cfg, "latencies.default", &value))
        config.userSuppliedLatency = value;

#ifdef CONFIG_METADATA
      /* Get the metadata setting. */
      config.metadata_enabled = 1; // if metadata support is included, then enable it by default
      if (config_lookup_string(config.cfg, "metadata.enabled", &str)) {
        if (strcasecmp(str, "no") == 0)
          config.metadata_enabled = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.metadata_enabled = 1;
        else
          die("Invalid metadata enabled option choice \"%s\". It should be \"yes\" or \"no\"");
      }

      config.get_coverart = 1; // if metadata support is included, then enable it by default
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

      if (config_lookup_string(config.cfg, "metadata.socket_address", &str)) {
        config.metadata_sockaddr = (char *)str;
      }
      if (config_lookup_int(config.cfg, "metadata.socket_port", &value)) {
        config.metadata_sockport = value;
      }
      config.metadata_sockmsglength = 500;
      if (config_lookup_int(config.cfg, "metadata.socket_msglength", &value)) {
        config.metadata_sockmsglength = value < 500 ? 500 : value > 65000 ? 65000 : value;
      }

#endif

      if (config_lookup_string(config.cfg, "sessioncontrol.run_this_before_play_begins", &str)) {
        config.cmd_start = (char *)str;
      }

      if (config_lookup_string(config.cfg, "sessioncontrol.run_this_after_play_ends", &str)) {
        config.cmd_stop = (char *)str;
      }

      if (config_lookup_string(config.cfg, "sessioncontrol.run_this_before_entering_active_state",
                               &str)) {
        config.cmd_active_start = (char *)str;
      }

      if (config_lookup_string(config.cfg, "sessioncontrol.run_this_after_exiting_active_state",
                               &str)) {
        config.cmd_active_stop = (char *)str;
      }

      if (config_lookup_float(config.cfg, "sessioncontrol.active_state_timeout", &dvalue)) {
        if (dvalue < 0.0)
          warn("Invalid value \"%f\" for sessioncontrol.active_state_timeout. It must be positive. "
               "The default of %f will be used instead.",
               dvalue, config.active_state_timeout);
        else
          config.active_state_timeout = dvalue;
      }

      if (config_lookup_string(config.cfg,
                               "sessioncontrol.run_this_if_an_unfixable_error_is_detected", &str)) {
        config.cmd_unfixable = (char *)str;
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

      if (config_lookup_string(config.cfg, "sessioncontrol.before_play_begins_returns_output",
                               &str)) {
        if (strcasecmp(str, "no") == 0)
          config.cmd_start_returns_output = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.cmd_start_returns_output = 1;
        else
          die("Invalid session control before_play_begins_returns_output option choice \"%s\". It "
              "should be "
              "\"yes\" or \"no\"");
      }

      if (config_lookup_string(config.cfg, "sessioncontrol.allow_session_interruption", &str)) {
        config.dont_check_timeout = 0; // this is for legacy -- only set by -t 0
        if (strcasecmp(str, "no") == 0)
          config.allow_session_interruption = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.allow_session_interruption = 1;
        else
          die("Invalid session control allow_interruption option choice \"%s\". It should be "
              "\"yes\" "
              "or \"no\"");
      }

      if (config_lookup_int(config.cfg, "sessioncontrol.session_timeout", &value)) {
        config.timeout = value;
        config.dont_check_timeout = 0; // this is for legacy -- only set by -t 0
      }

#ifdef CONFIG_CONVOLUTION
      if (config_lookup_string(config.cfg, "dsp.convolution", &str)) {
        if (strcasecmp(str, "no") == 0)
          config.convolution = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.convolution = 1;
        else
          die("Invalid dsp.convolution. It should be \"yes\" or \"no\"");
      }

      if (config_lookup_float(config.cfg, "dsp.convolution_gain", &dvalue)) {
        config.convolution_gain = dvalue;
        if (dvalue > 10 || dvalue < -50)
          die("Invalid value \"%f\" for dsp.convolution_gain. It should be between -50 and +10 dB",
              dvalue);
      }

      config.convolution_max_length = 8192;
      if (config_lookup_int(config.cfg, "dsp.convolution_max_length", &value)) {
        config.convolution_max_length = value;

        if (value < 1 || value > 200000)
          die("dsp.convolution_max_length must be within 1 and 200000");
      }

      if (config_lookup_string(config.cfg, "dsp.convolution_ir_file", &str)) {
        config.convolution_ir_file = str;
        convolver_init(config.convolution_ir_file, config.convolution_max_length);
      }

      if (config.convolution && config.convolution_ir_file == NULL) {
        die("Convolution enabled but no convolution_ir_file provided");
      }
#endif
      if (config_lookup_string(config.cfg, "dsp.loudness", &str)) {
        if (strcasecmp(str, "no") == 0)
          config.loudness = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.loudness = 1;
        else
          die("Invalid dsp.convolution. It should be \"yes\" or \"no\"");
      }

      config.loudness_reference_volume_db = -20;
      if (config_lookup_float(config.cfg, "dsp.loudness_reference_volume_db", &dvalue)) {
        config.loudness_reference_volume_db = dvalue;
        if (dvalue > 0 || dvalue < -100)
          die("Invalid value \"%f\" for dsp.loudness_reference_volume_db. It should be between "
              "-100 and 0",
              dvalue);
      }

      if (config.loudness == 1 && config_lookup_string(config.cfg, "alsa.mixer_control_name", &str))
        die("Loudness activated but hardware volume is active. You must remove "
            "\"alsa.mixer_control_name\" to use the loudness filter.");

    } else {
      if (config_error_type(&config_file_stuff) == CONFIG_ERR_FILE_IO)
        debug(2, "Error reading configuration file \"%s\": \"%s\".",
              config_error_file(&config_file_stuff), config_error_text(&config_file_stuff));
      else {
        die("Line %d of the configuration file \"%s\":\n%s", config_error_line(&config_file_stuff),
            config_error_file(&config_file_stuff), config_error_text(&config_file_stuff));
      }
    }
#if defined(CONFIG_DBUS_INTERFACE)
    /* Get the dbus service sbus setting. */
    if (config_lookup_string(config.cfg, "general.dbus_service_bus", &str)) {
      if (strcasecmp(str, "system") == 0)
        config.dbus_service_bus_type = DBT_system;
      else if (strcasecmp(str, "session") == 0)
        config.dbus_service_bus_type = DBT_session;
      else
        die("Invalid dbus_service_bus option choice \"%s\". It should be \"system\" (default) or "
            "\"session\"");
    }
#endif

#if defined(CONFIG_MPRIS_INTERFACE)
    /* Get the mpris service sbus setting. */
    if (config_lookup_string(config.cfg, "general.mpris_service_bus", &str)) {
      if (strcasecmp(str, "system") == 0)
        config.mpris_service_bus_type = DBT_system;
      else if (strcasecmp(str, "session") == 0)
        config.mpris_service_bus_type = DBT_session;
      else
        die("Invalid mpris_service_bus option choice \"%s\". It should be \"system\" (default) or "
            "\"session\"");
    }
#endif

#ifdef CONFIG_MQTT
    config_set_lookup_bool(config.cfg, "mqtt.enabled", &config.mqtt_enabled);
    if (config.mqtt_enabled && !config.metadata_enabled) {
      die("You need to have metadata enabled in order to use mqtt");
    }
    if (config_lookup_string(config.cfg, "mqtt.hostname", &str)) {
      config.mqtt_hostname = (char *)str;
      // TODO: Document that, if this is false, whole mqtt func is disabled
    }
    config.mqtt_port = 1883;
    if (config_lookup_int(config.cfg, "mqtt.port", &value)) {
      if ((value < 0) || (value > 65535))
          die("Invalid mqtt port number  \"%sd\". It should be between 0 and 65535, default is 1883",
              value);
      else
          config.mqtt_port = value;
    }

    if (config_lookup_string(config.cfg, "mqtt.username", &str)) {
      config.mqtt_username = (char *)str;
    }
    if (config_lookup_string(config.cfg, "mqtt.password", &str)) {
      config.mqtt_password = (char *)str;
    }
    int capath = 0;
    if (config_lookup_string(config.cfg, "mqtt.capath", &str)) {
      config.mqtt_capath = (char *)str;
      capath = 1;
    }
    if (config_lookup_string(config.cfg, "mqtt.cafile", &str)) {
      if (capath)
        die("Supply either mqtt cafile or mqtt capath -- you have supplied both!");
      config.mqtt_cafile = (char *)str;
    }
    int certkeynum = 0;
    if (config_lookup_string(config.cfg, "mqtt.certfile", &str)) {
      config.mqtt_certfile = (char *)str;
      certkeynum++;
    }
    if (config_lookup_string(config.cfg, "mqtt.keyfile", &str)) {
      config.mqtt_keyfile = (char *)str;
      certkeynum++;
    }
    if (certkeynum != 0 && certkeynum != 2) {
      die("If you want to use TLS Client Authentication, you have to specify "
          "mqtt.certfile AND mqtt.keyfile.\nYou have supplied only one of them.\n"
          "If you do not want to use TLS Client Authentication, leave both empty.");
    }

    if (config_lookup_string(config.cfg, "mqtt.topic", &str)) {
      config.mqtt_topic = (char *)str;
    }
    config_set_lookup_bool(config.cfg, "mqtt.publish_raw", &config.mqtt_publish_raw);
    config_set_lookup_bool(config.cfg, "mqtt.publish_parsed", &config.mqtt_publish_parsed);
    config_set_lookup_bool(config.cfg, "mqtt.publish_cover", &config.mqtt_publish_cover);
    if (config.mqtt_publish_cover && !config.get_coverart) {
      die("You need to have metadata.include_cover_art enabled in order to use mqtt.publish_cover");
    }
    config_set_lookup_bool(config.cfg, "mqtt.enable_remote", &config.mqtt_enable_remote);
#ifndef CONFIG_AVAHI
        if (config.mqtt_enable_remote) {
          die("You have enabled MQTT remote control which requires shairport-sync to be built with "
              "Avahi, but your installation is not using avahi. Please reinstall/recompile with "
              "avahi enabled, or disable remote control.");
        }
#endif
#endif
  }

  // now, do the command line options again, but this time do them fully -- it's a unix convention
  // that command line
  // arguments have precedence over configuration file settings.

  optind = argc;
  for (j = 0; j < argc; j++)
    if (strcmp(argv[j], "--") == 0)
      optind = j;

  optCon = poptGetContext(NULL, optind, (const char **)argv, optionsTable, 0);
  if (optCon == NULL)
    die("Can not get a popt context.");
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
      else if (strcmp(stuffing, "auto") == 0)
        config.packet_stuffing = ST_auto;
      else if (strcmp(stuffing, "soxr") == 0)
#ifdef CONFIG_SOXR
        config.packet_stuffing = ST_soxr;
#else
        die("The soxr option not available because this version of shairport-sync was built "
            "without libsoxr "
            "support. Change the -S option setting.");
#endif
      else
        die("Illegal stuffing option \"%s\" -- must be \"basic\" or \"soxr\"", stuffing);
      break;
    }
  }
  if (c < -1) {
    die("%s: %s", poptBadOption(optCon, POPT_BADOPTION_NOALIAS), poptStrerror(c));
  }

  poptFreeContext(optCon);

// here, we are finally finished reading the options


#ifdef CONFIG_LIBDAEMON
  if ((daemonisewith) && (daemonisewithout))
    die("Select either daemonize_with_pid_file or daemonize_without_pid_file -- you have selected "
        "both!");
  if ((daemonisewith) || (daemonisewithout)) {
    config.daemonise = 1;
    if (daemonisewith)
      config.daemonise_store_pid = 1;
  };
#else
  /* Check if we are called with -d or --daemon or -j or justDaemoniseNoPIDFile options*/
  if ((daemonisewith != 0) || (daemonisewithout != 0)) {
    fprintf(stderr,"%s was built without libdaemon, so does not support daemonisation using the -d, --deamon, -j or --justDaemoniseNoPIDFile options\n",config.appName);
    exit(EXIT_FAILURE);
  }

#endif

#ifdef CONFIG_METADATA
  if ((config.metadata_enabled == 1) && (config.metadata_pipename == NULL))
    config.metadata_pipename = strdup("/tmp/shairport-sync-metadata");
#endif

  /* if the regtype hasn't been set, do it now */
  if (config.regtype == NULL)
    config.regtype = strdup("_raop._tcp");

  if (tdebuglev != 0)
    debuglev = tdebuglev;

  /* if the Service Name wasn't specified, do it now */

  if (raw_service_name == NULL)
    raw_service_name = strdup("%H");

  // now, do the substitutions in the service name
  char hostname[100];
  gethostname(hostname, 100);
  char *i1 = str_replace(raw_service_name, "%h", hostname);
  if (raw_service_name) {
    free(raw_service_name);
    raw_service_name = NULL;
  }
  if ((hostname[0] >= 'a') && (hostname[0] <= 'z'))
    hostname[0] = hostname[0] - 0x20; // convert a lowercase first letter into a capital letter
  char *i2 = str_replace(i1, "%H", hostname);
  char *i3 = str_replace(i2, "%v", PACKAGE_VERSION);
  char *vs = get_version_string();
  config.service_name = str_replace(i3, "%V", vs);
  free(i1);
  free(i2);
  free(i3);
  free(vs);

#ifdef CONFIG_MQTT
  // mqtt topic was not set. As we have the service name just now, set it
  if (config.mqtt_topic == NULL) {
    int topic_length = 1 + strlen(config.service_name) + 1;
    char *topic = malloc(topic_length + 1);
    snprintf(topic, topic_length, "/%s/", config.service_name);
    config.mqtt_topic = topic;
  }
#endif

#ifdef CONFIG_LIBDAEMON

// now, check and calculate the pid directory
#ifdef DEFINED_CUSTOM_PID_DIR
  char *use_this_pid_dir = PIDDIR;
#else
  char *use_this_pid_dir = "/var/run/shairport-sync";
#endif
  // debug(1,"config.piddir \"%s\".",config.piddir);
  if (config.piddir)
    use_this_pid_dir = config.piddir;
  if (use_this_pid_dir)
    config.computed_piddir = strdup(use_this_pid_dir);
#endif
  return optind + 1;
}

#if defined(CONFIG_DBUS_INTERFACE) || defined(CONFIG_MPRIS_INTERFACE)
static GMainLoop *g_main_loop = NULL;

pthread_t dbus_thread;
void *dbus_thread_func(__attribute__((unused)) void *arg) {
  g_main_loop = g_main_loop_new(NULL, FALSE);
  g_main_loop_run(g_main_loop);
  debug(2, "g_main_loop thread exit");
  pthread_exit(NULL);
}
#endif

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

  // SIGUSR1 is used to interrupt a thread if blocked in pselect
  pthread_sigmask(SIG_SETMASK, NULL, &pselect_sigset);
  sigdelset(&pselect_sigset, SIGUSR1);

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

#ifdef CONFIG_LIBDAEMON
char pid_file_path_string[4096] = "\0";

const char *pid_file_proc(void) {
  snprintf(pid_file_path_string, sizeof(pid_file_path_string), "%s/%s.pid", config.computed_piddir,
           daemon_pid_file_ident ? daemon_pid_file_ident : "unknown");
  // debug(1,"pid_file_path_string \"%s\".",pid_file_path_string);
  return pid_file_path_string;
}
#endif

void main_cleanup_handler(__attribute__((unused)) void *arg) {
  // it doesn't look like this is called when the main function is cancelled eith a pthread cancel.
  debug(2, "main cleanup handler called.");
#ifdef CONFIG_MQTT
  if (config.mqtt_enabled) {
    // terminate_mqtt();
  }
#endif

#if defined(CONFIG_DBUS_INTERFACE) || defined(CONFIG_MPRIS_INTERFACE)
#ifdef CONFIG_MPRIS_INTERFACE
// stop_mpris_service();
#endif
#ifdef CONFIG_DBUS_INTERFACE
  stop_dbus_service();
#endif
  if (g_main_loop) {
    debug(2, "Stopping DBUS Loop Thread");
    g_main_loop_quit(g_main_loop);
    pthread_join(dbus_thread, NULL);
  }
#endif

#ifdef CONFIG_DACP_CLIENT
  debug(2, "Stopping DACP Monitor");
  dacp_monitor_stop();
#endif

#ifdef CONFIG_METADATA_HUB
  debug(2, "Stopping metadata hub");
  metadata_hub_stop();
#endif

#ifdef CONFIG_METADATA
  metadata_stop(); // close down the metadata pipe
#endif

  activity_monitor_stop(0);

  if ((config.output) && (config.output->deinit)) {
    debug(2, "Deinitialise the audio backend.");
    config.output->deinit();
  }
  
#ifdef CONFIG_LIBDAEMON
// only do this if you are the daemon
  if (pid == 0) {
    daemon_retval_send(0);
    daemon_pid_file_remove();
    daemon_signal_done();
  }  
#endif
 
  debug(2, "Exit...");
  exit(EXIT_SUCCESS);
}

void exit_function() {
  debug(2, "exit function called...");
  main_cleanup_handler(NULL);
  if (conns)
    free(conns); // make sure the connections have been deleted first
  if (config.service_name)
    free(config.service_name);
  if (config.regtype)
    free(config.regtype);
#ifdef CONFIG_LIBDAEMON
  if (config.computed_piddir)
    free(config.computed_piddir);
#endif
  if (ranarray)
    free((void *)ranarray);
  if (config.cfg)
    config_destroy(config.cfg);
  if (config.appName)
    free(config.appName);
  // probably should be freeing malloc'ed memory here, including strdup-created strings...
}

int main(int argc, char **argv) {
#ifdef CONFIG_LIBDAEMON
  pid = getpid();
#endif
  conns = NULL; // no connections active
  memset((void *)&main_thread_id, 0, sizeof(main_thread_id));
  memset(&config, 0, sizeof(config)); // also clears all strings, BTW
  fp_time_at_startup = get_absolute_time_in_fp();
  fp_time_at_last_debug_message = fp_time_at_startup;
  // this is a bit weird, but necessary -- basename() may modify the argument passed in
  char *basec = strdup(argv[0]);
  char *bname = basename(basec);
  config.appName = strdup(bname);
  if (config.appName == NULL)
    die("can not allocate memory for the app name!");
  free(basec);

  //  debug(1,"startup");
#ifdef CONFIG_LIBDAEMON
  daemon_set_verbosity(LOG_DEBUG);
#else
  setlogmask (LOG_UPTO (LOG_DEBUG));
  openlog(NULL,0,LOG_DAEMON);
#endif
  atexit(exit_function);

  // set defaults

  // get the endianness
  union {
    uint32_t u32;
    uint8_t arr[4];
  } xn;

  xn.arr[0] = 0x44; /* Lowest-address byte */
  xn.arr[1] = 0x33;
  xn.arr[2] = 0x22;
  xn.arr[3] = 0x11; /* Highest-address byte */

  if (xn.u32 == 0x11223344)
    config.endianness = SS_LITTLE_ENDIAN;
  else if (xn.u32 == 0x33441122)
    config.endianness = SS_PDP_ENDIAN;
  else if (xn.u32 == 0x44332211)
    config.endianness = SS_BIG_ENDIAN;
  else
    die("Can not recognise the endianness of the processor.");

  // set non-zero / non-NULL default values here
  // but note that audio back ends also have a chance to set defaults

  strcpy(configuration_file_path, SYSCONFDIR);
  // strcat(configuration_file_path, "/shairport-sync"); // thinking about adding a special
  // shairport-sync directory
  strcat(configuration_file_path, "/");
  strcat(configuration_file_path, config.appName);
  strcat(configuration_file_path, ".conf");
  config.configfile = configuration_file_path;

  // config.statistics_requested = 0; // don't print stats in the log
  // config.userSuppliedLatency = 0; // zero means none supplied
  
  config.debugger_show_relative_time = 1; // by default, log the  time back to the previous debug message
  config.resyncthreshold = 0.05; // 50 ms
  config.timeout = 120; // this number of seconds to wait for [more] audio before switching to idle.
  config.tolerance =
      0.002; // this number of seconds of timing error before attempting to correct it.
  config.buffer_start_fill = 220;
  config.port = 5000;


#ifdef CONFIG_SOXR
  config.packet_stuffing = ST_auto; // use soxr interpolation by default if support has been included and if the CPU is fast enough
#else
  config.packet_stuffing = ST_basic; // simple interpolation or deletion
#endif


  // char hostname[100];
  // gethostname(hostname, 100);
  // config.service_name = malloc(20 + 100);
  // snprintf(config.service_name, 20 + 100, "Shairport Sync on %s", hostname);
  set_requested_connection_state_to_output(
      1); // we expect to be able to connect to the output device
  config.audio_backend_buffer_desired_length = 6615; // 0.15 seconds.
  config.udp_port_base = 6001;
  config.udp_port_range = 10;
  config.output_format = SPS_FORMAT_S16_LE; // default
  config.output_format_auto_requested = 1;  // default auto select format
  config.output_rate = 44100;               // default
  config.output_rate_auto_requested = 1;    // default auto select format
  config.decoders_supported =
      1 << decoder_hammerton; // David Hammerton's decoder supported by default
#ifdef CONFIG_APPLE_ALAC
  config.decoders_supported += 1 << decoder_apple_alac;
  config.use_apple_decoder = 1; // use the ALAC decoder by default if support has been included
#endif

  // initialise random number generator

  r64init(0);

  // initialise the randomw number array

  r64arrayinit();

  /* Check if we are called with -V or --version parameter */
  if (argc >= 2 && ((strcmp(argv[1], "-V") == 0) || (strcmp(argv[1], "--version") == 0))) {
    print_version();
    exit(EXIT_FAILURE);
  }

  /* Check if we are called with -h or --help parameter */
  if (argc >= 2 && ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0))) {
    usage(argv[0]);
    exit(EXIT_FAILURE);
  }

#ifdef CONFIG_LIBDAEMON

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

  /* Set indentification string for the daemon for both syslog and PID file */
  daemon_pid_file_ident = daemon_log_ident = daemon_ident_from_argv0(argv[0]);

  daemon_pid_file_proc = pid_file_proc;

#endif


  // parse arguments into config -- needed to locate pid_dir
  int audio_arg = parse_options(argc, argv);

  // mDNS supports maximum of 63-character names (we append 13).
  if (strlen(config.service_name) > 50) {
    warn("Supplied name too long (max 50 characters)");
    config.service_name[50] = '\0'; // truncate it and carry on...
  }

  /* Check if we are called with -k or --kill option */
  if (killOption != 0) {  
#ifdef CONFIG_LIBDAEMON
    int ret;

    /* Kill daemon with SIGTERM */
    /* Check if the new function daemon_pid_file_kill_wait() is available, if it is, use it. */
    if ((ret = daemon_pid_file_kill_wait(SIGTERM, 5)) < 0)
      daemon_log(LOG_WARNING, "Failed to kill daemon: %s", strerror(errno));
    else {
      // debug(1,"Successfully killed the shairport sync daemon.");
    }
    return ret < 0 ? 1 : 0;
#else
    fprintf(stderr,"%s was built without libdaemon, so does not support the -k or --kill option\n",config.appName);
    return 1;
#endif
  }

#ifdef CONFIG_LIBDAEMON
  /* If we are going to daemonise, check that the daemon is not running already.*/
  if ((config.daemonise) && ((pid = daemon_pid_file_is_running()) >= 0)) {
    daemon_log(LOG_ERR, "Daemon already running on PID file %u", pid);
    return 1;
  }

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

      switch (ret) {
      case 0:
        break;
      case 1:
        daemon_log(LOG_ERR,
                   "daemon failed to launch: could not close open file descriptors after forking.");
        break;
      case 2:
        daemon_log(LOG_ERR, "daemon failed to launch: could not create PID file.");
        break;
      case 3:
        daemon_log(LOG_ERR, "daemon failed to launch: could not create or access PID directory.");
        break;
      default:
        daemon_log(LOG_ERR, "daemon failed to launch, error %i.", ret);
      }
      return ret;
    } else { /* pid == 0 means we are the daemon */

      /* Close FDs */
      if (daemon_close_all(-1) < 0) {
        daemon_log(LOG_ERR, "Failed to close all file descriptors: %s", strerror(errno));
        /* Send the error condition to the parent process */
        daemon_retval_send(1);

        daemon_signal_done();
        return 0;
      }

      /* Create the PID file if required */
      if (config.daemonise_store_pid) {
        /* Create the PID directory if required -- we don't really care about the result */
        printf("PID directory is \"%s\".", config.computed_piddir);
        int result = mkpath(config.computed_piddir, 0700);
        if ((result != 0) && (result != -EEXIST)) {
          // error creating or accessing the PID file directory
          daemon_retval_send(3);

          daemon_signal_done();
          return 0;
        }
 
        if (daemon_pid_file_create() < 0) {
          daemon_log(LOG_ERR, "Could not create PID file (%s).", strerror(errno));

          daemon_retval_send(2);
          daemon_signal_done();
          return 0;
        }
      }

      /* Send OK to parent process */
      daemon_retval_send(0);
    }
    /* end libdaemon stuff */
  }

#endif
  debug(1,"Started!");
  main_thread_id = pthread_self();
  if (!main_thread_id)
    debug(1, "Main thread is set up to be NULL!");

  signal_setup();

  // make sure the program can create files that group and world can read
  umask(S_IWGRP | S_IWOTH);


  /* print out version */

  char *version_dbs = get_version_string();
  if (version_dbs) {
    debug(1, "software version: \"%s\"", version_dbs);
    free(version_dbs);
  } else {
    debug(1, "can't print the version information!");
  }
  
  debug(1, "log verbosity is %d.", debuglev);

  config.output = audio_get_output(config.output_name);
  if (!config.output) {
    audio_ls_outputs();
    die("Invalid audio output specified!");
  }
  config.output->init(argc - audio_arg, argv + audio_arg);

  pthread_cleanup_push(main_cleanup_handler, NULL);

  // daemon_log(LOG_NOTICE, "startup");

  switch (config.endianness) {
  case SS_LITTLE_ENDIAN:
    debug(2, "The processor is running little-endian.");
    break;
  case SS_BIG_ENDIAN:
    debug(2, "The processor is running big-endian.");
    break;
  case SS_PDP_ENDIAN:
    debug(2, "The processor is running pdp-endian.");
    break;
  }

  /* Mess around with the latency options */
  // Basically, we expect the source to set the latency and add a fixed offset of 11025 frames to
  // it, which sounds right
  // If this latency is outside the max and min latensies that may be set by the source, clamp it to
  // fit.

  // If they specify a non-standard latency, we suggest the user to use the
  // audio_backend_latency_offset instead.

  if (config.userSuppliedLatency) {
    inform("The fixed latency setting is deprecated, as Shairport Sync gets the correct "
           "latency automatically from the source.");
    inform("Use the audio_backend_latency_offset_in_seconds setting "
           "instead to compensate for timing issues.");
    if ((config.userSuppliedLatency != 0) &&
        ((config.userSuppliedLatency < 4410) ||
         (config.userSuppliedLatency > BUFFER_FRAMES * 352 - 22050)))
      die("An out-of-range fixed latency has been specified. It must be between 4410 and %d (at "
          "44100 frames per second).",
          BUFFER_FRAMES * 352 - 22050);
  }

  /* Print out options */
  debug(1, "disable resend requests is %s.", config.disable_resend_requests ? "on" : "off");
  debug(1, "diagnostic_drop_packet_fraction is %f. A value of 0.0 means no packets will be dropped "
           "deliberately.",
        config.diagnostic_drop_packet_fraction);
  debug(1, "statistics_requester status is %d.", config.statistics_requested);
#if CONFIG_LIBDAEMON
  debug(1, "daemon status is %d.", config.daemonise);
  debug(1, "deamon pid file path is \"%s\".", pid_file_proc());
#endif
  debug(1, "rtsp listening port is %d.", config.port);
  debug(1, "udp base port is %d.", config.udp_port_base);
  debug(1, "udp port range is %d.", config.udp_port_range);
  debug(1, "player name is \"%s\".", config.service_name);
  debug(1, "backend is \"%s\".", config.output_name);
  debug(1, "run_this_before_play_begins action is \"%s\".", config.cmd_start);
  debug(1, "run_this_after_play_ends action is \"%s\".", config.cmd_stop);
  debug(1, "wait-cmd status is %d.", config.cmd_blocking);
  debug(1, "run_this_before_play_begins may return output is %d.", config.cmd_start_returns_output);
  debug(1, "run_this_if_an_unfixable_error_is_detected action is \"%s\".", config.cmd_unfixable);
  debug(1, "run_this_before_entering_active_state action is  \"%s\".", config.cmd_active_start);
  debug(1, "run_this_after_exiting_active_state action is  \"%s\".", config.cmd_active_stop);
  debug(1, "active_state_timeout is  %f seconds.", config.active_state_timeout);
  debug(1, "mdns backend \"%s\".", config.mdns_name);
  debug(2, "userSuppliedLatency is %d.", config.userSuppliedLatency);
  debug(1, "interpolation setting is \"%s\".", config.packet_stuffing == ST_basic ? "basic" : config.packet_stuffing == ST_soxr ? "soxr" : "auto");
  debug(1, "interpolation soxr_delay_threshold is %d.", config.soxr_delay_threshold);
  debug(1, "resync time is %f seconds.", config.resyncthreshold);
  debug(1, "allow a session to be interrupted: %d.", config.allow_session_interruption);
  debug(1, "busy timeout time is %d.", config.timeout);
  debug(1, "drift tolerance is %f seconds.", config.tolerance);
  debug(1, "password is \"%s\".", config.password);
  debug(1, "ignore_volume_control is %d.", config.ignore_volume_control);
  if (config.volume_max_db_set)
    debug(1, "volume_max_db is %d.", config.volume_max_db);
  else
    debug(1, "volume_max_db is not set");
  debug(1, "volume range in dB (zero means use the range specified by the mixer): %u.",
        config.volume_range_db);
  debug(
      1,
      "volume_range_combined_hardware_priority (1 means hardware mixer attenuation is used first) is %d.",
      config.volume_range_hw_priority);
  debug(1, "playback_mode is %d (0-stereo, 1-mono, 1-reverse_stereo, 2-both_left, 3-both_right).",
        config.playback_mode);
  debug(1, "disable_synchronization is %d.", config.no_sync);
  debug(1, "use_mmap_if_available is %d.", config.no_mmap ? 0 : 1);
  debug(1, "output_format automatic selection is %sabled.", config.output_format_auto_requested ? "en" : "dis");
  if (config.output_format_auto_requested == 0)
  	debug(1,
        "output_format is \"%s\".",
        sps_format_description_string(config.output_format));
  debug(1, "output_rate automatic selection is %sabled.", config.output_rate_auto_requested ? "en" : "dis");
  if (config.output_rate_auto_requested == 0)
  	debug(1, "output_rate is %d.", config.output_rate);
  debug(1, "audio backend desired buffer length is %f seconds.",
        config.audio_backend_buffer_desired_length);
  debug(1, "audio_backend_buffer_interpolation_threshold_in_seconds is %f seconds.", config.audio_backend_buffer_interpolation_threshold_in_seconds);
  debug(1, "audio backend latency offset is %f seconds.", config.audio_backend_latency_offset);
  debug(1, "audio backend silence lead-in time is %f seconds. A value -1.0 means use the default.",
        config.audio_backend_silent_lead_in_time);
  debug(1, "zeroconf regtype is \"%s\".", config.regtype);
  debug(1, "decoders_supported field is %d.", config.decoders_supported);
  debug(1, "use_apple_decoder is %d.", config.use_apple_decoder);
  debug(1, "alsa_use_hardware_mute is %d.", config.alsa_use_hardware_mute);
  if (config.interface)
    debug(1, "mdns service interface \"%s\" requested.", config.interface);
  else
    debug(1, "no special mdns service interface was requested.");
  char *realConfigPath = realpath(config.configfile, NULL);
  if (realConfigPath) {
    debug(1, "configuration file name \"%s\" resolves to \"%s\".", config.configfile,
          realConfigPath);
    free(realConfigPath);
  } else {
    debug(1, "configuration file name \"%s\" can not be resolved.", config.configfile);
  }
#ifdef CONFIG_METADATA
  debug(1, "metadata enabled is %d.", config.metadata_enabled);
  debug(1, "metadata pipename is \"%s\".", config.metadata_pipename);
  debug(1, "metadata socket address is \"%s\" port %d.", config.metadata_sockaddr,
        config.metadata_sockport);
  debug(1, "metadata socket packet size is \"%d\".", config.metadata_sockmsglength);
  debug(1, "get-coverart is %d.", config.get_coverart);
#endif
#ifdef CONFIG_MQTT
  debug(1, "mqtt is %sabled.", config.mqtt_enabled ? "en" : "dis");
  debug(1, "mqtt hostname is %s, port is %d.", config.mqtt_hostname, config.mqtt_port);
  debug(1, "mqtt topic is %s.", config.mqtt_topic);
  debug(1, "mqtt will%s publish raw metadata.", config.mqtt_publish_raw ? "" : " not");
  debug(1, "mqtt will%s publish parsed metadata.", config.mqtt_publish_parsed ? "" : " not");
  debug(1, "mqtt will%s publish cover Art.", config.mqtt_publish_cover ? "" : " not");
  debug(1, "mqtt remote control is %sabled.", config.mqtt_enable_remote ? "en" : "dis");
#endif

#ifdef CONFIG_CONVOLUTION
  debug(1, "convolution is %d.", config.convolution);
  debug(1, "convolution IR file is \"%s\"", config.convolution_ir_file);
  debug(1, "convolution max length %d", config.convolution_max_length);
  debug(1, "convolution gain is %f", config.convolution_gain);
#endif
  debug(1, "loudness is %d.", config.loudness);
  debug(1, "loudness reference level is %f", config.loudness_reference_volume_db);

  uint8_t ap_md5[16];

#ifdef CONFIG_SOXR
	pthread_create(&soxr_time_check_thread, NULL, &soxr_time_check, NULL);
#endif

#ifdef CONFIG_OPENSSL
  MD5_CTX ctx;
  MD5_Init(&ctx);
  MD5_Update(&ctx, config.service_name, strlen(config.service_name));
  MD5_Final(ap_md5, &ctx);
#endif

#ifdef CONFIG_MBEDTLS
#if MBEDTLS_VERSION_MINOR >= 7
  mbedtls_md5_context tctx;
  mbedtls_md5_starts_ret(&tctx);
  mbedtls_md5_update_ret(&tctx, (unsigned char *)config.service_name, strlen(config.service_name));
  mbedtls_md5_finish_ret(&tctx, ap_md5);
#else
  mbedtls_md5_context tctx;
  mbedtls_md5_starts(&tctx);
  mbedtls_md5_update(&tctx, (unsigned char *)config.service_name, strlen(config.service_name));
  mbedtls_md5_finish(&tctx, ap_md5);
#endif
#endif

#ifdef CONFIG_POLARSSL
  md5_context tctx;
  md5_starts(&tctx);
  md5_update(&tctx, (unsigned char *)config.service_name, strlen(config.service_name));
  md5_finish(&tctx, ap_md5);
#endif
  memcpy(config.hw_addr, ap_md5, sizeof(config.hw_addr));
#ifdef CONFIG_METADATA
  metadata_init(); // create the metadata pipe if necessary
#endif

#ifdef CONFIG_METADATA_HUB
  // debug(1, "Initialising metadata hub");
  metadata_hub_init();
#endif

#ifdef CONFIG_DACP_CLIENT
  // debug(1, "Requesting DACP Monitor");
  dacp_monitor_start();
#endif

#if defined(CONFIG_DBUS_INTERFACE) || defined(CONFIG_MPRIS_INTERFACE)
  // Start up DBUS services after initial settings are all made
  // debug(1, "Starting up D-Bus services");
  pthread_create(&dbus_thread, NULL, &dbus_thread_func, NULL);
#ifdef CONFIG_DBUS_INTERFACE
  start_dbus_service();
#endif
#ifdef CONFIG_MPRIS_INTERFACE
  start_mpris_service();
#endif
#endif

#ifdef CONFIG_MQTT
  if (config.mqtt_enabled) {
    initialise_mqtt();
  }
#endif

  activity_monitor_start();

  // debug(1, "Successful Startup");
  rtsp_listen_loop();

  // should not reach this...
  // daemon_log(LOG_NOTICE, "Unexpected exit...");
  // daemon_retval_send(0);
  // daemon_pid_file_remove();
  pthread_cleanup_pop(1);
  debug(1, "Odd exit point");
  pthread_exit(NULL);
}
