/*
 * 23-bit modular arithmetic. This file is part of Shairport Sync
 * Copyright (c) Mike Brady 2025

 * Modifications, including those associated with audio synchronization, multithreading and
 * metadata handling copyright (c) Mike Brady 2014--2025
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
 
 #include <stdint.h>
 #include "mod23.h"

#define MOD_23BIT 0x7FFFFF // 2^23 - 1

// Assumes 'a' and 'b' are within 2^22 of each other
int32_t a_minus_b_mod23(uint32_t a, uint32_t b) {

  // Mask to 23 bits
  a &= MOD_23BIT;
  b &= MOD_23BIT;

  // Compute difference modulo 2^23
  uint32_t diff = (a - b) & MOD_23BIT;

  // Interpret as signed 23-bit value
  // If the top bit (bit 22) is set, it's negative
  int32_t signed_diff = (diff & 0x400000) ? (diff | 0xFF800000) : diff;

  return signed_diff;
}
