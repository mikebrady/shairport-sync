/*
 * Audio driver handler. This file is part of Shairport.
 * Copyright (c) James Laird 2013
 * Modifications (c) Mike Brady 2014--2025
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

#include "audio.h"
#include "common.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CONFIG_JACK
extern audio_output audio_jack;
#endif
#ifdef CONFIG_SNDIO
extern audio_output audio_sndio;
#endif
#ifdef CONFIG_AO
extern audio_output audio_ao;
#endif
#ifdef CONFIG_SOUNDIO
extern audio_output audio_soundio;
#endif
#ifdef CONFIG_PIPEWIRE
extern audio_output audio_pw;
#endif
#ifdef CONFIG_PULSEAUDIO
extern audio_output audio_pa;
#endif
#ifdef CONFIG_ALSA
extern audio_output audio_alsa;
#endif
#ifdef CONFIG_DUMMY
extern audio_output audio_dummy;
#endif
#ifdef CONFIG_PIPE
extern audio_output audio_pipe;
#endif
#ifdef CONFIG_STDOUT
extern audio_output audio_stdout;
#endif

static audio_output *outputs[] = {
#ifdef CONFIG_ALSA
    &audio_alsa,
#endif
#ifdef CONFIG_SNDIO
    &audio_sndio,
#endif
#ifdef CONFIG_PIPEWIRE
    &audio_pw,
#endif
#ifdef CONFIG_PULSEAUDIO
    &audio_pa,
#endif
#ifdef CONFIG_JACK
    &audio_jack,
#endif
#ifdef CONFIG_AO
    &audio_ao,
#endif
#ifdef CONFIG_SOUNDIO
    &audio_soundio,
#endif
#ifdef CONFIG_PIPE
    &audio_pipe,
#endif
#ifdef CONFIG_STDOUT
    &audio_stdout,
#endif
#ifdef CONFIG_DUMMY
    &audio_dummy,
#endif
    NULL};

audio_output *audio_get_output(const char *name) {
  audio_output **out;

  // default to the first
  if (!name)
    return outputs[0];

  for (out = outputs; *out; out++)
    if (!strcasecmp(name, (*out)->name))
      return *out;

  return NULL;
}

void audio_ls_outputs(void) {
  audio_output **out;

  printf("Available audio backends:\n");
  for (out = outputs; *out; out++)
    printf("    %s%s\n", (*out)->name, out == outputs ? " (default)" : "");

  for (out = outputs; *out; out++) {
    printf("\n");
    if ((*out)->help) {
      printf("Settings and options for the audio backend \"%s\":\n", (*out)->name);
      (*out)->help();
    } else {
      printf("There are no settings or options for the audio backend \"%s\".\n", (*out)->name);
    }
  }
}

void parse_audio_options(const char *named_stanza, uint32_t default_format_set,
                         uint32_t default_rate_set, uint32_t default_channel_set) {
  /* this must be called after the output device has been initialised, so that the default values
   * are set before any options are chosen */
  int value;
  double dvalue;
  const char *str = 0;
  if (config.cfg != NULL) {

    /* Get the desired buffer size setting (deprecated). */
    if (config_lookup_int(config.cfg, "general.audio_backend_buffer_desired_length", &value)) {
      inform("The setting general.audio_backend_buffer_desired_length is no longer supported. "
             "Please use general.audio_backend_buffer_desired_length_in_seconds instead.");
    }

    /* Get the desired backend buffer size setting in seconds. */
    /* This is the size of the buffer in the output system, e.g. in the DAC itself in ALSA */
    if (config_lookup_float(config.cfg, "general.audio_backend_buffer_desired_length_in_seconds",
                            &dvalue)) {
      if (dvalue < 0) {
        die("Invalid audio_backend_buffer_desired_length_in_seconds value: \"%f\". It "
            "should be 0.0 or greater."
            " The default is %.3f seconds",
            dvalue, config.audio_backend_buffer_desired_length);
      } else {
        config.audio_backend_buffer_desired_length = dvalue;
      }
    }

    /* Get the desired decoded buffer size setting in seconds. */
    /* This is the size of the buffer of decoded and deciphered audio held in the player's output
     * queue prior to sending it to the output system */
    if (config_lookup_float(config.cfg, "general.audio_decoded_buffer_desired_length_in_seconds",
                            &dvalue)) {
      if (dvalue < 0) {
        die("Invalid audio_decoded_buffer_desired_length_in_seconds value: \"%f\". It "
            "should be 0.0 or greater."
            " The default is %.3f seconds",
            dvalue, config.audio_decoded_buffer_desired_length);
      } else {
        config.audio_decoded_buffer_desired_length = dvalue;
      }
    }

    /* Get the minimum buffer size for fancy interpolation setting in seconds. */
    if (config_lookup_float(config.cfg,
                            "general.audio_backend_buffer_interpolation_threshold_in_seconds",
                            &dvalue)) {
      if ((dvalue < 0) || (dvalue > config.audio_backend_buffer_desired_length)) {
        die("Invalid audio_backend_buffer_interpolation_threshold_in_seconds value: \"%f\". It "
            "should be between 0 and "
            "audio_backend_buffer_desired_length_in_seconds of %.3f, default is %.3f seconds",
            dvalue, config.audio_backend_buffer_desired_length,
            config.audio_backend_buffer_interpolation_threshold_in_seconds);
      } else {
        config.audio_backend_buffer_interpolation_threshold_in_seconds = dvalue;
      }
    }

    /* Get the latency offset (deprecated). */
    if (config_lookup_int(config.cfg, "general.audio_backend_latency_offset", &value)) {

      inform("The setting general.audio_backend_latency_offset is no longer supported. "
             "Please use general.audio_backend_latency_offset_in_seconds instead.");
    }

    /* Get the latency offset in seconds. */
    if (config_lookup_float(config.cfg, "general.audio_backend_latency_offset_in_seconds",
                            &dvalue)) {
      config.audio_backend_latency_offset = dvalue;
    }

    /* Check if the length of the silent lead-in ia set to \"auto\". */
    if (config_lookup_string(config.cfg, "general.audio_backend_silent_lead_in_time", &str)) {
      if (strcasecmp(str, "auto") == 0) {
        config.audio_backend_silent_lead_in_time_auto = 1;
      } else {
        if (config.audio_backend_silent_lead_in_time_auto == 1)
          warn("Invalid audio_backend_silent_lead_in_time \"%s\". It should be \"auto\" or the "
               "lead-in time in seconds. "
               "It remains set to \"auto\". Note: numbers should not be placed in quotes.",
               str);
        else
          warn("Invalid output rate \"%s\". It should be \"auto\" or the lead-in time in seconds. "
               "It remains set to %f. Note: numbers should not be placed in quotes.",
               str, config.audio_backend_silent_lead_in_time);
      }
    }

    /* Get the desired length of the silent lead-in. */
    if (config_lookup_float(config.cfg, "general.audio_backend_silent_lead_in_time", &dvalue)) {
      if ((dvalue < 0.0) || (dvalue > 4)) {
        if (config.audio_backend_silent_lead_in_time_auto == 1)
          warn("Invalid audio_backend_silent_lead_in_time \"%f\". It "
               "must be between 0.0 and 4.0 seconds. Omit the setting to use the automatic value. "
               "The setting remains at \"auto\".",
               dvalue);
        else
          warn("Invalid audio_backend_silent_lead_in_time \"%f\". It "
               "must be between 0.0 and 4.0 seconds. Omit the setting to use the automatic value. "
               "It remains set to %f.",
               dvalue, config.audio_backend_silent_lead_in_time);
      } else {
        config.audio_backend_silent_lead_in_time = dvalue;
        config.audio_backend_silent_lead_in_time_auto = 0;
      }
    }
  }

  if (named_stanza != NULL) {
    config.format_set = get_format_settings(named_stanza, "output_format");
    config.rate_set = get_rate_settings(named_stanza, "output_rate");
    config.channel_set = get_channel_settings(named_stanza, "output_channels");
  }
  // use the supplied defaults if no settings were given
  if (config.format_set == 0)
    config.format_set = default_format_set;
  if (config.rate_set == 0)
    config.rate_set = default_rate_set;
  if (config.channel_set == 0)
    config.channel_set = default_channel_set;
}

