/*
 * term.h
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
#ifndef __TERM_H
#define __TERM_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "misc.h"
#include "word.h"

typedef word_t term_t;

/****************************************************************************/
/* TAGS and TYPES                                                           */
/****************************************************************************/

enum termtag_e
{
    TAG_VAR     = 0,
    TAG_BOOL    = 1,
    TAG_ATOM    = 2,
    TAG_NUM     = 3,        // Must be 3 (for useful rounding)
    TAG_STR     = 4,
    TAG_FUNC    = 5,
    TAG_NIL     = 6,
    TAG_FOREIGN = 7,
};
#define TAG_MAX             8
typedef enum termtag_e termtag_t;

enum termtype_e
{
    VAR     = TAG_VAR,
    BOOL    = TAG_BOOL,
    ATOM    = TAG_ATOM,
    NUM     = TAG_NUM,
    STR     = TAG_STR,
    FUNC    = TAG_FUNC,
    NIL     = TAG_NIL,
    FOREIGN = TAG_FOREIGN,
};
#define TYPE_MAX            TAG_MAX
typedef enum termtype_e termtype_t;
typedef termtype_t type_t;

/*
 * Get the type of a term.
 */
static inline type_t term_get_type(term_t t)
{
    return (type_t)word_gettag((word_t)t);
}
extern const char *term_get_type_name(type_t type);
#define type(t)             term_get_type(t)
#define type_name(type)     term_get_type_name(type)

/***************************************************************************/
/* NIL                                                                     */
/***************************************************************************/

#define TERM_NIL            term_nil(make_nil())

typedef word_t nil_t;

static inline nil_t ALWAYS_INLINE term_build_nil(void)
{
    return (nil_t)0;
}
static inline nil_t ALWAYS_INLINE term_get_nil(term_t t)
{
    return term_build_nil();
}
static inline nil_t ALWAYS_INLINE term_make_nil(nil_t n)
{
    return (term_t)word_settag(term_build_nil(), TAG_NIL);
}
extern int_t term_compare_nil(nil_t na, nil_t nb);

#define make_nil()          term_build_nil()
#define nil(t)              term_get_nil(t)
#define term_nil(n)         term_make_nil(n)
#define compare_nil         term_compare_nil

/****************************************************************************/
/* BOOLEANS                                                                 */
/****************************************************************************/

#define BOOL_SHIFT          WORD_TAG_BITS
#define TERM_TRUE           term_boolean(make_boolean(true))
#define TERM_FALSE          term_boolean(make_boolean(false))

static inline bool_t ALWAYS_INLINE term_build_bool(bool b)
{
    return (bool_t)b;
}
static inline bool_t ALWAYS_INLINE term_get_bool(term_t t)
{
    return (bool_t)(word_untag((word_t)t, TAG_BOOL) >> BOOL_SHIFT);
}
static inline term_t ALWAYS_INLINE term_make_bool(bool_t b)
{
    return (term_t)word_settag((word_t)b << BOOL_SHIFT, TAG_BOOL);
}
extern int_t term_compare_bool(bool_t ba, bool_t bb);

#define make_boolean(b)     term_build_bool(b)
#define boolean(t)          term_get_bool(t)
#define term_boolean(b)     term_make_bool(b)
#define compare_boolean     term_compare_bool

/****************************************************************************/
/* NUMBERS                                                                  */
/****************************************************************************/

typedef double_t num_t;
#define NUM_SIGN_MASK       0x8000000000000000ull
#define NUM_SIGN_SHIFT      63
#define NUM_EXPONENT_MASK   0x7FF0000000000000ull
#define NUM_EXPONENT_SHIFT  52
#define NUM_FRACTION_MASK   0x000FFFFFFFFFFFFFull
#define NUM_UNDEFINED       0x7FF8000000000000ull   // QNaN
#define inf                 (1.0/0.0)
#define NUM_INT_MAX         9007199254740992.0
#define TERM_INF            ((term_t)word_settag(NUM_EXPONENT_MASK, TAG_NUM))

