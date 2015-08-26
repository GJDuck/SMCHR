/*
 * answer.c
 * Copyright (C) 2012 National University of Singapore
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

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gc.h"
#include "log.h"
#include "map.h"
#include "parse.h"
#include "prompt.h"
#include "show.h"
#include "term.h"

struct bounds_s
{
    num_t lb;
    num_t ub;
};
typedef struct bounds_s *bounds_t;

MAP_DECL(boundsinfo, var_t, bounds_t, compare_var);

struct heaps_s
{
    var_t p;
    var_t v;
    struct heaps_s *next;
};
typedef struct heaps_s *heaps_t;

MAP_DECL(heapsinfo, var_t, heaps_t, compare_var);

/*
 * Prototypes.
 */
static bool is_interesting_var(var_t x)
{
    if (x->name == NULL)
        return false;
    if (x->name[0] == '\0')
        return false;
    if (x->name[0] == '_')
        return false;
    return true;
}

/*
 * Print finite domain smchr answers in a more readble form:
 */
int main(void)
{
    if (!gc_init())
        panic("failed to initialize the garbage collector: %s",
            strerror(errno));
    
    term_init();
    parse_init();
    prompt_init();

    char *line = prompt(true, stdin, NULL);
    if (line == NULL)
        return 0;

    if (strcmp(line, "UNSAT") == 0)
    {
        message("!rUNSAT!d");
        goto end;
    }
    char unknown[] = "UNKNOWN ";
    if (strncmp(line, unknown, sizeof(unknown)-1) != 0)
        fatal("failed to parse \"%s\"; expected \"UNSAT\" or \"UNKNOWN\"",
            line);
    line += sizeof(unknown)-1;
    
    size_t lineno = 1;
    term_t t = parse_term("<stdin>", &lineno, opinfo_init(), line, NULL,
        NULL);
    if (t == (term_t)NULL)
        fatal("failed to parse \"%s\" into a term", line);

    boundsinfo_t binfo = boundsinfo_init();
    heapsinfo_t hinfo = heapsinfo_init();
    atom_t atom_and = make_atom("/\\", 2);
    atom_t atom_not = make_atom("not", 1);
    atom_t atom_lb  = make_atom("int_lb", 2);
    atom_t atom_eqc = make_atom("int_eq_c", 2);
    atom_t atom_neg = make_atom("-", 1);
    atom_t atom_in  = make_atom("in", 3);
    while (type(t) != BOOL && t != TERM_TRUE)
    {
        if (type(t) != FUNC)
            fatal("failed to parse term \"%s\"; expected a function", show(t));
        term_t c;
        func_t f = func(t);
        if (f->atom == atom_and)
        {
            c = f->args[0];
            t = f->args[1];
        }
        else
        {
            c = t;
            t = TERM_TRUE;
        }
        if (type(c) != FUNC)
c_parse_error:
            fatal("failed to parse constraint \"%s\"; expected a function",
                show(c));
        f = func(c);
        bool not = false;
        if (f->atom == atom_not)
        {
            not = true;
            c = f->args[0];
            if (type(c) != FUNC)
                goto c_parse_error;
            f = func(c);
        }
        if (f->atom == atom_lb || f->atom == atom_eqc)
        {
            if (not && f->atom == atom_eqc)
                continue;

            term_t a = f->args[0];
            if (type(a) != VAR)
v_parse_error:
                fatal("failed to parse \"%s\"; expected a variable", show(a));
            var_t x = var(a);
            if (!is_interesting_var(x))
                continue;

            term_t n = f->args[1];
            bool neg = false; 
            if (type(n) == FUNC)
            {
                f = func(n);
                if (f->atom != atom_neg)
n_parse_error:
                    fatal("failed to parse \"%s\"; expected a number", show(n));
                neg = true;
                n = f->args[0];
            }
            if (type(n) != NUM)
                goto n_parse_error;
            num_t lb = num(n);
            if (neg)
                lb = -lb;

            bounds_t bs;
            if (boundsinfo_search(binfo, x, &bs))
            {
                if (f->atom == atom_eqc)
                {
                    bs->lb = lb;
                    bs->ub = lb;
                }
                else if (not)
                {
                    if (lb-1 < bs->ub)
                        bs->ub = lb-1;
                }
                else
                {
                    if (lb > bs->lb)
                        bs->lb = lb;
                }
            }
            else
            {
                bs = (bounds_t)gc_malloc(sizeof(struct bounds_s));
                bs->lb = -inf;
                bs->ub = inf;
                if (f->atom == atom_eqc)
                {
                    bs->lb = lb;
                    bs->ub = lb;
                }
                if (not)
                    bs->ub = lb-1;
                else
                    bs->lb = lb;
                binfo = boundsinfo_insert(binfo, x, bs);
            }
        }
        else if (f->atom == atom_in)
        {
            if (not)
                continue;

            term_t a = f->args[0];
            if (type(a) != VAR)
                goto v_parse_error;
            var_t h = var(a);
            if (!is_interesting_var(h))
                continue;

            a = f->args[1];
            if (type(a) != VAR)
                goto v_parse_error;
            var_t p = var(a);

            a = f->args[2];
            if (type(a) != VAR)
                goto v_parse_error;
            var_t v = var(a);

            heaps_t hs = (heaps_t)gc_malloc(sizeof(struct heaps_s));
            hs->p = p;
            hs->v = v;
            hs->next = NULL;
            heaps_t hs0;
            if (heapsinfo_search(hinfo, h, &hs0))
            {
                while (hs0->next != NULL)
                    hs0 = hs0->next;
                hs0->next = hs;
            }
            else
                hinfo = heapsinfo_insert(hinfo, h, hs);
        }

    }

    var_t x;
    bounds_t bs;
    message("!gUNKNOWN!d");
    for (boundsinfoitr_t i = boundsinfoitr(binfo); boundsinfo_get(i, &x, &bs);
            boundsinfo_next(i))
    {
        if (bs->lb == bs->ub)
            message("!r%s!d = !g%s!d", show_var(x), show_num(bs->lb));
        else
            message("!r%s!d::!g%s!d..!g%s!d", show_var(x), show_num(bs->lb),
                show_num(bs->ub));
    }
    heaps_t hs;
    for (heapsinfoitr_t i = heapsinfoitr(hinfo); heapsinfo_get(i, &x, &hs);
            heapsinfo_next(i))
    {
        message_0("!r%s!d = {", show_var(x));
        while (true)
        {
            message_0("!m%s!d |-> !c%s!d", show_var(hs->p), show_var(hs->v));
            if (hs->next == NULL)
                break;
            hs = hs->next;
            message_0(", ");
        }
        message("}!d");
    }

end:
    while ((line = prompt(true, stdin, NULL)) != NULL)
        message(line);
    
    return 0;
}