uint32_t get_format_settings(const char *stanza_name, const char *setting_name) {
  uint32_t format_set = 0;
  if (config.cfg != NULL) {
    char setting_path[256];
    snprintf(setting_path, sizeof(setting_path) - 1, "%s.%s", stanza_name, setting_name);
    // get any format settings -- can be "auto", a single format e.g. "S16" or a list of formats
    config_setting_t *format_setting = config_lookup(config.cfg, setting_path);
    if (format_setting != NULL) {
      const char **format_settings;
      int format_settings_count =
          config_get_string_settings_as_string_array(format_setting, &format_settings);
      if (format_settings_count > 0) {
        int i;
        for (i = 0; i < format_settings_count; i++) {
          debug(3, "format setting %u: \"%s\".", i, format_settings[i]);
          if (strcmp(format_settings[i], "auto") == 0) {
            if (format_settings_count != 1)
              warn("in the \"%s\" setting in the \"%s\" section of the configuration file, "
                   "multiple formats, including \"auto\", "
                   "are specified, but \"auto\" includes all "
                   "formats anyway...",
                   setting_name, stanza_name);
            format_set = SPS_FORMAT_SET; // all valid formats
          } else if (strcmp(format_settings[i], "S16") == 0) {
            format_set |= (1 << SPS_FORMAT_S16_LE) | (1 << SPS_FORMAT_S16_BE);
          } else if (strcmp(format_settings[i], "S24") == 0) {
            format_set |= (1 << SPS_FORMAT_S24_LE) | (1 << SPS_FORMAT_S24_BE) |
                          (1 << SPS_FORMAT_S24_3LE) | (1 << SPS_FORMAT_S24_3BE);
          } else if (strcmp(format_settings[i], "S32") == 0) {
            format_set |= (1 << SPS_FORMAT_S32_LE) | (1 << SPS_FORMAT_S32_BE);
          } else {
            sps_format_t f;
            int valid = 0;
            for (f = SPS_FORMAT_LOWEST; f <= SPS_FORMAT_HIGHEST_NATIVE; f++) {
              if (strcmp(format_settings[i], sps_format_description_string(f)) == 0) {
                format_set |= (1 << f);
                valid = 1;
              }
            }
            if (valid == 0) {
              warn("in the \"%s\" setting in the \"%s\" section of the configuration file, an "
                   "invalid format: \"%s\" has been detected.",
                   setting_name, stanza_name, format_settings[i]);
            }
          }
        }
        free(format_settings);
      } else {
        debug(1,
              "in the \"%s\" setting in the \"%s\" section of the configuration file, an error has "
              "been detected at argument %d.",
              setting_name, stanza_name, -format_settings_count);
      }
    }
  }
  sps_format_t f;
  uint32_t t_format_set = format_set;
  char buf[256];
  char *p = buf;
  for (f = SPS_FORMAT_UNKNOWN; f <= SPS_FORMAT_S32_BE; f++) {
    if ((t_format_set & (1 << f)) != 0) {
      snprintf(p, sizeof(buf) - (p - buf) - 1, "%s", sps_format_description_string(f));
      p = p + strlen(sps_format_description_string(f));
      t_format_set -= (1 << f);
      if (t_format_set != 0) {
        snprintf(p, sizeof(buf) - (p - buf) - 1, ", ");
        p = p + strlen(", ");
      }
    }
  }
  if (p != buf) {
    *p = '.';
    p++;
  }
  *p = '\0';
  if (strlen(buf) == 0)
    debug(3, "No \"%s\" output format settings.", stanza_name);
  else
    debug(3, "The \"%s\" output format settings are: \"%s\".", stanza_name, buf);
  return format_set;
}