static inline num_t ALWAYS_INLINE term_build_num(double n)
{
    return (num_t)n;
}
static inline num_t ALWAYS_INLINE term_build_int(int64_t i)
{
    return (num_t)i;
}
static inline num_t ALWAYS_INLINE term_get_num(term_t t)
{
    union word2double_u u;
    u.word = word_untag((word_t)t, TAG_NUM);
    return u.num;
}
extern term_t term_make_num(num_t n);
static inline term_t ALWAYS_INLINE term_make_int(num_t n)
{
    word_t w = word_makedouble(n);
    return (term_t)word_settag(w, TAG_NUM);
}
extern int_t term_compare_num(num_t na, num_t nb);

#define make_num(n)         term_build_num(n)
#define make_int(n)         term_build_int(n)
#define num(t)              term_get_num(t)
#define term_num(n)         term_make_num(n)
#define term_int(i)         term_make_int(i)
#define compare_num         term_compare_num

/***************************************************************************/
/* STRINGS                                                                 */
/***************************************************************************/

struct str_s
{
    size_t len;             // The string's length.
    char   chars[];         // The string's characters.
};
typedef struct str_s *str_t;

extern str_t term_build_string(const char *s);
static inline str_t ALWAYS_INLINE term_get_string(term_t t)
{
    return (str_t)word_untag((word_t)t, TAG_STR);
}
static inline term_t ALWAYS_INLINE term_make_string(str_t str)
{
    return (term_t)word_settag((word_t)str, TAG_STR);
}
extern int_t term_compare_string(str_t sa, str_t sb);

#define make_string(s)      term_build_string(s)
#define string(t)           term_get_string(t)
#define term_string(s)      term_make_string(s)
#define compare_string      term_compare_string

/***************************************************************************/
/* ATOMS                                                                   */
/***************************************************************************/

typedef word_t atom_t;
#define ATOM_NIL            ((atom_t)0)
#define ATOM_NAME_MASK      0xFFFFFFFF00000000ull
#define ATOM_NAME_SHIFT     32
#define ATOM_ID_MASK        0x00000000FFFF0000ull
#define ATOM_ID_SHIFT       16
#define ATOM_ARITY_MASK     0x000000000000FFF0ull
#define ATOM_ARITY_SHIFT    4

#define ATOM_MAX_ARITY      (ATOM_ARITY_MASK >> ATOM_ARITY_SHIFT)
#define ATOM_MAX_ID         (ATOM_ID_MASK >> ATOM_ID_SHIFT)

extern atom_t term_build_atom(const char *name, size_t arity);
static inline atom_t ALWAYS_INLINE term_get_atom(term_t t)
{
    return (atom_t)word_untag((word_t)t, TAG_ATOM);
}
static inline term_t ALWAYS_INLINE term_make_atom(atom_t a)
{
    return (atom_t)word_settag((word_t)a, TAG_ATOM);
}
extern int_t term_compare_atom(atom_t aa, atom_t ab);

static inline size_t ALWAYS_INLINE term_get_atom_arity(atom_t atom)
{
    return (size_t)((atom & ATOM_ARITY_MASK) >> ATOM_ARITY_SHIFT);
}
static inline atom_t ALWAYS_INLINE term_set_atom_arity(atom_t atom, size_t a)
{
    return (atom_t)((atom & ~ATOM_ARITY_MASK) |
        ((a << ATOM_ARITY_SHIFT) & ATOM_ARITY_MASK));
}
extern const char *term_get_atom_name(atom_t atom);

#define make_atom(n, a)     term_build_atom(n, a)
#define atom(t)             term_get_atom(t)
#define term_atom(a)        term_make_atom(a)
#define compare_atom        term_compare_atom

#define atom_arity(a)       term_get_atom_arity(a)
#define atom_name(a)        term_get_atom_name(a)
#define atom_set_arity(a, aty)                                              \
    term_set_atom_arity((a), (aty))

extern atom_t ATOM_NOT;
extern atom_t ATOM_AND;
extern atom_t ATOM_OR;
extern atom_t ATOM_IMPLIES;
extern atom_t ATOM_IFF;
extern atom_t ATOM_XOR;
extern atom_t ATOM_EQ;
extern atom_t ATOM_NEQ;
extern atom_t ATOM_LT;
extern atom_t ATOM_LEQ;
extern atom_t ATOM_GT;
extern atom_t ATOM_GEQ;
extern atom_t ATOM_NEG;
extern atom_t ATOM_ADD;
extern atom_t ATOM_SUB;
extern atom_t ATOM_MUL;
extern atom_t ATOM_DIV;

