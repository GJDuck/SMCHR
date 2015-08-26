/*
 * solver_eq.c
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

#include "options.h"
#include "solver.h"

/*
 * Prototypes.
 */
static void eq_init(void);
static void eq_handler(prop_t prop);
static void eq_default_handler(prop_t prop);

/*
 * Solver.
 */
static struct solver_s solver_eq_0 =
{
    eq_init,
    NULL,
    "eq"
};
solver_t solver_eq = &solver_eq_0;

/*
 * Solver initialization.
 */
static void eq_init(void)
{
    check(option_eq);
    register_solver(EQ, 1, EVENT_ALL, eq_handler);
    register_solver(EQ_NIL, 1, EVENT_ALL, eq_handler);
    register_solver(EQ_ATOM, 1, EVENT_ALL, eq_handler);
    register_solver(EQ_STR, 1, EVENT_ALL, eq_handler);
}

/*
 * x = y handler.
 */
static void eq_handler(prop_t prop)
{
    cons_t c = constraint(prop);
    debug("!rEQ!d WAKE %s <%d>", show_cons(c), level(c->b));
    term_t x = c->args[X];
    term_t y = c->args[Y];
    switch (decision(c->b))
    {
        case TRUE:
        {
            if (match_vars_0(var(x), var(y)))
            {
                debug("!rDELETE!d %s", show_cons(c));
                purge(c);
                return;
            }
            debug("!rBIND!d %s <%d>", show_cons(c), level(c->b));
            solver_bind_vars(c->b, var(x), var(y));
            kill(prop);
            return;
        }
        case FALSE:
        {
            reason_t reason = make_reason(-c->b);
            debug("!rMATCH!d %s", show_cons(c));
            if (match_vars(reason, var(x), var(y)))
                fail(reason);
            return;
        }
        default:
            return;
    }
}

/*
 * Register the default handler for the given symbol.
 */
extern void solver_default_solver(sym_t sym)
{
    if (!option_eq)
        return;
    register_solver(sym, 4, EVENT_ALL, eq_default_handler);
    debug("!yDEFAULT!d %s/%zu", sym->name, sym->arity);
}

/*
 * Default handler.
 */
static void eq_default_handler(prop_t prop)
{
    cons_t c = constraint(prop);

    debug("!yDEFAULT!d WAKE %s", show_cons(c));

    hash_t key = hash_cons(c);
    conslist_t cs = solver_store_search(key);
    bool del = false;
    while (cs != NULL)
    {
        cons_t d = cs->cons;
        cs = cs->next;
        if (c == d)
            continue;

        reason_t reason = make_reason();
        for (size_t i = 0; i < c->sym->arity; i++)
            solver_match_arg(reason, c->args[i], d->args[i]);
        antecedent(reason, c->b);
        consequent(reason, d->b);
        propagate(reason);
        undo(reason, 2);
        antecedent(reason, -c->b);
        consequent(reason, -d->b);
        propagate(reason);

        // One copy not deleted?
        if (!ispurged(d))
            del = true;
    }

    // NOTE: it is important to delete the most recent copy, otherwise very
    //       subtle late clause bugs may occur with some solvers.
    if (del)
    {
        debug("!yDELETE!d %s", show_cons(c));
        purge(c);
    }
}