uint32_t get_rate_settings(const char *stanza_name, const char *setting_name) {
  uint32_t rate_set = 0;
  if (config.cfg != NULL) {
    char setting_path[256];
    snprintf(setting_path, sizeof(setting_path) - 1, "%s.%s", stanza_name, setting_name);
    config_setting_t *rate_setting = config_lookup(config.cfg, setting_path);
    if (rate_setting != NULL) {
      if (config_setting_type(rate_setting) == CONFIG_TYPE_STRING) {
        // see if it is "auto"
        if (strcmp(config_setting_get_string(rate_setting), "auto") == 0) {
#ifdef CONFIG_FFMPEG
          rate_set = SPS_RATE_SET; // all valid rates
#else
          rate_set = SPS_RATE_NON_FFMPEG_SET;
#endif
        } else {
          warn("In the \"%s\" setting in the \"%s\" section of the configuration file, an invalid "
               "character string -- \"%s\" -- has been detected. (Note that numbers must not be "
               "enclosed in quotes.)",
               setting_name, stanza_name, config_setting_get_string(rate_setting));
        }
      } else {
        int *rates;
        int rate_settings_count = config_get_int_settings_as_int_array(rate_setting, &rates);
        if (rate_settings_count > 0) {
          debug(3, "%d rate settings found.", rate_settings_count);
          int i;
          for (i = 0; i < rate_settings_count; i++) {
            debug(3, "rate setting %d: %d.", i, rates[i]);
            sps_rate_t r;
            int valid = 0;
            for (r = SPS_RATE_5512; r <= SPS_RATE_384000; r++) {
              if ((unsigned int)rates[i] == sps_rate_actual_rate(r)) {
                valid = 1;

#ifdef CONFIG_FFMPEG
                rate_set |= (1 << r);
#else
                if (((1 << r) & SPS_RATE_NON_FFMPEG_SET) != 0) {
                  rate_set |= (1 << r);
                } else {
                  warn("In the \"%s\" setting in the \"%s\" section of the configuration file, "
                       "the rate selected -- %d -- can not be used because Shairport Sync has been "
                       "built without FFmpeg support.",
                       setting_name, stanza_name, rates[i]);
                }
#endif
              }
            }
            if (valid == 0) {
              warn("In the \"%s\" setting in the \"%s\" section of the configuration file, an "
                   "invalid rate: %d has been detected.",
                   setting_name, stanza_name, rates[i]);
            }
          }
          free(rates);
        } else {
          warn("in the \"%s\" setting in the \"%s\" section of the configuration file, an error "
               "has been detected at argument %d. (Note that numbers must not be enclosed in "
               "quotes.)",
               setting_name, stanza_name, -rate_settings_count);
        }
      }
    }
  }
  sps_rate_t r;
  char buf[256];
  char *p = buf;
  uint32_t t_rate_set = rate_set;
  char numbuf[32];
  for (r = SPS_RATE_UNKNOWN; r <= SPS_RATE_384000; r++) {
    if ((t_rate_set & (1 << r)) != 0) {
      snprintf(numbuf, sizeof(numbuf) - 1, "%u", sps_rate_actual_rate(r));
      snprintf(p, sizeof(buf) - (p - buf) - 1, "%s", numbuf);
      p = p + strlen(numbuf);
      t_rate_set -= (1 << r);
      if (t_rate_set != 0) {
        snprintf(p, sizeof(buf) - (p - buf) - 1, ", ");
        p = p + strlen(", ");
      }
    }
  }
  if (p != buf) {
    *p = '.';
    p++;
  }
  *p = '\0';
  if (strlen(buf) == 0)
    debug(3, "No \"%s\" output rate settings.", stanza_name);
  else
    debug(3, "The \"%s\" output rate settings are: \"%s\".", stanza_name, buf);
  return rate_set;
}

uint32_t get_channel_settings(const char *stanza_name, const char *setting_name) {
  uint32_t channel_set = 0;
  if (config.cfg != NULL) {
    char setting_path[256];
    snprintf(setting_path, sizeof(setting_path) - 1, "%s.%s", stanza_name, setting_name);
    // now get the channels count set
    config_setting_t *channels_setting = config_lookup(config.cfg, setting_path);
    if (channels_setting != NULL) {
      if (config_setting_type(channels_setting) == CONFIG_TYPE_STRING) {
        // see if it is "auto"
        if (strcmp(config_setting_get_string(channels_setting), "auto") == 0) {
#ifdef CONFIG_FFMPEG
          channel_set = SPS_CHANNEL_SET; // all valid channels
#else
          channel_set = SPS_CHANNNEL_NON_FFMPEG_SET; // just two channels
#endif
        } else {
          warn("in the \"%s\" setting in the \"%s\" section of the configuration file, an invalid "
               "setting: \"%s\" has been detected.",
               setting_name, stanza_name, config_setting_get_string(channels_setting));
        }
      } else {
        int *channel_counts;
        int channel_counts_count =
            config_get_int_settings_as_int_array(channels_setting, &channel_counts);
        if (channel_counts_count > 0) {
          debug(3, "%d channel count settings found.", channel_counts_count);
          int i;
          for (i = 0; i < channel_counts_count; i++) {
            debug(3, "channel count setting %d: %d.", i, channel_counts[i]);

            if ((channel_counts[i] >= 1) && (channel_counts[i] <= 8)) {
#ifdef CONFIG_FFMPEG
              channel_set |= (1 << channel_counts[i]);
#else
              if (((1 << channel_counts[i]) & SPS_CHANNNEL_NON_FFMPEG_SET) != 0) {
                channel_set |= (1 << channel_counts[i]);
              } else {
                warn("in the \"%s\" setting in the \"%s\" section of the configuration file, "
                     "the channel count selected -- %d -- can not be used because Shairport Sync "
                     "has been built without FFmpeg support.",
                     setting_name, stanza_name, channel_counts[i]);
              }
#endif
            } else {
              warn("in the \"%s\" setting in the \"%s\" section of the configuration file, an "
                   "invalid channel count: %d has been detected.",
                   setting_name, stanza_name, channel_counts[i]);
            }
          }
          free(channel_counts);
        } else {
          debug(1,
                "in the \"%s\" setting in the \"%s\" section of the configuration file, an error "
                "has been detected at argument %d.",
                setting_name, stanza_name, -channel_counts_count);
        }
      }
    }
  }
  char buf[256];
  char *p = buf;
  char numbuf[32];
  unsigned int c;
  uint32_t t_channel_set = channel_set;
  for (c = 0; c <= 8; c++) {
    if ((t_channel_set & (1 << c)) != 0) {
      snprintf(numbuf, sizeof(numbuf) - 1, "%u", c);
      snprintf(p, sizeof(buf) - (p - buf) - 1, "%s", numbuf);
      p = p + strlen(numbuf);
      t_channel_set -= (1 << c);
      if (t_channel_set != 0) {
        snprintf(p, sizeof(buf) - (p - buf) - 1, ", ");
        p = p + strlen(", ");
      }
    }
  }
  if (p != buf) {
    *p = '.';
    p++;
  }
  *p = '\0';
  if (strlen(buf) == 0)
    debug(3, "No \"%s\" output channel settings.", stanza_name);
  else
    debug(3, "The \"%s\" output channel settings are: \"%s\".", stanza_name, buf);
  return channel_set;
}

