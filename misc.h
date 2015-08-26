/*
 * misc.h
 * Copyright (C) 2013 National University of Singapore
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __MISC_H
#define __MISC_H

/*
 * This module contains anything that does not fit anywhere else.
 */

#include "word.h"

/*
 * Some useful macros.
 */
#define NO_RETURN               __attribute__((__noreturn__))
#define ALWAYS_INLINE           __attribute__((__always_inline__))
#define NO_INLINE               __attribute__((noinline))
#define PURE                    __attribute__((__const__))

#define VA_ARGS_LENGTH(type, ...)                                           \
    (sizeof((type []){(type)0, ##__VA_ARGS__}) / sizeof(type) - 1)

#define STRING_HELPER(s)        #s
#define STRING(s)               STRING_HELPER(s)

/*
 * Which system are we?
 */
#ifdef __MINGW32__
#define WINDOWS     1
#endif

#ifdef __APPLE__
#define MACOSX      1
#endif

#if !defined __MINGW32__ && !defined __APPLE__
#define LINUX       1
#endif

/*
 * OS-dependent init.
 */
extern void os_init(void);

/*
 * Word comparison.
 */
extern int_t word_compare(word_t a, word_t b);

/*
 * Int comparison.
 */
extern int_t int_compare(int_t a, int_t b);

/*
 * strcmp() wrapper.
 */
extern int_t strcmp_compare(const char *a, const char *b);

/*
 * Greatest Common Divisor.
 */
extern int64_t gcd(int64_t a, int64_t b);

/*
 * Large buffer functions.
 */
extern void *buffer_alloc(size_t size);
extern void buffer_free(void *buf, size_t size);

#endif      /* __MISC_H */
