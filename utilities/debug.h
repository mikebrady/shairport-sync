/*
MIT License

Copyright (c) 2023--2025 Mike Brady 4265913+mikebrady@users.noreply.github.com

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

#ifndef __DEBUG_H
#define __DEBUG_H

#include <stddef.h>

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

// four level debug message utility giving file and line, total elapsed time,
// interval time warn / inform / debug / die calls.

// level 0 is no messages, level 3 is most messages
EXTERNC void debug_init(int level, int show_elapsed_time, int show_relative_time,
                        int show_file_and_line);

EXTERNC int debug_level();
EXTERNC void set_debug_level(int level);
EXTERNC void increase_debug_level();
EXTERNC void decrease_debug_level();
EXTERNC int get_show_elapsed_time();
EXTERNC void set_show_elapsed_time(int setting);
EXTERNC int get_show_relative_timel();
EXTERNC void set_show_relative_time(int setting);
EXTERNC int get_show_file_and_line();
EXTERNC void set_show_file_and_line(int setting);

#if defined(__GNUC__) || defined(__clang__)
#define PRINTF_LIKE(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define PRINTF_LIKE(fmt, args)
#endif

// Function declarations with printf-style format checking
EXTERNC void _die(const char *filename, const int linenumber, const char *format, ...) PRINTF_LIKE(3,4);
EXTERNC void _warn(const char *filename, const int linenumber, const char *format, ...) PRINTF_LIKE(3,4);
EXTERNC void _inform(const char *filename, const int linenumber, const char *format, ...) PRINTF_LIKE(3,4);
EXTERNC void _debug(const char *filename, const int linenumber, int level, const char *format, ...) PRINTF_LIKE(4,5);
EXTERNC void _debug_print_buffer(const char *thefilename, const int linenumber, int level, void *buf,
                                size_t buf_len); // not printf-style, no change needed
#define die(...) _die(__FILE__, __LINE__, __VA_ARGS__)
#define debug(...) _debug(__FILE__, __LINE__, __VA_ARGS__)
#define warn(...) _warn(__FILE__, __LINE__, __VA_ARGS__)
#define inform(...) _inform(__FILE__, __LINE__, __VA_ARGS__)
#define debug_print_buffer(...) _debug_print_buffer(__FILE__, __LINE__, __VA_ARGS__)

#endif /* __DEBUG_H */