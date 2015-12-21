/*
 * ALAC (Apple Lossless Audio Codec) decoder
 * Copyright (c) 2005 David Hammerton
 * All rights reserved.
 *
 * This is the actual decoder.
 *
 * http://crazney.net/programs/itunes/alac.html
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
 *
 */

static const int host_bigendian = 0;

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
    #include "stdint_win.h"
#else
    #include <stdint.h>
#endif

#include "alac.h"

#define _Swap32(v) do { \
                   v = (((v) & 0x000000FF) << 0x18) | \
                       (((v) & 0x0000FF00) << 0x08) | \
                       (((v) & 0x00FF0000) >> 0x08) | \
                       (((v) & 0xFF000000) >> 0x18); } while(0)

#define _Swap16(v) do { \
                   v = (((v) & 0x00FF) << 0x08) | \
                       (((v) & 0xFF00) >> 0x08); } while (0)

struct {signed int x:24;} se_struct_24;
#define SignExtend24(val) (se_struct_24.x = val)

void alac_free(alac_file *alac) {
    if (alac->predicterror_buffer_a)
        free(alac->predicterror_buffer_a);
    if (alac->predicterror_buffer_b)
        free(alac->predicterror_buffer_b);

    if (alac->outputsamples_buffer_a)
        free(alac->outputsamples_buffer_a);
    if (alac->outputsamples_buffer_b)
        free(alac->outputsamples_buffer_b);

    if (alac->uncompressed_bytes_buffer_a)
        free(alac->uncompressed_bytes_buffer_a);
    if (alac->uncompressed_bytes_buffer_b)
        free(alac->uncompressed_bytes_buffer_b);

    free(alac);
}

void alac_allocate_buffers(alac_file *alac)
{
    alac->predicterror_buffer_a = malloc(alac->setinfo_max_samples_per_frame * 4);
    alac->predicterror_buffer_b = malloc(alac->setinfo_max_samples_per_frame * 4);

    alac->outputsamples_buffer_a = malloc(alac->setinfo_max_samples_per_frame * 4);
    alac->outputsamples_buffer_b = malloc(alac->setinfo_max_samples_per_frame * 4);

    alac->uncompressed_bytes_buffer_a = malloc(alac->setinfo_max_samples_per_frame * 4);
    alac->uncompressed_bytes_buffer_b = malloc(alac->setinfo_max_samples_per_frame * 4);
}

void alac_set_info(alac_file *alac, char *inputbuffer)
{
  char *ptr = inputbuffer;
  ptr += 4; /* size */
  ptr += 4; /* frma */
  ptr += 4; /* alac */
  ptr += 4; /* size */
  ptr += 4; /* alac */

  ptr += 4; /* 0 ? */

  alac->setinfo_max_samples_per_frame = *(uint32_t*)ptr; /* buffer size / 2 ? */
  if (!host_bigendian)
      _Swap32(alac->setinfo_max_samples_per_frame);
  ptr += 4;
  alac->setinfo_7a = *(uint8_t*)ptr;
  ptr += 1;
  alac->setinfo_sample_size = *(uint8_t*)ptr;
  ptr += 1;
  alac->setinfo_rice_historymult = *(uint8_t*)ptr;
  ptr += 1;
  alac->setinfo_rice_initialhistory = *(uint8_t*)ptr;
  ptr += 1;
  alac->setinfo_rice_kmodifier = *(uint8_t*)ptr;
  ptr += 1;
  alac->setinfo_7f = *(uint8_t*)ptr;
  ptr += 1;
  alac->setinfo_80 = *(uint16_t*)ptr;
  if (!host_bigendian)
      _Swap16(alac->setinfo_80);
  ptr += 2;
  alac->setinfo_82 = *(uint32_t*)ptr;
  if (!host_bigendian)
      _Swap32(alac->setinfo_82);
  ptr += 4;
  alac->setinfo_86 = *(uint32_t*)ptr;
  if (!host_bigendian)
      _Swap32(alac->setinfo_86);
  ptr += 4;
  alac->setinfo_8a_rate = *(uint32_t*)ptr;
  if (!host_bigendian)
      _Swap32(alac->setinfo_8a_rate);

  alac_allocate_buffers(alac);

}

