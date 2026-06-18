/*
 * TLV helpers are adapted from ESP homekit:
 *    <https://github.com/maximkulkin/esp-homekit>
 *
 * The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "pair-tlv.h"
#include "pair-internal.h"


static int
tlv_add_value_(pair_tlv_values_t *values, uint8_t type, uint8_t *value, size_t size) {
    pair_tlv_t *tlv = malloc(sizeof(pair_tlv_t));
    if (!tlv) {
        return PAIR_TLV_ERROR_MEMORY;
    }
    tlv->type = type;
    tlv->size = size;
    tlv->value = value;
    tlv->next = NULL;

    if (!values->head) {
        values->head = tlv;
    } else {
        pair_tlv_t *t = values->head;
        while (t->next) {
            t = t->next;
        }
        t->next = tlv;
    }

    return 0;
}


pair_tlv_values_t *
pair_tlv_new() {
    pair_tlv_values_t *values = malloc(sizeof(pair_tlv_values_t));
    if (!values)
        return NULL;

    values->head = NULL;
    return values;
}

void
pair_tlv_free(pair_tlv_values_t *values) {
    if (!values)
        return;

    pair_tlv_t *t = values->head;
    while (t) {
        pair_tlv_t *t2 = t;
        t = t->next;
        if (t2->value)
            free(t2->value);
        free(t2);
    }
    free(values);
}

int
pair_tlv_add_value(pair_tlv_values_t *values, uint8_t type, const uint8_t *value, size_t size) {
    uint8_t *data = NULL;
    int ret;
    if (size) {
        data = malloc(size);
        if (!data) {
            return PAIR_TLV_ERROR_MEMORY;
        }
        memcpy(data, value, size);
    }
    ret = tlv_add_value_(values, type, data, size);
    if (ret < 0)
        free(data);
    return ret;
}

pair_tlv_t *
pair_tlv_get_value(const pair_tlv_values_t *values, uint8_t type) {
    pair_tlv_t *t = values->head;
    while (t) {
        if (t->type == type)
            return t;
        t = t->next;
    }
    return NULL;
}

int
pair_tlv_format(const pair_tlv_values_t *values, uint8_t *buffer, size_t *size) {
    size_t required_size = 0;
    pair_tlv_t *t = values->head;
    while (t) {
        required_size += t->size + 2 * ((t->size + 254) / 255);
        t = t->next;
    }

    if (*size < required_size) {
        *size = required_size;
        return PAIR_TLV_ERROR_INSUFFICIENT_SIZE;
    }

    *size = required_size;

    t = values->head;
    while (t) {
        uint8_t *data = t->value;
        if (!t->size) {
            buffer[0] = t->type;
            buffer[1] = 0;
            buffer += 2;
            t = t->next;
            continue;
        }

        size_t remaining = t->size;

        while (remaining) {
            buffer[0] = t->type;
            size_t chunk_size = (remaining > 255) ? 255 : remaining;
            buffer[1] = chunk_size;
            memcpy(&buffer[2], data, chunk_size);
            remaining -= chunk_size;
            buffer += chunk_size + 2;
            data += chunk_size;
        }

        t = t->next;
    }

    return 0;
}

int
pair_tlv_parse(const uint8_t *buffer, size_t length, pair_tlv_values_t *values) {
    size_t i = 0;
    int ret;
    while (i < length) {
        if (i + 2 > length)
            return PAIR_TLV_ERROR_INSUFFICIENT_SIZE;

        uint8_t type = buffer[i];
        size_t size = 0;
        uint8_t *data = NULL;

        // scan TLVs to accumulate total size of subsequent TLVs with same type (chunked data)
        size_t j = i;
        while (j + 1 < length && buffer[j] == type && buffer[j+1] == 255) {
            size_t chunk_size = buffer[j+1];
            size += chunk_size;
            j += chunk_size + 2;
        }
        if (j + 1 < length && buffer[j] == type) {
            size_t chunk_size = buffer[j+1];
            size += chunk_size;
        }

        // allocate memory to hold all pieces of chunked data and copy data there
        if (size != 0) {
            data = malloc(size);
            if (!data)
                return PAIR_TLV_ERROR_MEMORY;

            uint8_t *p = data;

            size_t remaining = size;
            while (remaining) {
                if (i + 2 > length) {
                    free(data);
                    return PAIR_TLV_ERROR_INSUFFICIENT_SIZE;
                }
                size_t chunk_size = buffer[i+1];
                if (chunk_size > length - i - 2) {
                    free(data);
                    return PAIR_TLV_ERROR_INSUFFICIENT_SIZE;
                }
                memcpy(p, &buffer[i+2], chunk_size);
                p += chunk_size;
                i += chunk_size + 2;
                remaining -= chunk_size;
            }
        } else {
            i += 2;
        }

        ret = tlv_add_value_(values, type, data, size);
        if (ret < 0) {
            free(data);
            return ret;
        }
    }

    return 0;
}

#ifdef DEBUG_PAIR
void
pair_tlv_debug(const pair_tlv_values_t *values)
{
  printf("Received TLV values\n");
  for (pair_tlv_t *t=values->head; t; t=t->next)
    {
      printf("Type %d value (%zu bytes): \n", t->type, t->size);
      hexdump("", t->value, t->size);
    }
}
#endif

