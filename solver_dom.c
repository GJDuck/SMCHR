/*
 * solver_dom.c
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

/*
 * Symbols.
 */
static sym_t DOM;

/*
 * Prototypes.
 */
static void dom_init(void);
static void dom_handler(prop_t prop);

/*
 * Solver.
 */
static struct solver_s solver_dom_0 =
{
    dom_init,
    NULL,
    "dom"
};
solver_t solver_dom = &solver_dom_0;

/*
 * solver init.
 */
static void dom_init(void)
{
    DOM = make_sym("int_dom", 3, false);
    typesig_t sig = make_typesig(TYPEINST_BOOL, TYPEINST_VAR_NUM,
        TYPEINST_NUM, TYPEINST_NUM);
    register_solver(DOM, 0, EVENT_TRUE, dom_handler);
    register_typesig(DOM, sig);
}

/*
 * dom(x, lb, ub) handler.
 */
static void dom_handler(prop_t prop)
{
    cons_t c = constraint(prop);

    if (decision(c->b) != TRUE)
        return;
    if (level(c->b) != 0)
        return;

    var_t x = var(c->args[X]);
    num_t lb = num(c->args[1]);
    num_t ub = num(c->args[2]);

    reason_t reason = make_reason();
    if (lb > ub)
    {
        consequent(reason, -c->b);
        fail(reason);      
    }

    antecedent(reason, c->b);
    size_t sp = save(reason);

    size_t size = (size_t)ub - (size_t)lb + 1;
    cons_t bounds[size + 2];
    cons_t eqs[size];

    for (size_t i = 0; i < size; i++)
    {
        cons_t lbc = make_cons(reason, LB, term_var(x), term_num(lb + i));
        if (sp != save(reason))
        {
dom_handler_eq_error:
            error("incompatible solver combination; for constraint `!y%s!d', "
                "variable `!y%s!d' cannot be unified (e.g. by the `eq' "
                "solver)", show_cons(c), show_var(x));
            bail();
        }
        cons_t eqc = make_cons(reason, EQ_C, term_var(x), term_int(lb + i));
        if (sp != save(reason))
            goto dom_handler_eq_error;
        bounds[i+1] = lbc;
        eqs[i] = eqc;
    }
    cons_t lbc = make_cons(reason, LB, term_var(x), term_num(lb - 1));
    if (sp != save(reason))
        goto dom_handler_eq_error;
    bounds[0] = lbc;
    lbc = make_cons(reason, LB, term_var(x), term_num(lb + size));
    if (sp != save(reason))
        goto dom_handler_eq_error;
    bounds[size+1] = lbc;

    consequent(reason, bounds[1]->b);
    propagate(reason);
    restore(reason, sp);

    for (size_t i = 0; i < size; i++)
    {
        cons_t lbc = bounds[i+1];
        cons_t prev_lbc = bounds[i];
        cons_t next_lbc = bounds[i+2];
        cons_t eqc = eqs[i];
 
        // lb(x, L) --> lb(x, L-1)
        antecedent(reason, lbc->b);
        consequent(reason, prev_lbc->b);
        propagate(reason);
        restore(reason, sp);

        // x = C --> lbc(x, C)
        // x = C --> not lbc(x, C+1)
        antecedent(reason, eqc->b);
        consequent(reason, lbc->b);
        propagate(reason);
        undo(reason, 1);
        consequent(reason, -next_lbc->b);
        propagate(reason);
        restore(reason, sp);

        // lbc(x, c) /\ not lbc(x, C+1) --> x = C
        antecedent(reason, lbc->b);
        antecedent(reason, -next_lbc->b);
        consequent(reason, eqc->b);
        propagate(reason);
        restore(reason, sp);

        // x != C --> not lbc(x, C) \/ lbc(x, C+1)
        antecedent(reason, -eqc->b);
        consequent(reason, -lbc->b);
        consequent(reason, next_lbc->b);
        propagate(reason);
        restore(reason, sp);
    }
    
    consequent(reason, -bounds[size+1]->b);
    propagate(reason);

    annihilate(prop);
}