/* stream reading */

/* supports reading 1 to 16 bits, in big endian format */
static uint32_t readbits_16(alac_file *alac, int bits)
{
    uint32_t result;
    int new_accumulator;

    result = (alac->input_buffer[0] << 16) |
             (alac->input_buffer[1] << 8) |
             (alac->input_buffer[2]);

    /* shift left by the number of bits we've already read,
     * so that the top 'n' bits of the 24 bits we read will
     * be the return bits */
    result = result << alac->input_buffer_bitaccumulator;

    result = result & 0x00ffffff;

    /* and then only want the top 'n' bits from that, where
     * n is 'bits' */
    result = result >> (24 - bits);

    new_accumulator = (alac->input_buffer_bitaccumulator + bits);

    /* increase the buffer pointer if we've read over n bytes. */
    alac->input_buffer += (new_accumulator >> 3);

    /* and the remainder goes back into the bit accumulator */
    alac->input_buffer_bitaccumulator = (new_accumulator & 7);

    return result;
}

/* supports reading 1 to 32 bits, in big endian format */
static uint32_t readbits(alac_file *alac, int bits)
{
    int32_t result = 0;

    if (bits > 16)
    {
        bits -= 16;
        result = readbits_16(alac, 16) << bits;
    }

    result |= readbits_16(alac, bits);

    return result;
}

/* reads a single bit */
static int readbit(alac_file *alac)
{
    int result;
    int new_accumulator;

    result = alac->input_buffer[0];

    result = result << alac->input_buffer_bitaccumulator;

    result = result >> 7 & 1;

    new_accumulator = (alac->input_buffer_bitaccumulator + 1);

    alac->input_buffer += (new_accumulator / 8);

    alac->input_buffer_bitaccumulator = (new_accumulator % 8);

    return result;
}

static void unreadbits(alac_file *alac, int bits)
{
    int new_accumulator = (alac->input_buffer_bitaccumulator - bits);

    alac->input_buffer += (new_accumulator >> 3);

    alac->input_buffer_bitaccumulator = (new_accumulator & 7);
    if (alac->input_buffer_bitaccumulator < 0)
        alac->input_buffer_bitaccumulator *= -1;
}

/* various implementations of count_leading_zero:
 * the first one is the original one, the simplest and most
 * obvious for what it's doing. never use this.
 * then there are the asm ones. fill in as necessary
 * and finally an unrolled and optimised c version
 * to fall back to
 */
#if 0
/* hideously inefficient. could use a bitmask search,
 * alternatively bsr on x86,
 */
static int count_leading_zeros(int32_t input)
{
    int i = 0;
    while (!(0x80000000 & input) && i < 32)
    {
        i++;
        input = input << 1;
    }
    return i;
}
#elif defined(__GNUC__)
/* for some reason the unrolled version (below) is
 * actually faster than this. yay intel!
 */
static int count_leading_zeros(int input)
{
    return __builtin_clz(input);
}
#elif defined(_MSC_VER) && defined(_M_IX86)
static int count_leading_zeros(int input)
{
    int output = 0;
    if (!input) return 32;
    __asm
    {
        mov eax, input;
        mov edx, 0x1f;
        bsr ecx, eax;
        sub edx, ecx;
        mov output, edx;
    }
    return output;
}
#else
#warning using generic count leading zeroes. You may wish to write one for your CPU / compiler
static int count_leading_zeros(int input)
{
    int output = 0;
    int curbyte = 0;

    curbyte = input >> 24;
    if (curbyte) goto found;
    output += 8;

    curbyte = input >> 16;
    if (curbyte & 0xff) goto found;
    output += 8;

    curbyte = input >> 8;
    if (curbyte & 0xff) goto found;
    output += 8;

    curbyte = input;
    if (curbyte & 0xff) goto found;
    output += 8;

    return output;

found:
    if (!(curbyte & 0xf0))
    {
        output += 4;
    }
    else
        curbyte >>= 4;

    if (curbyte & 0x8)
        return output;
    if (curbyte & 0x4)
        return output + 1;
    if (curbyte & 0x2)
        return output + 2;
    if (curbyte & 0x1)
        return output + 3;

    /* shouldn't get here: */
    return output + 4;
}
#endif