extern atom_t ATOM_INT_EQ;
extern atom_t ATOM_NIL_EQ;
extern atom_t ATOM_STR_EQ;
extern atom_t ATOM_ATOM_EQ;
extern atom_t ATOM_INT_EQ_C;
extern atom_t ATOM_NIL_EQ_C;
extern atom_t ATOM_STR_EQ_C;
extern atom_t ATOM_ATOM_EQ_C;
extern atom_t ATOM_INT_EQ_PLUS;
extern atom_t ATOM_INT_EQ_PLUS_C;
extern atom_t ATOM_INT_EQ_MUL;
extern atom_t ATOM_INT_EQ_MUL_C;
extern atom_t ATOM_INT_EQ_POW_C;
extern atom_t ATOM_INT_GT;
extern atom_t ATOM_INT_GT_C;

/***************************************************************************/
/* FUNCTORS                                                                */
/***************************************************************************/

struct func_s
{
    atom_t atom;            // The functor's atom.
    term_t args[];          // The functor's arguments.
};
typedef struct func_s *func_t;

extern func_t term_build_func(atom_t atom, ...);
extern func_t term_build_func_a(atom_t atom, term_t *args);
static inline func_t ALWAYS_INLINE term_get_func(term_t t)
{
    return (func_t)word_untag((word_t)t, TAG_FUNC);
}
static inline term_t ALWAYS_INLINE term_make_func(func_t f)
{
    return (term_t)word_settag((word_t)f, TAG_FUNC);
}
extern int_t term_compare_func(func_t fa, func_t fb);

#define make_func(a, ...)   term_build_func(a, ##__VA_ARGS__)
#define make_func_a(a, args)                                                \
    term_build_func_a((a), (args))
#define func(t)             term_get_func(t)
#define term_func(f)        term_make_func(f)
#define compare_func        term_compare_func
#define term(n, ...)                                                        \
    term_make_func(term_build_func(term_build_atom((n),                     \
        VA_ARGS_LENGTH(term_t, ## __VA_ARGS__)), ## __VA_ARGS__))
#define term_a(n, arity, args)                                              \
    term_make_func(term_build_func_a(term_build_atom((n), (arity)), (args)))

/***************************************************************************/
/* FOREIGN                                                                 */
/***************************************************************************/

typedef word_t foreign_t;

static inline foreign_t ALWAYS_INLINE term_build_foreign(word_t f)
{
    return (foreign_t)f;
}
static inline foreign_t ALWAYS_INLINE term_get_foreign(term_t t)
{
    return (foreign_t)word_untag((word_t)t, TAG_FOREIGN);
}
static inline term_t ALWAYS_INLINE term_make_foreign(foreign_t f)
{
    return (term_t)word_settag((word_t)f, TAG_FOREIGN);
}
extern int_t term_compare_foreign(foreign_t fa, foreign_t fb);

#define make_foreign(f)     term_build_foreign(f)
#define foreign(t)          term_get_foreign(t)
#define term_foreign(f)     term_make_foreign(f)
#define compare_foreign     term_compare_foreign

/****************************************************************************/
/* VARS                                                                     */
/****************************************************************************/

struct var_s
{
    const char *name;       // Variable's name.
};
typedef struct var_s *var_t;

extern var_t term_build_var(const char *name);
static inline var_t ALWAYS_INLINE term_get_var(term_t t)
{
    return (var_t)word_untag((word_t)t, TAG_VAR);
}
static inline term_t ALWAYS_INLINE term_make_var(var_t v)
{
    return (term_t)word_settag((word_t)v, TAG_VAR);
}
extern int_t term_compare_var(var_t va, var_t vb);

#define make_var(n)         term_build_var(n)
#define var(t)              term_get_var(t)
#define term_var(v)         term_make_var(v)
#define compare_var         term_compare_var

/****************************************************************************/
/* GENERAL                                                                  */
/****************************************************************************/

/*
 * Initialize this module.
 */
extern void term_init(void);

/*
 * Compare two terms.
 */
extern int_t term_compare(term_t a, term_t b);

#endif      /* __TERM_H */