sps_format_t check_configuration_with_formats(
    unsigned int channels, unsigned int rate, sps_format_t format,
    int (*check_configuration)(unsigned int channels, unsigned int rate, unsigned int format)) {
  sps_format_t response = SPS_FORMAT_UNKNOWN; // this is encoded as zero

  // first, check that the channels are permissible...
  if ((config.channel_set & (1 << channels)) != 0) {
    // now check the rate...
    sps_rate_t r = SPS_RATE_LOWEST;
    int rate_is_permissible = 0;
    while ((rate_is_permissible == 0) && (r <= SPS_RATE_HIGHEST)) {
      if (((config.rate_set & (1 << r)) != 0) && (rate == sps_rate_actual_rate(r)))
        rate_is_permissible = 1;
      else
        r++;
    }
    if (rate_is_permissible != 0) {
      // check the actual requested format first with the initial_search array
      // and if it fails, then check in the subsequent_search array.
      // the choice of subsequent search array is determined by the machine's endianness

      // clang-format off
      sps_format_t ordered_initial_search_U8[] = {SPS_FORMAT_U8, SPS_FORMAT_S8};
      sps_format_t ordered_initial_search_S8[] = {SPS_FORMAT_S8, SPS_FORMAT_U8};
      sps_format_t ordered_subsequent_search_8_LE[] = {SPS_FORMAT_S16_LE, SPS_FORMAT_S16_BE, SPS_FORMAT_S24_LE, SPS_FORMAT_S24_BE, SPS_FORMAT_S24_3LE, SPS_FORMAT_S24_3BE, SPS_FORMAT_S32_LE, SPS_FORMAT_S32_BE};
      sps_format_t ordered_subsequent_search_8_BE[] = {SPS_FORMAT_S16_BE, SPS_FORMAT_S16_LE, SPS_FORMAT_S24_BE, SPS_FORMAT_S24_LE, SPS_FORMAT_S24_3BE, SPS_FORMAT_S24_3LE, SPS_FORMAT_S32_BE, SPS_FORMAT_S32_LE};
      
      sps_format_t ordered_initial_search_S16_LE[] = {SPS_FORMAT_S16_LE, SPS_FORMAT_S16_BE};
      sps_format_t ordered_initial_search_S16_BE[] = {SPS_FORMAT_S16_BE, SPS_FORMAT_S16_LE};
      sps_format_t ordered_subsequent_search_S16_LE[] = {SPS_FORMAT_S24_LE, SPS_FORMAT_S24_BE, SPS_FORMAT_S24_3LE, SPS_FORMAT_S24_3BE, SPS_FORMAT_S32_LE, SPS_FORMAT_S32_BE, SPS_FORMAT_S8, SPS_FORMAT_U8};
      sps_format_t ordered_subsequent_search_S16_BE[] = {SPS_FORMAT_S24_BE, SPS_FORMAT_S24_LE, SPS_FORMAT_S24_3BE, SPS_FORMAT_S24_3LE, SPS_FORMAT_S32_BE, SPS_FORMAT_S32_LE, SPS_FORMAT_S8, SPS_FORMAT_U8};
      
      sps_format_t ordered_initial_search_S24_LE[] = {SPS_FORMAT_S24_LE, SPS_FORMAT_S24_BE};
      sps_format_t ordered_initial_search_S24_BE[] = {SPS_FORMAT_S24_BE, SPS_FORMAT_S24_LE};
      sps_format_t ordered_subsequent_search_S24_LE[] = {SPS_FORMAT_S24_3LE, SPS_FORMAT_S24_3BE, SPS_FORMAT_S32_LE, SPS_FORMAT_S32_BE, SPS_FORMAT_S16_LE, SPS_FORMAT_S16_BE, SPS_FORMAT_S8, SPS_FORMAT_U8};
      sps_format_t ordered_subsequent_search_S24_BE[] = {SPS_FORMAT_S24_3BE, SPS_FORMAT_S24_3LE, SPS_FORMAT_S32_BE, SPS_FORMAT_S32_LE, SPS_FORMAT_S16_BE, SPS_FORMAT_S16_LE, SPS_FORMAT_S8, SPS_FORMAT_U8};
      
      sps_format_t ordered_initial_search_S24_3le[] = {SPS_FORMAT_S24_3LE, SPS_FORMAT_S24_3BE};
      sps_format_t ordered_initial_search_S24_3be[] = {SPS_FORMAT_S24_3BE, SPS_FORMAT_S24_3LE};
      sps_format_t ordered_subsequent_search_S24_3le[] = {SPS_FORMAT_S24_LE, SPS_FORMAT_S24_BE, SPS_FORMAT_S32_LE, SPS_FORMAT_S32_BE, SPS_FORMAT_S16_LE, SPS_FORMAT_S16_BE, SPS_FORMAT_S8, SPS_FORMAT_U8};
      sps_format_t ordered_subsequent_search_S24_3be[] = {SPS_FORMAT_S24_BE, SPS_FORMAT_S24_LE, SPS_FORMAT_S32_BE, SPS_FORMAT_S32_LE, SPS_FORMAT_S16_BE, SPS_FORMAT_S16_LE, SPS_FORMAT_S8, SPS_FORMAT_U8};
      
      sps_format_t ordered_initial_search_S32_LE[] = {SPS_FORMAT_S32_LE, SPS_FORMAT_S32_BE};
      sps_format_t ordered_initial_search_S32_BE[] = {SPS_FORMAT_S32_BE, SPS_FORMAT_S32_LE};
      sps_format_t ordered_subsequent_search_S32_LE[] = {SPS_FORMAT_S24_LE, SPS_FORMAT_S24_BE, SPS_FORMAT_S24_3LE, SPS_FORMAT_S24_3BE, SPS_FORMAT_S16_LE, SPS_FORMAT_S16_BE, SPS_FORMAT_S8, SPS_FORMAT_U8};
      sps_format_t ordered_subsequent_search_S32_BE[] = {SPS_FORMAT_S24_BE, SPS_FORMAT_S24_LE, SPS_FORMAT_S24_3BE, SPS_FORMAT_S24_3LE, SPS_FORMAT_S16_BE, SPS_FORMAT_S16_LE, SPS_FORMAT_S8, SPS_FORMAT_U8};
      // clang-format on

      // set up the initial_search pointer
      sps_format_t *initial_formats_to_check = NULL;
      unsigned int number_of_initial_formats_to_check = 0;

      // set up the subsequent_search pointer
      sps_format_t *subsequent_formats_to_check = NULL;
      unsigned int number_of_subsequent_formats_to_check = 0;

      // Set up the initial and subsequent search pointers.

      // Searching is done with an initial search and, if necessary, a subsequent search.
      // Searching stops when a suitable format is confirmed as being acceptable.

      // The initial search checks the requested format first, followed by its equivalents of the
      // same bit size. The subsequent search puts the formats to check in order of suitability but
      // with the native-endian format first. SPS_FORMAT_S16, SPS_FORMAT_S24 and SPS_FORMAT_S24 are
      // treated the same as their equivalent with native endianness. AUTO is treated as
      // SPS_FORMAT_S24 with native endianness.

      switch (format) {
      case SPS_FORMAT_U8:
      case SPS_FORMAT_S8:
        if (config.endianness == SS_LITTLE_ENDIAN) {
          subsequent_formats_to_check = ordered_subsequent_search_8_LE;
          number_of_subsequent_formats_to_check =
              sizeof(ordered_subsequent_search_8_LE) / sizeof(sps_format_t);
        } else {
          subsequent_formats_to_check = ordered_subsequent_search_8_BE;
          number_of_subsequent_formats_to_check =
              sizeof(ordered_subsequent_search_8_BE) / sizeof(sps_format_t);
        }
        if (format == SPS_FORMAT_S8) {
          initial_formats_to_check = ordered_initial_search_S8;
          number_of_initial_formats_to_check =
              sizeof(ordered_initial_search_S8) / sizeof(sps_format_t);
        } else {
          initial_formats_to_check = ordered_initial_search_U8;
          number_of_initial_formats_to_check =
              sizeof(ordered_initial_search_U8) / sizeof(sps_format_t);
        }
        break;
      case SPS_FORMAT_S16:
      case SPS_FORMAT_S16_LE:
      case SPS_FORMAT_S16_BE:
        if (config.endianness == SS_LITTLE_ENDIAN) {
          subsequent_formats_to_check = ordered_subsequent_search_S16_LE;
          number_of_subsequent_formats_to_check =
              sizeof(ordered_subsequent_search_S16_LE) / sizeof(sps_format_t);
          if (format == SPS_FORMAT_S16) {
            initial_formats_to_check = ordered_initial_search_S16_LE;
            number_of_initial_formats_to_check =
                sizeof(ordered_initial_search_S16_LE) / sizeof(sps_format_t);
          }
        } else {
          subsequent_formats_to_check = ordered_subsequent_search_S16_BE;
          number_of_subsequent_formats_to_check =
              sizeof(ordered_subsequent_search_S16_BE) / sizeof(sps_format_t);
          if (format == SPS_FORMAT_S16) {
            initial_formats_to_check = ordered_initial_search_S16_BE;
            number_of_initial_formats_to_check =
                sizeof(ordered_initial_search_S16_BE) / sizeof(sps_format_t);
          }
        }
        if (format == SPS_FORMAT_S16_LE) {
          initial_formats_to_check = ordered_initial_search_S16_LE;
          number_of_initial_formats_to_check =
              sizeof(ordered_initial_search_S16_LE) / sizeof(sps_format_t);
        } else if (format == SPS_FORMAT_S16_BE) {
          initial_formats_to_check = ordered_initial_search_S16_BE;
          number_of_initial_formats_to_check =
              sizeof(ordered_initial_search_S16_BE) / sizeof(sps_format_t);
        }
        break;
      case SPS_FORMAT_S24:
      case SPS_FORMAT_S24_LE:
      case SPS_FORMAT_S24_BE:
        if (config.endianness == SS_LITTLE_ENDIAN) {
          subsequent_formats_to_check = ordered_subsequent_search_S24_LE;
          number_of_subsequent_formats_to_check =
              sizeof(ordered_subsequent_search_S24_LE) / sizeof(sps_format_t);
          if (format == SPS_FORMAT_S24) {
            initial_formats_to_check = ordered_initial_search_S24_LE;
            number_of_initial_formats_to_check =
                sizeof(ordered_initial_search_S24_LE) / sizeof(sps_format_t);
          }
        } else {
          subsequent_formats_to_check = ordered_subsequent_search_S24_BE;
          number_of_subsequent_formats_to_check =
              sizeof(ordered_subsequent_search_S24_BE) / sizeof(sps_format_t);
          if (format == SPS_FORMAT_S24) {
            initial_formats_to_check = ordered_initial_search_S24_BE;
            number_of_initial_formats_to_check =
                sizeof(ordered_initial_search_S24_BE) / sizeof(sps_format_t);
          }
        }
        if (format == SPS_FORMAT_S24_LE) {
          initial_formats_to_check = ordered_initial_search_S24_LE;
          number_of_initial_formats_to_check =
              sizeof(ordered_initial_search_S24_LE) / sizeof(sps_format_t);
        } else if (format == SPS_FORMAT_S24_BE) {
          initial_formats_to_check = ordered_initial_search_S24_BE;
          number_of_initial_formats_to_check =
              sizeof(ordered_initial_search_S24_BE) / sizeof(sps_format_t);
        }
        break;
      case SPS_FORMAT_S32:
      case SPS_FORMAT_S32_LE:
      case SPS_FORMAT_S32_BE:
        //      case SPS_FORMAT_AUTO:
        if (config.endianness == SS_LITTLE_ENDIAN) {
          subsequent_formats_to_check = ordered_subsequent_search_S32_LE;
          number_of_subsequent_formats_to_check =
              sizeof(ordered_subsequent_search_S32_LE) / sizeof(sps_format_t);
          if ((format == SPS_FORMAT_S32) || (format == SPS_FORMAT_AUTO)) {
            initial_formats_to_check = ordered_initial_search_S32_LE;
            number_of_initial_formats_to_check =
                sizeof(ordered_initial_search_S32_LE) / sizeof(sps_format_t);
          }
        } else {
          subsequent_formats_to_check = ordered_subsequent_search_S32_BE;
          number_of_subsequent_formats_to_check =
              sizeof(ordered_subsequent_search_S32_BE) / sizeof(sps_format_t);
          if ((format == SPS_FORMAT_S32) || (format == SPS_FORMAT_AUTO)) {
            initial_formats_to_check = ordered_initial_search_S32_BE;
            number_of_initial_formats_to_check =
                sizeof(ordered_initial_search_S32_BE) / sizeof(sps_format_t);
          }
        }
        if (format == SPS_FORMAT_S32_LE) {
          initial_formats_to_check = ordered_initial_search_S32_LE;
          number_of_initial_formats_to_check =
              sizeof(ordered_initial_search_S32_LE) / sizeof(sps_format_t);
        } else if (format == SPS_FORMAT_S32_BE) {
          initial_formats_to_check = ordered_initial_search_S32_BE;
          number_of_initial_formats_to_check =
              sizeof(ordered_initial_search_S32_BE) / sizeof(sps_format_t);
        }
        break;
      case SPS_FORMAT_S24_3LE:
      case SPS_FORMAT_S24_3BE:
        if (config.endianness == SS_LITTLE_ENDIAN) {
          subsequent_formats_to_check = ordered_subsequent_search_S24_3le;
          number_of_subsequent_formats_to_check =
              sizeof(ordered_subsequent_search_S24_3le) / sizeof(sps_format_t);
        } else {
          subsequent_formats_to_check = ordered_subsequent_search_S24_3be;
          number_of_subsequent_formats_to_check =
              sizeof(ordered_subsequent_search_S24_3be) / sizeof(sps_format_t);
        }
        if (format == SPS_FORMAT_S24_3LE) {
          initial_formats_to_check = ordered_initial_search_S24_3le;
          number_of_initial_formats_to_check =
              sizeof(ordered_initial_search_S24_3le) / sizeof(sps_format_t);
        } else {
          initial_formats_to_check = ordered_initial_search_S24_3be;
          number_of_initial_formats_to_check =
              sizeof(ordered_initial_search_S24_3be) / sizeof(sps_format_t);
        }
        break;
      default:
        debug(1, "unknown format %u to check.", format);
        break;
      }

      if ((initial_formats_to_check == NULL) || (subsequent_formats_to_check == NULL)) {
        debug(1, "error initialising for format %s.", sps_format_description_string(format));
      }

      // here, we know the channel and rate are permissible according to the settings,
      // but we don't yet know about each of the formats
      // do initial search
      unsigned int i = 0;
      while ((i < number_of_initial_formats_to_check) && (response == SPS_FORMAT_UNKNOWN)) {
        // only call check_configuration() if the format is permissible
        if ((((1 << initial_formats_to_check[i]) & config.format_set) != 0) &&
            ((check_configuration == NULL) ||
             (check_configuration(channels, rate, initial_formats_to_check[i]) == 0)))
          response = initial_formats_to_check[i];
        else
          i++;
      }
      // if no joy, do subsequent search
      if (response == 0) {
        i = 0;
        while ((i < number_of_subsequent_formats_to_check) && (response == SPS_FORMAT_UNKNOWN)) {
          // only call check_configuration() if the format is permissible
          if ((((1 << subsequent_formats_to_check[i]) & config.format_set) != 0) &&
              ((check_configuration == NULL) ||
               (check_configuration(channels, rate, subsequent_formats_to_check[i]) == 0)))
            response = subsequent_formats_to_check[i];
          else
            i++;
        }
      }
    }
  }
  return response;
}