#define RICE_THRESHOLD 8 // maximum number of bits for a rice prefix.

static int32_t entropy_decode_value(alac_file* alac,
                             int readSampleSize,
                             int k,
                             int rice_kmodifier_mask)
{
    int32_t x = 0; // decoded value

    // read x, number of 1s before 0 represent the rice value.
    while (x <= RICE_THRESHOLD && readbit(alac))
    {
        x++;
    }

    if (x > RICE_THRESHOLD)
    {
        // read the number from the bit stream (raw value)
        int32_t value;

        value = readbits(alac, readSampleSize);

        // mask value
        value &= (((uint32_t)0xffffffff) >> (32 - readSampleSize));

        x = value;
    }
    else
    {
        if (k != 1)
        {
            int extraBits = readbits(alac, k);

            // x = x * (2^k - 1)
            x *= (((1 << k) - 1) & rice_kmodifier_mask);

            if (extraBits > 1)
                x += extraBits - 1;
            else
                unreadbits(alac, 1);
        }
    }

    return x;
}

static void entropy_rice_decode(alac_file* alac,
                         int32_t* outputBuffer,
                         int outputSize,
                         int readSampleSize,
                         int rice_initialhistory,
                         int rice_kmodifier,
                         int rice_historymult,
                         int rice_kmodifier_mask)
{
    int             outputCount;
    int             history = rice_initialhistory;
    int             signModifier = 0;

    for (outputCount = 0; outputCount < outputSize; outputCount++)
    {
        int32_t     decodedValue;
        int32_t     finalValue;
        int32_t     k;

        k = 31 - rice_kmodifier - count_leading_zeros((history >> 9) + 3);

        if (k < 0) k += rice_kmodifier;
        else k = rice_kmodifier;

        // note: don't use rice_kmodifier_mask here (set mask to 0xFFFFFFFF)
        decodedValue = entropy_decode_value(alac, readSampleSize, k, 0xFFFFFFFF);

        decodedValue += signModifier;
        finalValue = (decodedValue + 1) / 2; // inc by 1 and shift out sign bit
        if (decodedValue & 1) // the sign is stored in the low bit
            finalValue *= -1;

        outputBuffer[outputCount] = finalValue;

        signModifier = 0;

        // update history
        history += (decodedValue * rice_historymult)
                - ((history * rice_historymult) >> 9);

        if (decodedValue > 0xFFFF)
            history = 0xFFFF;

        // special case, for compressed blocks of 0
        if ((history < 128) && (outputCount + 1 < outputSize))
        {
            int32_t     blockSize;

            signModifier = 1;

            k = count_leading_zeros(history) + ((history + 16) / 64) - 24;

            // note: blockSize is always 16bit
            blockSize = entropy_decode_value(alac, 16, k, rice_kmodifier_mask);

            // got blockSize 0s
            if (blockSize > 0)
            {
                memset(&outputBuffer[outputCount + 1], 0, blockSize * sizeof(*outputBuffer));
                outputCount += blockSize;
            }

            if (blockSize > 0xFFFF)
                signModifier = 0;

            history = 0;
        }
    }
}

#define SIGN_EXTENDED32(val, bits) ((val << (32 - bits)) >> (32 - bits))

#define SIGN_ONLY(v) \
                     ((v < 0) ? (-1) : \
                                ((v > 0) ? (1) : \
                                           (0)))

