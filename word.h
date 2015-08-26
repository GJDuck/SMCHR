/*
 * word.h
 * Copyright (C) 2011 National University of Singapore
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
#ifndef __WORD_H
#define __WORD_H

#include <math.h>       // for double_t
#include <stdint.h>

#include "gc.h"

typedef uintptr_t word_t;
typedef intptr_t  sword_t;
typedef double    fword_t;

typedef word_t    bool_t;
typedef word_t    uint_t;
typedef sword_t   int_t;
// typedef fword_t   double_t;

#define WORD_SIZE           sizeof(word_t)
#define WORD_BITS           (WORD_SIZE*8)
#define WORD_TAG_BITS       4
#define WORD_TAG_MASK       (GC_ALIGNMENT-1)
#define WORD_TAG_INCR       GC_ALIGNMENT

#define WORD_FORMAT_D       "%ld"
#define WORD_FORMAT_U       "%lu"
#define WORD_FORMAT_X       "%lx"

/*
 * Word to floating-point.
 */
union word2double_u
{
    word_t word;
    double_t num;
};
static inline double_t word_getdouble(word_t w)
{
    union word2double_u u;
    u.word = w;
    return u.num;
}
static inline word_t word_makedouble(double_t n)
{
    union word2double_u u;
    u.num = n;
    return u.word;
}

/*
 * Tag functions.
 */
static inline word_t word_settag(word_t word, word_t tag)
{
    return (word_t)gc_settag((void *)word, (uintptr_t)tag);
}
static inline word_t word_gettag(word_t word)
{
    return (word_t)gc_gettag((void *)word);
}
static inline word_t word_untag(word_t word, word_t tag)
{
    return (word_t)gc_deltag((void *)word, (intptr_t)tag);
}
static inline word_t word_striptag(word_t word)
{
    return (word_t)gc_striptag((void *)word);
}

#define WORD_SIZEOF(t)      \
    ((sizeof(t)+sizeof(word_t)-1)/sizeof(word_t))

#endif      /* __TREE_H */