/*

sps_format_t all_formats_to_check_le[] = {SPS_FORMAT_S32_LE, SPS_FORMAT_S32_BE,  SPS_FORMAT_S24_LE,
                                          SPS_FORMAT_S24_BE, SPS_FORMAT_S24_3LE, SPS_FORMAT_S24_3BE,
                                          SPS_FORMAT_S16_LE, SPS_FORMAT_S16_BE};
// look at little-endian formats first
sps_format_t all_formats_to_check_le[] = {SPS_FORMAT_S32_LE, SPS_FORMAT_S32_BE,  SPS_FORMAT_S24_LE,
                                          SPS_FORMAT_S24_BE, SPS_FORMAT_S24_3LE, SPS_FORMAT_S24_3BE,
                                          SPS_FORMAT_S16_LE, SPS_FORMAT_S16_BE};
// look at big-endian formats first
sps_format_t all_formats_to_check_be[] = {SPS_FORMAT_S32_BE, SPS_FORMAT_S32_LE,  SPS_FORMAT_S24_BE,
                                          SPS_FORMAT_S24_LE, SPS_FORMAT_S24_3BE, SPS_FORMAT_S24_3LE,
                                          SPS_FORMAT_S16_BE, SPS_FORMAT_S16_LE};
// look at little-endian formats first
sps_format_t s32_formats_to_check_le[] = {SPS_FORMAT_S32_LE, SPS_FORMAT_S32_BE};
// look at big-endian formats first
sps_format_t s32_formats_to_check_be[] = {SPS_FORMAT_S32_BE, SPS_FORMAT_S32_LE};
// look at little-endian formats first
sps_format_t s24_formats_to_check_le[] = {
    SPS_FORMAT_S24_LE,
    SPS_FORMAT_S24_BE,
    SPS_FORMAT_S24_3LE,
    SPS_FORMAT_S24_3BE,
};
// look at big-endian formats first
sps_format_t s24_formats_to_check_be[] = {
    SPS_FORMAT_S24_BE,
    SPS_FORMAT_S24_LE,
    SPS_FORMAT_S24_3BE,
    SPS_FORMAT_S24_3LE,
};
// look at little-endian formats first
sps_format_t s16_formats_to_check_le[] = {SPS_FORMAT_S16_LE, SPS_FORMAT_S16_BE};
// look at big-endian formats first
sps_format_t s16_formats_to_check_be[] = {SPS_FORMAT_S16_BE, SPS_FORMAT_S16_LE};

sps_format_t single_format_to_check[1];

sps_format_t check_configuration_with_formats(
    unsigned int channels, unsigned int rate, sps_format_t format,
    int (*check_configuration)(unsigned int channels, unsigned int rate, unsigned int format)) {
  sps_format_t response = SPS_FORMAT_UNKNOWN; // this is encoded as zero

  // assume there is one single format to check
  unsigned int number_of_formats_to_check = 1;
  sps_format_t single_format_to_check[] = {format};
  sps_format_t *formats_to_check = single_format_to_check;

  // now, check to see if there are multiple formats to check, e.g. if the format is given as AUTO
  // or if it doesn't specify _LE or _BE. Try to favour the host endianness:

  if (config.endianness == SS_LITTLE_ENDIAN) {
    switch (format) {
    case SPS_FORMAT_AUTO:
    case SPS_FORMAT_S32:
    case SPS_FORMAT_32_LE:
    case SPS_FORMAT_32_BE:
      formats_to_check = all_formats_to_check_le;
      number_of_formats_to_check = sizeof(all_formats_to_check_le) / sizeof(sps_format_t);
      break;
    case SPS_FORMAT_S24:
    case SPS_FORMAT_S24_LE:
    case SPS_FORMAT_S24_BE:
      formats_to_check = s32_formats_to_check_le;
      number_of_formats_to_check = sizeof(s32_formats_to_check_le) / sizeof(sps_format_t);
      break;
    case SPS_FORMAT_S24:
      formats_to_check = s24_formats_to_check_le;
      number_of_formats_to_check = sizeof(s24_formats_to_check_le) / sizeof(sps_format_t);
      break;
    case SPS_FORMAT_S16:
      formats_to_check = s16_formats_to_check_le;
      number_of_formats_to_check = sizeof(s16_formats_to_check_le) / sizeof(sps_format_t);
      break;
    default:
      break;
    };
  } else { // big-endian
    switch (format) {
    case SPS_FORMAT_AUTO:
      formats_to_check = all_formats_to_check_be;
      number_of_formats_to_check = sizeof(all_formats_to_check_be) / sizeof(sps_format_t);
      break;
    case SPS_FORMAT_S32:
      formats_to_check = s32_formats_to_check_be;
      number_of_formats_to_check = sizeof(s32_formats_to_check_be) / sizeof(sps_format_t);
      break;
    case SPS_FORMAT_S24:
      formats_to_check = s24_formats_to_check_be;
      number_of_formats_to_check = sizeof(s24_formats_to_check_be) / sizeof(sps_format_t);
      break;
    case SPS_FORMAT_S16:
      formats_to_check = s16_formats_to_check_be;
      number_of_formats_to_check = sizeof(s16_formats_to_check_be) / sizeof(sps_format_t);
      break;
    default:
      break;
    };
  }

  unsigned int i = 0;
  while ((i < number_of_formats_to_check) && (response == SPS_FORMAT_UNKNOWN)) {
    if (check_configuration(channels, rate, formats_to_check[i]) == 0)
      response = formats_to_check[i];
    else
      i++;
  }

  return response;
}

*/