static void predictor_decompress_fir_adapt(int32_t *error_buffer,
                                           int32_t *buffer_out,
                                           int output_size,
                                           int readsamplesize,
                                           int16_t *predictor_coef_table,
                                           int predictor_coef_num,
                                           int predictor_quantitization)
{
    int i;

    /* first sample always copies */
    *buffer_out = *error_buffer;

    if (!predictor_coef_num)
    {
        if (output_size <= 1) return;
        memcpy(buffer_out+1, error_buffer+1, (output_size-1) * 4);
        return;
    }

    if (predictor_coef_num == 0x1f) /* 11111 - max value of predictor_coef_num */
    { /* second-best case scenario for fir decompression,
       * error describes a small difference from the previous sample only
       */
        if (output_size <= 1) return;
        for (i = 0; i < output_size - 1; i++)
        {
            int32_t prev_value;
            int32_t error_value;

            prev_value = buffer_out[i];
            error_value = error_buffer[i+1];
            buffer_out[i+1] = SIGN_EXTENDED32((prev_value + error_value), readsamplesize);
        }
        return;
    }

    /* read warm-up samples */
    if (predictor_coef_num > 0)
    {
        int i;
        for (i = 0; i < predictor_coef_num; i++)
        {
            int32_t val;

            val = buffer_out[i] + error_buffer[i+1];

            val = SIGN_EXTENDED32(val, readsamplesize);

            buffer_out[i+1] = val;
        }
    }

#if 0
    /* 4 and 8 are very common cases (the only ones i've seen). these
     * should be unrolled and optimised
     */
    if (predictor_coef_num == 4)
    {
        /* FIXME: optimised general case */
        return;
    }

    if (predictor_coef_table == 8)
    {
        /* FIXME: optimised general case */
        return;
    }
#endif


    /* general case */
    if (predictor_coef_num > 0)
    {
        for (i = predictor_coef_num + 1;
             i < output_size;
             i++)
        {
            int j;
            int sum = 0;
            int outval;
            int error_val = error_buffer[i];

            for (j = 0; j < predictor_coef_num; j++)
            {
                sum += (buffer_out[predictor_coef_num-j] - buffer_out[0]) *
                       predictor_coef_table[j];
            }

            outval = (1 << (predictor_quantitization-1)) + sum;
            outval = outval >> predictor_quantitization;
            outval = outval + buffer_out[0] + error_val;
            outval = SIGN_EXTENDED32(outval, readsamplesize);

            buffer_out[predictor_coef_num+1] = outval;

            if (error_val > 0)
            {
                int predictor_num = predictor_coef_num - 1;

                while (predictor_num >= 0 && error_val > 0)
                {
                    int val = buffer_out[0] - buffer_out[predictor_coef_num - predictor_num];
                    int sign = SIGN_ONLY(val);

                    predictor_coef_table[predictor_num] -= sign;

                    val *= sign; /* absolute value */

                    error_val -= ((val >> predictor_quantitization) *
                                  (predictor_coef_num - predictor_num));

                    predictor_num--;
                }
            }
            else if (error_val < 0)
            {
                int predictor_num = predictor_coef_num - 1;

                while (predictor_num >= 0 && error_val < 0)
                {
                    int val = buffer_out[0] - buffer_out[predictor_coef_num - predictor_num];
                    int sign = - SIGN_ONLY(val);

                    predictor_coef_table[predictor_num] -= sign;

                    val *= sign; /* neg value */

                    error_val -= ((val >> predictor_quantitization) *
                                  (predictor_coef_num - predictor_num));

                    predictor_num--;
                }
            }

            buffer_out++;
        }
    }
}

static void deinterlace_16(int32_t *buffer_a, int32_t *buffer_b,
                    int16_t *buffer_out,
                    int numchannels, int numsamples,
                    uint8_t interlacing_shift,
                    uint8_t interlacing_leftweight)
{
    int i;
    if (numsamples <= 0) return;

    /* weighted interlacing */
    if (interlacing_leftweight)
    {
        for (i = 0; i < numsamples; i++)
        {
            int32_t difference, midright;
            int16_t left;
            int16_t right;

            midright = buffer_a[i];
            difference = buffer_b[i];


            right = midright - ((difference * interlacing_leftweight) >> interlacing_shift);
            left = right + difference;

            /* output is always little endian */
            if (host_bigendian)
            {
                _Swap16(left);
                _Swap16(right);
            }

            buffer_out[i*numchannels] = left;
            buffer_out[i*numchannels + 1] = right;
        }

        return;
    }

    /* otherwise basic interlacing took place */
    for (i = 0; i < numsamples; i++)
    {
        int16_t left, right;

        left = buffer_a[i];
        right = buffer_b[i];

        /* output is always little endian */
        if (host_bigendian)
        {
            _Swap16(left);
            _Swap16(right);
        }

        buffer_out[i*numchannels] = left;
        buffer_out[i*numchannels + 1] = right;
    }
}

