/*
 * show.c
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

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "log.h"
#include "op.h"
#include "show.h"
#include "term.h"

/*
 * Prototypes.
 */
static bool show_needs_brackets(atom_t atom, term_t t, size_t idx,
    assoc_t assoc, unsigned priority, bool ac);

/*
 * Print a term to a buffer.
 */
extern char *show_buf(char *start, char *end, term_t t)
{
    switch (type(t))
    {
        case NIL:
            return show_buf_nil(start, end);
        case BOOL:
            return show_buf_bool(start, end, boolean(t));
        case NUM:
            return show_buf_num(start, end, num(t));
        case STR:
            return show_buf_string(start, end, string(t));
        case ATOM:
            return show_buf_atom(start, end, atom(t));
        case FUNC:
            return show_buf_func(start, end, func(t));
        case FOREIGN:
            return show_buf_foreign(start, end, foreign(t));
        case VAR:
            return show_buf_var(start, end, var(t));
        default:
            panic("bad term type (%d)", type(t));
    }
}

/*
 * Print a term to a file.
 */
extern void show_file(FILE *file, term_t t)
{
    char buf[1000];
    char *end = show_buf(buf, buf+sizeof(buf), t);
    if (end < buf+sizeof(buf))
    {
        fputs(buf, file);
        return;
    }
    else
    {
        char buf2[end-buf+1];
        show_buf(buf2, buf2+sizeof(buf2), t);
        fputs(buf2, file);
        return;
    }
}

/*
 * Print a term to an allocated string.
 */
extern char *show(term_t t)
{
    char *end = show_buf(NULL, NULL, t);
    size_t len = end - (char *)NULL;
    char *str = (char *)gc_malloc(len+1);
    show_buf(str, str+len+1, t);
    return str;
}

/*
 * Write a character.
 */
extern char *show_buf_char(char *start, char *end, char c)
{
    if (start < end)
        *start = c;
    start++;
    if (start < end)
        *start = '\0';
    return start;
}


/*
 * Write a string.
 */
extern char *show_buf_str(char *start, char *end, const char *str)
{
    for (size_t i = 0; str[i] != '\0'; i++)
        start = show_buf_char(start, end, str[i]);
    return start;
}

/*
 * Write an escaped string.
 */
extern char *show_buf_esc_str(char *start, char *end, const char *str,
    size_t str_len)
{
    for (size_t i = 0; i < str_len; i++)
    {
        if (isprint(str[i]) && str[i] != '"' && str[i] != '\\')
            start = show_buf_char(start, end, str[i]);
        else
        {
            start = show_buf_char(start, end, '\\');
            switch (str[i])
            {
                case '\0':
                    start = show_buf_char(start, end, '0');
                    break;
                case '"':
                    start = show_buf_char(start, end, '"');
                    break;
                case '\\':
                    start = show_buf_char(start, end, '\\');
                    break;
                case '\n':
                    start = show_buf_char(start, end, 'n');
                    break;
                case '\r':
                    start = show_buf_char(start, end, 'r');
                    break;
                case '\t':
                    start = show_buf_char(start, end, 't');
                    break;
                case '\a':
                    start = show_buf_char(start, end, 'a');
                    break;
                case '\b':
                    start = show_buf_char(start, end, 'b');
                    break;
                case '\f':
                    start = show_buf_char(start, end, 'f');
                    break;
                default:
                {
                    start = show_buf_char(start, end, 'x');
                    char tmp[3];
                    snprintf(tmp, sizeof(tmp), "%.2X", str[i] & 0xFF);
                    start = show_buf_char(start, end, tmp[0]);
                    start = show_buf_char(start, end, tmp[1]);
                    break;
                }
            }
        }
    }
    return start;
}

/*
 * Write a functor.
 */
