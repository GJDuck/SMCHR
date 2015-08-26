/*
 * var.c
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

#include "solver.h"

#ifdef NODEBUG
#define NODEBUG_VAR
#endif

/*
 * Sizeof the solver-specific data.
 */
#define MAX_EXTRA           64
static size_t extra_size = 0;
static word_t extra_template[MAX_EXTRA];

/*
 * Variable name counter.
 */
static size_t var_count;

/*
 * Reverse info.
 */
struct revinfo_s
{
    svar_t x;
    svar_t y;
};
typedef struct revinfo_s *revinfo_t;

/*
 * Prototypes.
 */
static void var_reverse(word_t arg);

/*
 * Initialize this module.
 */
extern void solver_init_var(void)
{
    var_count = 0;
}
extern void solver_reset_var(void)
{
    solver_init_var();
}

/*
 * Construct a new variable with an optional name.
 */
extern var_t solver_make_var(const char *name)
{
    svar_t x = (svar_t)gc_malloc(sizeof(struct svar_s) +
        extra_size*sizeof(word_t));
    x->next = x;
    x->lit  = LITERAL_TRUE;
    x->hash = hash_new();
    x->cs   = NULL;
    x->tail = NULL;
    x->cs_len = 0;
    x->mark = false;
    memcpy(x->extra, extra_template, extra_size*sizeof(word_t));

    if (name == NULL)
    {
        // All solver variables must have a name:
        name = unique_name("S", &var_count);
    }
    else if (!gc_isptr((char *)name))
        name = gc_strdup(name);
    x->var.name = name;

    return (var_t)x;
}

/*
 * Allocate solver-specific-data.
 */
extern size_t solver_alloc_extra(size_t size, word_t *template)
{
    size_t old_extra_size = extra_size;
    extra_size += size;
    if (extra_size >= MAX_EXTRA)
        panic("too much solve-specific data");
    memcpy(extra_template + old_extra_size, template, size*sizeof(word_t));
    return old_extra_size;
}

/*
 * Get the solver-specific-data.
 */
extern word_t *solver_get_extra(var_t x0, size_t offset)
{
    svar_t x = (svar_t)x0;
    return x->extra + offset;
}

/*
 * Attach a constraint to a var.
 */
extern void solver_attach_var(var_t x0, cons_t c)
{
    svar_t x = (svar_t)x0;
    conslist_t cs = (conslist_t)gc_malloc(sizeof(struct conslist_s));
    cs->cons = c;
    cs->next = x->cs;
    x->cs = cs;
    if (x->cs_len == 0)
        x->tail = cs;
    x->cs_len++;
}

/*
 * Bind two variables.
 */
extern void solver_bind_vars(literal_t lit, var_t x0, var_t y0)
{
    var_t x1 = deref(x0);
    var_t y1 = deref(y0);
    if (x1 == y1)
        return;
    svar_t x = (svar_t)x1;
    svar_t y = (svar_t)y1;

    // Choose binding direction that creates the least work:
    if (y->cs_len < x->cs_len)
    {
        svar_t t = x; x = y; y = t;
        var_t s = x0; x0 = y0; y0 = s;
        var_t u = x1; x1 = y1; y1 = u;
    }

    // Signal this binding:
    solver_event_bind((var_t)x, (var_t)y);

    // Re-index all constraints in x.
    if (x->cs_len != 0)
    {
        hash_t xkey = hash_var_0((var_t)x);
        hash_t ykey = hash_var_0((var_t)y);
        conslist_t cs = x->cs;
        while (cs != NULL)
        {
            cons_t c = cs->cons;
            solver_store_move(c, xkey, ykey);
            cs = cs->next;
        }
        if (y->tail->next != x->cs)
        { 
            trail(&y->tail->next);
            y->tail->next = x->cs;
        }
        if (y->tail != x->tail)
        {
            trail(&y->tail);
            y->tail = x->tail;
        }
        trail(&y->cs_len);
        y->cs_len += x->cs_len;
    }

    // Link x and y:
    x = (svar_t)x0;
    y = (svar_t)y0;
    debug("!mLINK!d %s -> %s [%s]", show_var(&x->var), show_var(&y->var),
        show_cons(sat_get_constraint(lit)));
    if (x == x->next)
    {
        // Simple case:
        trail(&x->next);
        x->next = y;
        trail(&x->lit);
        x->lit = lit;
#ifndef NODEBUG_VAR
        solver_var_verify(x0);
        solver_var_verify(y0);
#endif
        return;
    }

    // Complex case: fix the graph for x:
    debug("FORWARD REVERSE");
    while (x != y)
    {
        svar_t n = x->next;
        literal_t nlit = (literal_t)x->lit;
        debug("LINK [%s -> %s] TO [%s -> %s] (%s)", show_var(&x->var),
            show_var(&x->next->var), show_var(&x->var), show_var(&y->var),
            show_cons(sat_get_constraint(lit)));
        x->next = y;
        x->lit  = lit;
        y = x;
        x = n;
        lit = nlit;
    }
    revinfo_t info = (revinfo_t)gc_malloc(sizeof(struct revinfo_s));
    info->x = x;
    info->y = (svar_t)y0;
    trail_func(var_reverse, (word_t)info);

#ifndef NODEBUG_VAR
    solver_var_verify(x0);
    solver_var_verify(y0);
#endif
}