static void deinterlace_24(int32_t *buffer_a, int32_t *buffer_b,
                    int uncompressed_bytes,
                    int32_t *uncompressed_bytes_buffer_a, int32_t *uncompressed_bytes_buffer_b,
                    void *buffer_out,
                    int numchannels, int numsamples,
                    uint8_t interlacing_shift,
                    uint8_t interlacing_leftweight)
{
    int i;
    if (numsamples <= 0) return;

    /* weighted interlacing */
    if (interlacing_leftweight)
    {
        for (i = 0; i < numsamples; i++)
        {
            int32_t difference, midright;
            int32_t left;
            int32_t right;

            midright = buffer_a[i];
            difference = buffer_b[i];

            right = midright - ((difference * interlacing_leftweight) >> interlacing_shift);
            left = right + difference;

            if (uncompressed_bytes)
            {
                uint32_t mask = ~(0xFFFFFFFF << (uncompressed_bytes * 8));
                left <<= (uncompressed_bytes * 8);
                right <<= (uncompressed_bytes * 8);

                left |= uncompressed_bytes_buffer_a[i] & mask;
                right |= uncompressed_bytes_buffer_b[i] & mask;
            }

            ((uint8_t*)buffer_out)[i * numchannels * 3] = (left) & 0xFF;
            ((uint8_t*)buffer_out)[i * numchannels * 3 + 1] = (left >> 8) & 0xFF;
            ((uint8_t*)buffer_out)[i * numchannels * 3 + 2] = (left >> 16) & 0xFF;

            ((uint8_t*)buffer_out)[i * numchannels * 3 + 3] = (right) & 0xFF;
            ((uint8_t*)buffer_out)[i * numchannels * 3 + 4] = (right >> 8) & 0xFF;
            ((uint8_t*)buffer_out)[i * numchannels * 3 + 5] = (right >> 16) & 0xFF;
        }

        return;
    }

    /* otherwise basic interlacing took place */
    for (i = 0; i < numsamples; i++)
    {
        int32_t left, right;

        left = buffer_a[i];
        right = buffer_b[i];

        if (uncompressed_bytes)
        {
            uint32_t mask = ~(0xFFFFFFFF << (uncompressed_bytes * 8));
            left <<= (uncompressed_bytes * 8);
            right <<= (uncompressed_bytes * 8);

            left |= uncompressed_bytes_buffer_a[i] & mask;
            right |= uncompressed_bytes_buffer_b[i] & mask;
        }

        ((uint8_t*)buffer_out)[i * numchannels * 3] = (left) & 0xFF;
        ((uint8_t*)buffer_out)[i * numchannels * 3 + 1] = (left >> 8) & 0xFF;
        ((uint8_t*)buffer_out)[i * numchannels * 3 + 2] = (left >> 16) & 0xFF;

        ((uint8_t*)buffer_out)[i * numchannels * 3 + 3] = (right) & 0xFF;
        ((uint8_t*)buffer_out)[i * numchannels * 3 + 4] = (right >> 8) & 0xFF;
        ((uint8_t*)buffer_out)[i * numchannels * 3 + 5] = (right >> 16) & 0xFF;

    }

}