// This looks for a suitable configuration in the right order and checks possible
// configurations using the check_configuration method passed in.

// If it's the same as the most recent request, it sends back the cached reply.
// The cache is keyed to the address of the checker method in order to distinguish
// between requests from different backends. It's moot, as only one back end can be
// active, but anyway... ya never just know.

// but it does mean that it only remembers the last response, whichever backend it was for

typedef struct {
  int32_t encoded_request;  // this will have the channels and the rate requested but not the format
  int32_t encoded_response; // this will have the channels, rate and format suggested
  int (*check_configuration)(unsigned int channels, unsigned int rate, unsigned int format);
} configuration_request_and_response_t;

static configuration_request_and_response_t search_for_suitable_configuration_last_response = {
    0, 0, NULL};

// First it looks for an exact match of channels, rate or with a rate that is an even multiple.
// If that fails it will look for an exact match of channels with the next highest rate
// If that fails it will look for an exact match of channels with the next lowest rate
// If that fails it increases the number channels, if available, and checks all over again
// Finally, if that fails it reduces the number channels, if possible, and checks all over again.

// Regarding the requested format: ask for the exact format only if no audio processing will be
// done in the software or (as far as can be known) in the hardware mixer.
// So, if ignore_volume_control is set and volume_max_db is
// not set, and the number of output channels is greater than or equal to those requested,
// and mixing to mono is not requested and the rate being checked is the rate requested,
// then look for the exact rate format.
// Otherwise ask for the deepest format, which could be useful
// for attenuation, mixing, transcoding or other audio processing.
// (Haven't included checking for convolution here.)

