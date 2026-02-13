/*
 * This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2019--2025
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

#include <string.h>

// these are headers for the ALAC decoder, utilities and endian utilities
#include <alac/ALACBitUtilities.h>
#include <alac/ALACDecoder.h>
#include <alac/EndianPortable.h>

#include "apple_alac.h"
#include "config.h"

typedef struct magicCookie {
  ALACSpecificConfig config;
  ALACAudioChannelLayout channelLayoutInfo; // seems to be unused
} magicCookie;

magicCookie cookie;
ALACDecoder *theDecoder;

extern "C" int apple_alac_init(int32_t fmtp[12]) {

  memset(&cookie, 0, sizeof(magicCookie));

  // create a magic cookie for the decoder from the fmtp information. It seems to be in the same
  // format as a simple magic cookie

  cookie.config.frameLength = Swap32NtoB(352);
  cookie.config.compatibleVersion = fmtp[2];         // should be zero, uint8_t
  cookie.config.bitDepth = fmtp[3];                  // uint8_t expected to be 16
  cookie.config.pb = fmtp[4];                        // uint8_t should be 40;
  cookie.config.mb = fmtp[5];                        // uint8_t should be 10;
  cookie.config.kb = fmtp[6];                        // uint8_t should be 14;
  cookie.config.numChannels = fmtp[7];               // uint8_t expected to be 2
  cookie.config.maxRun = Swap16NtoB(fmtp[8]);        // uint16_t expected to be 255
  cookie.config.maxFrameBytes = Swap32NtoB(fmtp[9]); // uint32_t should be 0;
  cookie.config.avgBitRate = Swap32NtoB(fmtp[10]);   // uint32_t should be 0;;
  cookie.config.sampleRate = Swap32NtoB(fmtp[11]);   // uint32_t expected to be 44100;

  theDecoder = new ALACDecoder;
  theDecoder->Init(&cookie, sizeof(magicCookie));

  return 0;
}

extern "C" int apple_alac_decode_frame(unsigned char *sampleBuffer, uint32_t bufferLength,
                                       unsigned char *dest, int *outsize) {
  uint32_t numFrames = 0;
  BitBuffer theInputBuffer;
  BitBufferInit(&theInputBuffer, sampleBuffer, bufferLength);
  theDecoder->Decode(&theInputBuffer, dest, Swap32BtoN(cookie.config.frameLength),
                     cookie.config.numChannels, &numFrames);
  *outsize = numFrames;
  return 0;
}

extern "C" int apple_alac_terminate() {
  delete (theDecoder);
  return 0;
}