extern char *show_buf_func(char *start, char *end, func_t f)
{
show_buf_func_restart: {}
    const char *name = atom_name(f->atom);
    uint_t a = atom_arity(f->atom);
    assoc_t assoc;
    unsigned priority;
    bool ac, space;

    if (a == 1 && unop_lookup(opinfo_init(), name, &priority, &space))
    {
        // A unary operator.
        bool b2 = show_needs_brackets(f->atom, f->args[0], 0, XFX, priority,
            false);
        start = show_buf_str(start, end, name);
        if (space)
            start = show_buf_char(start, end, ' ');
        if (!b2 && type(f->args[0]) == FUNC)
        {
            f = func(f->args[0]);
            goto show_buf_func_restart;     // Tail call optimization.
        }
        if (b2)
            start = show_buf_char(start, end, '(');
        start = show_buf(start, end, f->args[0]);
        if (b2)
            start = show_buf_char(start, end, ')');
        return start;
    }

    if (a == 2 && binop_lookup(opinfo_init(), name, &assoc, &priority, &ac,
            &space))
    {
        // A binary operator.
        bool b1 = show_needs_brackets(f->atom, f->args[0], 0, assoc, priority,
            ac);
        bool b2 = show_needs_brackets(f->atom, f->args[1], 1, assoc, priority,
            ac);
        if (b1)
            start = show_buf_char(start, end, '(');
        start = show_buf(start, end, f->args[0]);
        if (b1)
            start = show_buf_char(start, end, ')');
        if (space)
            start = show_buf_char(start, end, ' ');
        start = show_buf_str(start, end, name);
        if (space)
            start = show_buf_char(start, end, ' ');
        if (!b2 && type(f->args[1]) == FUNC)
        {
            f = func(f->args[1]);
            goto show_buf_func_restart;     // Tail call optimization.
        }
        if (b2)
            start = show_buf_char(start, end, '(');
        start = show_buf(start, end, f->args[1]);
        if (b2)
            start = show_buf_char(start, end, ')');
        return start;
    }

    // Not an operator:
    start = show_buf_name(start, end, name);
    if (a == 0)
    {
        start = show_buf_str(start, end, "()");
        return start;
    }

    start = show_buf_char(start, end, '(');
    for (uint_t i = 0; i < a-1; i++)
    {
        start = show_buf(start, end, f->args[i]);
        start = show_buf_char(start, end, ',');
    }
    start = show_buf(start, end, f->args[a-1]);
    start = show_buf_char(start, end, ')');
    return start;
}

/*
 * Show a functor.
 */
extern char *show_func(func_t f)
{
    char *end = show_buf_func(NULL, NULL, f);
    size_t len = end - (char *)NULL;
    char *str = (char *)gc_malloc(len+1);
    show_buf_func(str, str+len+1, f);
    return str;
}

/*
 * Check if an argument needs brackets or not.
 */
static bool show_needs_brackets(atom_t atom, term_t t, size_t idx,
    assoc_t assoc, unsigned priority, bool ac)
{
    if (type(t) != FUNC)
        return false;
    func_t f = func(t);
    if (atom_arity(f->atom) != 2)
        return false;
    unsigned priority_1;
    if (!binop_lookup(opinfo_init(), atom_name(f->atom), NULL, &priority_1,
            NULL, NULL))
        return false;
    if (priority_1 < priority)
        return false;
    if (priority_1 > priority)
        return true;
    if (atom == f->atom && ac)
        return false;
    switch (assoc)
    {
        case XFX:
            return true;
        case XFY:
            return (idx == 1);
        case YFX:
            return (idx == 0);
    }
    return true;
}

/*
 * Write a name.
 */
extern char *show_buf_name(char *start, char *end, const char *name)
{
    bool quotes = false;
    for (size_t i = 0; name[i] != '\0'; i++)
    {
        if (name[i] != '_' && !isalnum(name[i]))
        {
            quotes = true;
            break;
        }
    }

    if (quotes)
    {
        start = show_buf_char(start, end, '\'');
        start = show_buf_esc_str(start, end, name, strlen(name));
        start = show_buf_char(start, end, '\'');
    }
    else
        start = show_buf_str(start, end, name);
    return start;
}

/*
 * Write a string.
 */
extern char *show_buf_string(char *start, char *end, str_t s)
{
    start = show_buf_char(start, end, '"');
    start = show_buf_esc_str(start, end, s->chars, s->len);
    start = show_buf_char(start, end, '"');
    return start;
}

/*
 * Show a string.
 */
extern char *show_string(str_t s)
{
    char *end = show_buf_string(NULL, NULL, s);
    size_t len = end - (char *)NULL;
    char *str = (char *)gc_malloc(len+1);
    show_buf_string(str, str+len+1, s);
    return str;
}

