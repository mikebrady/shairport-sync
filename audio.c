/*
 * Audio driver handler. This file is part of Shairport.
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

#include <stdio.h>
#include <string.h>
#include "audio.h"
#include "config.h"

#ifdef CONFIG_SNDIO
extern audio_output audio_sndio;
#endif
#ifdef CONFIG_AO
extern audio_output audio_ao;
#endif
#ifdef CONFIG_PULSE
extern audio_output audio_pulse;
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
#if (defined(CONFIG_RKTUBE) && defined(__android__))
extern audio_output audio_rktube;
#endif

static audio_output *outputs[] = {
#ifdef CONFIG_SNDIO
    &audio_sndio,
#endif
#ifdef CONFIG_ALSA
    &audio_alsa,
#endif
#ifdef CONFIG_PULSE
    &audio_pulse,
#endif
#ifdef CONFIG_AO
    &audio_ao,
#endif
#ifdef CONFIG_DUMMY
    &audio_dummy,
#endif
#ifdef CONFIG_PIPE 
    &audio_pipe,
#endif
#ifdef CONFIG_STDOUT
    &audio_stdout,
#endif
#if (defined(CONFIG_RKTUBE) && defined(__android__))
    &audio_rktube,
#endif
    NULL};

audio_output *audio_get_output(char *name) {
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

  printf("Available audio outputs:\n");
  for (out = outputs; *out; out++)
    printf("    %s%s\n", (*out)->name, out == outputs ? " (default)" : "");

  for (out = outputs; *out; out++) {
    printf("\n");
    printf("Options for output %s:\n", (*out)->name);
    (*out)->help();
  }
}