int32_t search_for_suitable_configuration(unsigned int channels, unsigned int rate,
                                          unsigned int format,
                                          int (*check_configuration)(unsigned int in_channels,
                                                                     unsigned int in_rate,
                                                                     unsigned int in_format)) {
  int32_t reply = 0; // means nothing was found
  if ((channels == 0) || (rate == 0))
    debug(1, "invalid channel %u or rate %u parameters to search_for_suitable_configuration",
          channels, rate);
  int32_t encoded_request = CHANNELS_TO_ENCODED_FORMAT(channels) | RATE_TO_ENCODED_FORMAT(rate) |
                            FORMAT_TO_ENCODED_FORMAT(format);

  if ((search_for_suitable_configuration_last_response.check_configuration ==
       check_configuration) && // same method (implies same backend)
      (search_for_suitable_configuration_last_response.encoded_request ==
       encoded_request)) { // same request
    reply = search_for_suitable_configuration_last_response
                .encoded_response; // provide cached response...
  }
  if (reply == 0) { // no luck with the last response generated, if any...
    // check for native number of channels or more... and then, if not successful, with fewer
    // channels
#ifdef CONFIG_FFMPEG
    unsigned int channel_count_check_sequence[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 7, 6, 5, 4, 3, 2, 1};
    unsigned int rates[] = {44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000,
                            64000, 32000, 22050, 16000, 11025,  8000,   5512};
#else
    unsigned int channel_count_check_sequence[] = {0, 1, 2};
//    unsigned int rates[] = {44100};
#endif

    unsigned int channel_count_index = channels;
    // start looking with the required number of channels
    unsigned int local_channels;
    unsigned int local_rate;
    sps_format_t local_format = SPS_FORMAT_UNKNOWN;
    uint32_t channel_check_bit_field = 0; // set (1 << channel_count) when check done.
    // Use it to stop checking the same channel count on the way down as well as on the way up...
    while ((local_format == SPS_FORMAT_UNKNOWN) &&
           (channel_count_index < (sizeof(channel_count_check_sequence) / sizeof(unsigned int)))) {
      local_channels = channel_count_check_sequence[channel_count_index];
      // if we haven't already checked....
      if ((channel_check_bit_field & (1 << local_channels)) == 0) {
        channel_check_bit_field |= (1 << local_channels); // flag that we have checked it
        int rate_multiplier = 2;
        local_rate = rate; // begin with the requested rate
        debug(3, "check for an exact multiple of %u with %u channels.", rate, local_channels);
        while ((local_rate <= 384000) && (local_format == SPS_FORMAT_UNKNOWN)) {
          if (
              // clang-format off
            // check for the exact format only under these conditions, otherwise look for the best
            (config.ignore_volume_control != 0) &&
            (config.volume_max_db_set == 0) &&
            (local_rate == rate) &&
            (local_channels >= channels) &&
            (config.playback_mode != ST_mono)
              // clang-format on
          ) {
            // debug(1, "check exact");
            local_format = check_configuration_with_formats(
                local_channels, local_rate, (sps_format_t)format, check_configuration);
          } else {
            // debug(1, "check best");
            local_format = check_configuration_with_formats(local_channels, local_rate,
                                                            SPS_FORMAT_S32, check_configuration);
          }

          if (local_format == SPS_FORMAT_UNKNOWN) {
            local_rate = rate * rate_multiplier;
            rate_multiplier = rate_multiplier * 2;
          }
        }
#ifdef CONFIG_FFMPEG
        if (local_format == SPS_FORMAT_UNKNOWN) {
          debug(3, "check for an next highest rate above %u with %u channels.", rate,
                local_channels);
          unsigned int rate_pointer = 0;
          while ((rate_pointer < sizeof(rates) / sizeof(unsigned int)) &&
                 (local_format == SPS_FORMAT_UNKNOWN)) {
            local_rate = rates[rate_pointer];
            if (local_rate > rate) {
              local_format = check_configuration_with_formats(
                  local_channels, local_rate, (sps_format_t)format, check_configuration);
            }
            if (local_format == SPS_FORMAT_UNKNOWN) {
              rate_pointer++;
            }
          }
        }

        if (local_format == SPS_FORMAT_UNKNOWN) {
          int rate_pointer = (int)(sizeof(rates) / sizeof(unsigned int) - 1);
          debug(3, "check for an next lowest rate below %u with %u channels.", rate,
                local_channels);
          while ((rate_pointer >= 0) && (local_format == SPS_FORMAT_UNKNOWN)) {
            local_rate = rates[rate_pointer];
            if (local_rate < rate) {
              local_format = check_configuration_with_formats(
                  local_channels, local_rate, (sps_format_t)format, check_configuration);
            }
            if (local_format == SPS_FORMAT_UNKNOWN) {
              rate_pointer--;
            }
          }
        }
#endif
      }
      // if unsuccessful, try with, firstly, more channels, and then, later, with fewer channels
      if (local_format == SPS_FORMAT_UNKNOWN)
        channel_count_index++;
    }
    if (local_format != SPS_FORMAT_UNKNOWN) {
      reply = CHANNELS_TO_ENCODED_FORMAT(local_channels) | RATE_TO_ENCODED_FORMAT(local_rate) |
              FORMAT_TO_ENCODED_FORMAT(local_format);

      // cache the response
      search_for_suitable_configuration_last_response.check_configuration = check_configuration;
      search_for_suitable_configuration_last_response.encoded_request = encoded_request;
      search_for_suitable_configuration_last_response.encoded_response = reply;

      debug(3,
            "output configuration search for request: %s/%u/%u, succeeded with response: %u/%s/%u.",
            sps_format_description_string(format), rate, channels,
            RATE_FROM_ENCODED_FORMAT(reply),                                  // rate
            sps_format_description_string(FORMAT_FROM_ENCODED_FORMAT(reply)), // format
            CHANNELS_FROM_ENCODED_FORMAT(reply)                               // channels
      );

    } else {
      debug(1, "output configuration search for request: %s/%u/%u failed.",
            sps_format_description_string(format), rate, channels);
    }
  }
  return reply;
}