/*
 * Reverse the path.
 */
static void var_reverse(word_t arg)
{
    debug("BACKTRACK REVERSE");
    revinfo_t info = (revinfo_t)arg;
    svar_t x = info->x;
    svar_t y = info->y;
    svar_t p = x;
    literal_t lit = (literal_t)x->lit;
    while (x != y)
    {
        svar_t n = x->next;
        literal_t nlit = (literal_t)x->lit;
        debug("UNLINK [%s -> %s] TO [%s -> %s] (%s)", show_var(&x->var),
            show_var(&x->next->var), show_var(&x->var), show_var(&p->var),
            show_cons(sat_get_constraint(lit)));
        x->next = p;
        x->lit  = lit;
        p = x;
        x = n;
        lit = nlit;
    }

    debug("DONE");

#ifndef NODEBUG_VAR
    solver_var_verify((var_t)p);
    solver_var_verify((var_t)info->y);
#endif
    gc_free(info);
}

/*
 * Equality test.
 */
extern bool solver_match_vars(reason_t reason, var_t x0, var_t y0)
{
    debug("MATCH %s = %s", show_var(x0), show_var(y0));

    svar_t x = (svar_t)x0;
    svar_t y = (svar_t)y0;

    if (x == y)
        return true;

    size_t s = save(reason);

#ifndef NODEBUG_VAR
    solver_var_verify(x0);
    solver_var_verify(y0);
#endif

    // Mark along x:
    svar_t xi = x;
    do
    {
        debug("MARK %s", show_var(&xi->var));
        xi->mark = true;
        xi = xi->next;
    }
    while (xi != xi->next);
    debug("MARK %s", show_var(&xi->var));
    xi->mark = true;

    // Search along y:
    svar_t yi = y;
    do
    {
        debug("SEARCH %s", show_var(&yi->var));
        if (yi->mark)
            break;
        debug("PUSH %s -> %s", show_var(&yi->var), show_var(&yi->next->var));
        antecedent(reason, yi->lit);
        yi = yi->next;
    }
    while (yi != yi->next);

    if (!yi->mark)
    {
        restore(reason, s);
        xi = x;
        do
        {
            xi->mark = false;
            xi = xi->next;
        }
        while (xi != xi->next);
        xi->mark = false;
        return false;
    }

    // Search along x and clear marks:
    xi = x;
    while (true)
    {
        xi->mark = false;
        debug("UNMARK %s", show_var(&xi->var));
        if (xi == yi)
            break;
        debug("PUSH %s -> %s", show_var(&xi->var), show_var(&xi->next->var));
        antecedent(reason, xi->lit);
        xi = xi->next;
    }
    while (xi != xi->next)
    {
        xi->mark = false;
        debug("UNMARK %s", show_var(&xi->var));
        xi = xi->next;
    }
    xi->mark = false;
    debug("UNMARK %s", show_var(&xi->var));

#ifndef NODEBUG_VAR
    solver_var_verify(x0);
    solver_var_verify(y0);
#endif

    return true;
}

/*
 * Get all constraints that mention the variable.
 */
extern conslist_t solver_var_search(var_t x0)
{
    x0 = deref(x0);
    svar_t x = (svar_t)x0;
    return x->cs;
}

/*
 * Check the svar_t invariant.
 */
extern void solver_var_verify(var_t x0)
{

solver_var_verify_restart:

    if (!gc_isptr(x0))
        panic("variable is not a GC pointer");
    const char *name;
    if (x0->name != NULL)
    {
        name = x0->name;
        if (!gc_isptr(name))
            panic("variable name `%s' is not a GC pointer", x0->name);
    }
    else
        name = show_var(x0);
    svar_t x = (svar_t)x0;
    if (gc_size(x) < sizeof(struct svar_s))
        panic("variable `%s' is not a solver var", name);

    if (x->mark)
        panic("variable `%s' is marked", name);
    if (x->next == x)
        return;
    literal_t lit = (literal_t)x->lit;
    if (lit < 0)
        panic("variable `%s' has negated literal", name);
    cons_t c = sat_get_constraint(lit);
    if (c == NULL)
        panic("variable `%s' has no constraint for literal", name);
    if (type(c->args[X]) == VAR && type(c->args[Y]) == VAR &&
        ((x0 == var(c->args[X]) && (var_t)x->next == var(c->args[Y])) ||
         (x0 == var(c->args[Y]) && (var_t)x->next == var(c->args[X]))))
    {
        x = x->next;
        x0 = (var_t)x;
        goto solver_var_verify_restart;
    }
    panic("variable `%s' has incompatible constraint %s for link %s -> %s",
        name, show_cons(c), name, show_var((var_t)x->next));
}