/*
 * Write an atom.
 */
extern char *show_buf_atom(char *start, char *end, atom_t a)
{
    start = show_buf_char(start, end, '@');
    start = show_buf_str(start, end, atom_name(a));
    return start;
}

/*
 * Show a Boolean.
 */
extern char *show_atom(atom_t a)
{
    char *end = show_buf_atom(NULL, NULL, a);
    size_t len = end - (char *)NULL;
    char *str = (char *)gc_malloc(len+1);
    show_buf_atom(str, str+len+1, a);
    return str;
}

/*
 * Write a nil.
 */
extern char *show_buf_nil(char *start, char *end)
{
    return show_buf_str(start, end, "nil");             
}

/*
 * Show a nil.
 */
extern char *show_nil(void)
{
    char *str = (char *)gc_malloc(4);
    show_buf_nil(str, str+4);
    return str;
}

/*
 * Write a Boolean.
 */
extern char *show_buf_bool(char *start, char *end, bool_t b)
{
    return show_buf_str(start, end, (b? "true": "false"));
}

/*
 * Show a Boolean.
 */
extern char *show_bool(bool_t b)
{
    char *end = show_buf_bool(NULL, NULL, b);
    size_t len = end - (char *)NULL;
    char *str = (char *)gc_malloc(len+1);
    show_buf_bool(str, str+len+1, b);
    return str;
}

/*
 * Write a number.
 */
#define NUM_MAX_LEN         100
#define NUM_MIN_PRECISION   15
#define NUM_MAX_PRECISION   (NUM_MIN_PRECISION+2)
extern char *show_buf_num(char *start, char *end, num_t n)
{
    if (n == inf)
        return show_buf_str(start, end, "inf");
    if (n == -inf)
        return show_buf_str(start, end, "-inf");

    char tmp[NUM_MAX_LEN];
    ssize_t l;
    size_t p = NUM_MIN_PRECISION;
    num_t n1;
    do
    {
        l = snprintf(tmp, sizeof(tmp), "%.*g", (int)p, n);
        if (l < 0)
            panic("snprintf failed: %s", strerror(errno));
        p++;
        if (p >= NUM_MAX_PRECISION)
            break;
        sscanf(tmp, "%lf", &n1);
        n1 = num(make_num(n1));
    }
    while (n != n1);

    if (start >= end)
        return start + (size_t)l;

    return show_buf_str(start, end, tmp);
}

/*
 * Show a number.
 */
extern char *show_num(num_t n)
{
    char *end = show_buf_num(NULL, NULL, n);
    size_t len = end - (char *)NULL;
    char *str = (char *)gc_malloc(len+1);
    show_buf_num(str, str+len+1, n);
    return str;
}

/*
 * Write a variable.
 */
extern char *show_buf_var(char *start, char *end, var_t v)
{
    const char *name = v->name;
    if (name == NULL)
    {
        char *buf = (start < end? start: NULL);
        size_t len = (start < end? end - start: 0);
        size_t idx = gc_objidx(v);
        int res = snprintf(buf, len, "_V%zu", idx);
        if (res < 0)
            panic("snprintf failed: %s", strerror(errno));
        return start + (size_t)res;
    }
    else
        return show_buf_name(start, end, name);
}

/*
 * Show a variable.
 */
extern char *show_var(var_t v)
{
    char *end = show_buf_var(NULL, NULL, v);
    size_t len = end - (char *)NULL;
    char *str = (char *)gc_malloc(len+1);
    show_buf_var(str, str+len+1, v);
    return str;
}

/*
 * Write a foriegn value.
 */
extern char *show_buf_foreign(char *start, char *end, foreign_t f)
{
    char *buf = (start < end? start: NULL);
    size_t len = (start < end? end - start: 0);

    int res = snprintf(buf, len, "#%.16llX", (unsigned long long)f);
    if (res < 0)
        panic("snprintf failed: %s", strerror(errno));
    
    return start + (size_t)res;
}

/*
 * Show a foreign.
 */
extern char *show_foreign(foreign_t f)
{
    char *end = show_buf_foreign(NULL, NULL, f);
    size_t len = end - (char *)NULL;
    char *str = (char *)gc_malloc(len+1);
    show_buf_foreign(str, str+len+1, f);
    return str;
}