void alac_decode_frame(alac_file *alac,
                       unsigned char *inbuffer,
                       void *outbuffer, int *outputsize)
{
    int channels;
    int32_t outputsamples = alac->setinfo_max_samples_per_frame;

    /* setup the stream */
    alac->input_buffer = inbuffer;
    alac->input_buffer_bitaccumulator = 0;

    channels = readbits(alac, 3);

    *outputsize = outputsamples * alac->bytespersample;

    switch(channels)
    {
    case 0: /* 1 channel */
    {
        int hassize;
        int isnotcompressed;
        int readsamplesize;

        int uncompressed_bytes;
        int ricemodifier;

        /* 2^result = something to do with output waiting.
         * perhaps matters if we read > 1 frame in a pass?
         */
        readbits(alac, 4);

        readbits(alac, 12); /* unknown, skip 12 bits */

        hassize = readbits(alac, 1); /* the output sample size is stored soon */

        uncompressed_bytes = readbits(alac, 2); /* number of bytes in the (compressed) stream that are not compressed */

        isnotcompressed = readbits(alac, 1); /* whether the frame is compressed */

        if (hassize)
        {
            /* now read the number of samples,
             * as a 32bit integer */
            outputsamples = readbits(alac, 32);
            *outputsize = outputsamples * alac->bytespersample;
        }

        readsamplesize = alac->setinfo_sample_size - (uncompressed_bytes * 8);

        if (!isnotcompressed)
        { /* so it is compressed */
            int16_t predictor_coef_table[32];
            int predictor_coef_num;
            int prediction_type;
            int prediction_quantitization;
            int i;

            /* skip 16 bits, not sure what they are. seem to be used in
             * two channel case */
            readbits(alac, 8);
            readbits(alac, 8);

            prediction_type = readbits(alac, 4);
            prediction_quantitization = readbits(alac, 4);

            ricemodifier = readbits(alac, 3);
            predictor_coef_num = readbits(alac, 5);

            /* read the predictor table */
            for (i = 0; i < predictor_coef_num; i++)
            {
                predictor_coef_table[i] = (int16_t)readbits(alac, 16);
            }

            if (uncompressed_bytes)
            {
                int i;
                for (i = 0; i < outputsamples; i++)
                {
                    alac->uncompressed_bytes_buffer_a[i] = readbits(alac, uncompressed_bytes * 8);
                }
            }

            entropy_rice_decode(alac,
                                alac->predicterror_buffer_a,
                                outputsamples,
                                readsamplesize,
                                alac->setinfo_rice_initialhistory,
                                alac->setinfo_rice_kmodifier,
                                ricemodifier * alac->setinfo_rice_historymult / 4,
                                (1 << alac->setinfo_rice_kmodifier) - 1);

            if (prediction_type == 0)
            { /* adaptive fir */
                predictor_decompress_fir_adapt(alac->predicterror_buffer_a,
                                               alac->outputsamples_buffer_a,
                                               outputsamples,
                                               readsamplesize,
                                               predictor_coef_table,
                                               predictor_coef_num,
                                               prediction_quantitization);
            }
            else
            {
                fprintf(stderr, "FIXME: unhandled prediction type for compressed case: %i\n", prediction_type);
                /* i think the only other prediction type (or perhaps this is just a
                 * boolean?) runs adaptive fir twice.. like:
                 * predictor_decompress_fir_adapt(predictor_error, tempout, ...)
                 * predictor_decompress_fir_adapt(predictor_error, outputsamples ...)
                 * little strange..
                 */
            }

        }
        else
        { /* not compressed, easy case */
            if (alac->setinfo_sample_size <= 16)
            {
                int i;
                for (i = 0; i < outputsamples; i++)
                {
                    int32_t audiobits = readbits(alac, alac->setinfo_sample_size);

                    audiobits = SIGN_EXTENDED32(audiobits, alac->setinfo_sample_size);

                    alac->outputsamples_buffer_a[i] = audiobits;
                }
            }
            else
            {
                int i;
                for (i = 0; i < outputsamples; i++)
                {
                    int32_t audiobits;

                    audiobits = readbits(alac, 16);
                    /* special case of sign extension..
                     * as we'll be ORing the low 16bits into this */
                    audiobits = audiobits << (alac->setinfo_sample_size - 16);
                    audiobits |= readbits(alac, alac->setinfo_sample_size - 16);
                    audiobits = SignExtend24(audiobits);

                    alac->outputsamples_buffer_a[i] = audiobits;
                }
            }
            uncompressed_bytes = 0; // always 0 for uncompressed
        }

        switch(alac->setinfo_sample_size)
        {
        case 16:
        {
            int i;
            for (i = 0; i < outputsamples; i++)
            {
                int16_t sample = alac->outputsamples_buffer_a[i];
                if (host_bigendian)
                    _Swap16(sample);
                ((int16_t*)outbuffer)[i * alac->numchannels] = sample;
            }
            break;
        }
        case 24:
        {
            int i;
            for (i = 0; i < outputsamples; i++)
            {
                int32_t sample = alac->outputsamples_buffer_a[i];

                if (uncompressed_bytes)
                {
                    uint32_t mask;
                    sample = sample << (uncompressed_bytes * 8);
                    mask = ~(0xFFFFFFFF << (uncompressed_bytes * 8));
                    sample |= alac->uncompressed_bytes_buffer_a[i] & mask;
                }

                ((uint8_t*)outbuffer)[i * alac->numchannels * 3] = (sample) & 0xFF;
                ((uint8_t*)outbuffer)[i * alac->numchannels * 3 + 1] = (sample >> 8) & 0xFF;
                ((uint8_t*)outbuffer)[i * alac->numchannels * 3 + 2] = (sample >> 16) & 0xFF;
            }
            break;
        }
        case 20:
        case 32:
            fprintf(stderr, "FIXME: unimplemented sample size %i\n", alac->setinfo_sample_size);
            break;
        default:
            break;
        }
        break;
    }
    case 1: /* 2 channels */
    {
        int hassize;
        int isnotcompressed;
        int readsamplesize;

        int uncompressed_bytes;

        uint8_t interlacing_shift;
        uint8_t interlacing_leftweight;

        /* 2^result = something to do with output waiting.
         * perhaps matters if we read > 1 frame in a pass?
         */
        readbits(alac, 4);

        readbits(alac, 12); /* unknown, skip 12 bits */

        hassize = readbits(alac, 1); /* the output sample size is stored soon */

        uncompressed_bytes = readbits(alac, 2); /* the number of bytes in the (compressed) stream that are not compressed */

        isnotcompressed = readbits(alac, 1); /* whether the frame is compressed */

        if (hassize)
        {
            /* now read the number of samples,
             * as a 32bit integer */
            outputsamples = readbits(alac, 32);
            *outputsize = outputsamples * alac->bytespersample;
        }

        readsamplesize = alac->setinfo_sample_size - (uncompressed_bytes * 8) + 1;

        if (!isnotcompressed)
        { /* compressed */
            int16_t predictor_coef_table_a[32];
            int predictor_coef_num_a;
            int prediction_type_a;
            int prediction_quantitization_a;
            int ricemodifier_a;

            int16_t predictor_coef_table_b[32];
            int predictor_coef_num_b;
            int prediction_type_b;
            int prediction_quantitization_b;
            int ricemodifier_b;

            int i;

            interlacing_shift = readbits(alac, 8);
            interlacing_leftweight = readbits(alac, 8);

            /******** channel 1 ***********/
            prediction_type_a = readbits(alac, 4);
            prediction_quantitization_a = readbits(alac, 4);

            ricemodifier_a = readbits(alac, 3);
            predictor_coef_num_a = readbits(alac, 5);

            /* read the predictor table */
            for (i = 0; i < predictor_coef_num_a; i++)
            {
                predictor_coef_table_a[i] = (int16_t)readbits(alac, 16);
            }

            /******** channel 2 *********/
            prediction_type_b = readbits(alac, 4);
            prediction_quantitization_b = readbits(alac, 4);

            ricemodifier_b = readbits(alac, 3);
            predictor_coef_num_b = readbits(alac, 5);

            /* read the predictor table */
            for (i = 0; i < predictor_coef_num_b; i++)
            {
                predictor_coef_table_b[i] = (int16_t)readbits(alac, 16);
            }

            /*********************/
            if (uncompressed_bytes)
            { /* see mono case */
                int i;
                for (i = 0; i < outputsamples; i++)
                {
                    alac->uncompressed_bytes_buffer_a[i] = readbits(alac, uncompressed_bytes * 8);
                    alac->uncompressed_bytes_buffer_b[i] = readbits(alac, uncompressed_bytes * 8);
                }
            }

            /* channel 1 */
            entropy_rice_decode(alac,
                                alac->predicterror_buffer_a,
                                outputsamples,
                                readsamplesize,
                                alac->setinfo_rice_initialhistory,
                                alac->setinfo_rice_kmodifier,
                                ricemodifier_a * alac->setinfo_rice_historymult / 4,
                                (1 << alac->setinfo_rice_kmodifier) - 1);

            if (prediction_type_a == 0)
            { /* adaptive fir */
                predictor_decompress_fir_adapt(alac->predicterror_buffer_a,
                                               alac->outputsamples_buffer_a,
                                               outputsamples,
                                               readsamplesize,
                                               predictor_coef_table_a,
                                               predictor_coef_num_a,
                                               prediction_quantitization_a);
            }
            else
            { /* see mono case */
                fprintf(stderr, "FIXME: unhandled prediction type on channel 1: %i\n", prediction_type_a);
            }

            /* channel 2 */
            entropy_rice_decode(alac,
                                alac->predicterror_buffer_b,
                                outputsamples,
                                readsamplesize,
                                alac->setinfo_rice_initialhistory,
                                alac->setinfo_rice_kmodifier,
                                ricemodifier_b * alac->setinfo_rice_historymult / 4,
                                (1 << alac->setinfo_rice_kmodifier) - 1);

            if (prediction_type_b == 0)
            { /* adaptive fir */
                predictor_decompress_fir_adapt(alac->predicterror_buffer_b,
                                               alac->outputsamples_buffer_b,
                                               outputsamples,
                                               readsamplesize,
                                               predictor_coef_table_b,
                                               predictor_coef_num_b,
                                               prediction_quantitization_b);
            }
            else
            {
                fprintf(stderr, "FIXME: unhandled prediction type on channel 2: %i\n", prediction_type_b);
            }
        }
        else
        { /* not compressed, easy case */
            if (alac->setinfo_sample_size <= 16)
            {
                int i;
                for (i = 0; i < outputsamples; i++)
                {
                    int32_t audiobits_a, audiobits_b;

                    audiobits_a = readbits(alac, alac->setinfo_sample_size);
                    audiobits_b = readbits(alac, alac->setinfo_sample_size);

                    audiobits_a = SIGN_EXTENDED32(audiobits_a, alac->setinfo_sample_size);
                    audiobits_b = SIGN_EXTENDED32(audiobits_b, alac->setinfo_sample_size);

                    alac->outputsamples_buffer_a[i] = audiobits_a;
                    alac->outputsamples_buffer_b[i] = audiobits_b;
                }
            }
            else
            {
                int i;
                for (i = 0; i < outputsamples; i++)
                {
                    int32_t audiobits_a, audiobits_b;

                    audiobits_a = readbits(alac, 16);
                    audiobits_a = audiobits_a << (alac->setinfo_sample_size - 16);
                    audiobits_a |= readbits(alac, alac->setinfo_sample_size - 16);
                    audiobits_a = SignExtend24(audiobits_a);

                    audiobits_b = readbits(alac, 16);
                    audiobits_b = audiobits_b << (alac->setinfo_sample_size - 16);
                    audiobits_b |= readbits(alac, alac->setinfo_sample_size - 16);
                    audiobits_b = SignExtend24(audiobits_b);

                    alac->outputsamples_buffer_a[i] = audiobits_a;
                    alac->outputsamples_buffer_b[i] = audiobits_b;
                }
            }
            uncompressed_bytes = 0; // always 0 for uncompressed
            interlacing_shift = 0;
            interlacing_leftweight = 0;
        }

        switch(alac->setinfo_sample_size)
        {
        case 16:
        {
            deinterlace_16(alac->outputsamples_buffer_a,
                           alac->outputsamples_buffer_b,
                           (int16_t*)outbuffer,
                           alac->numchannels,
                           outputsamples,
                           interlacing_shift,
                           interlacing_leftweight);
            break;
        }
        case 24:
        {
            deinterlace_24(alac->outputsamples_buffer_a,
                           alac->outputsamples_buffer_b,
                           uncompressed_bytes,
                           alac->uncompressed_bytes_buffer_a,
                           alac->uncompressed_bytes_buffer_b,
                           (int16_t*)outbuffer,
                           alac->numchannels,
                           outputsamples,
                           interlacing_shift,
                           interlacing_leftweight);
            break;
        }
        case 20:
        case 32:
            fprintf(stderr, "FIXME: unimplemented sample size %i\n", alac->setinfo_sample_size);
            break;
        default:
            break;
        }

        break;
    }
    }
}

alac_file *alac_create(int samplesize, int numchannels)
{
    alac_file *newfile = malloc(sizeof(alac_file));

    memset(newfile, 0, sizeof(alac_file));

    newfile->samplesize = samplesize;
    newfile->numchannels = numchannels;
    newfile->bytespersample = (samplesize / 8) * numchannels;

    return newfile;
}

