/*
 * smchr.c
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

#include <errno.h>
#include <string.h>

#include "backend.h"
#include "debug.h"
#include "expr.h"
#include "options.h"
#include "pass_cnf.h"
#include "pass_flatten.h"
#include "pass_rewrite.h"
#include "plugin.h"
#include "smchr.h"
#include "solver.h"

#include "solver_bounds.h"
#include "solver_chr.h"
#include "solver_dom.h"
#include "solver_eq.h"
#include "solver_heaps.h"
#include "solver_linear.h"

/*
 * Solver info.
 */
MAP_DECL(solverinfo, char *, solver_t, strcmp_compare);
static solverinfo_t solverinfo;

/*
 * Initialize this module.
 */
extern void smchr_init(void)
{
    static bool init = false;

    if (init)
        return;

    os_init();
    if (!gc_init())
        panic("failed to initialize the garbage collector: %s",
            strerror(errno));
    term_init();
    typecheck_init();
    rewrite_init();
    expr_init();
    parse_init();
    names_init();
    solver_init();
    sat_init();

    if (!gc_root(&solverinfo, sizeof(solverinfo)))
        panic("failed to register GC root for solver info");
    solverinfo = solverinfo_init();

    init = true;
}

/*
 * Load a solver.
 */
extern bool smchr_load(const char *name)
{
    smchr_init();

    // Check if solver is already loaded.
    if (solverinfo_search(solverinfo, (char *)name, NULL))
        return true;

    // Builtin solvers:
    solver_t solvers[] = {
        solver_dom,
        solver_eq,
        solver_bounds,
        solver_heaps,
        solver_linear};

    // (1) Check for `.chr' extension:
    size_t len = strlen(name);
    if (len >= 5 && strcmp(name + len - 4, ".chr") == 0)
    {
        // Load CHR solver:
        solver_chr->init();
        solverinfo = solverinfo_insert(solverinfo, gc_strdup(name), NULL);
        return chr_compile(name);
    }

    // (2) Check for a built-in solver:
    for (size_t i = 0; i < sizeof(solvers) / sizeof(solver_t); i++)
    {
        solver_t solver = solvers[i];
        if (strcmp(name, solver->name) == 0)
        {
            if (solver->init != NULL)
                solver->init();
            solverinfo = solverinfo_insert(solverinfo, solver->name, solver);
            return true;
        }
    }

    // (3) Attempt to load solver from a plugin.
    solver_t solver = plugin_load(name);
    if (solver != NULL)
    {
        if (solver->init != NULL)
            solver->init();
        solverinfo = solverinfo_insert(solverinfo, solver->name, solver);
        return true;
    }

    return false;
}

/*
 * Execute the given goal.
 */
extern term_t smchr_execute(const char *filename, size_t lineno, term_t goal)
{
    smchr_init();
    option_debug_on = option_debug;
    stats_reset();

    // (1) Type-checking:
    debug("T: !m%s", show(goal));
    if (option_debug_on)
    {
        message("************************************************************"
            "********************");
        message("[orig   ] = !m%s!d", show(goal));
    }

    typeinfo_t tinfo;
    if (!typecheck(filename, lineno, goal, &tinfo))
        return TERM_NIL;

    // (2) Term-to-expression conversion:
    expr_t e = expr_compile(tinfo, goal);
    debug("0: !y%s", show(expr_term(e)));
    if (option_debug_on)
        message("[expr   ] = !y%s!d", show(expr_term(e)));

    // (3) Flatten the expression:
    e = pass_flatten_expr(filename, lineno, e);
    if (e == (expr_t)NULL)
        return TERM_NIL;
    debug("F: !g%s", show(expr_term(e)));
    if (option_debug_on)
        message("[flatten] = !g%s!d", show(expr_term(e)));

    // (4) Convert the expression into CNF:
    expr_t d;
    e = pass_nnf_expr(filename, lineno, e);
    debug("N: !c%s", show(expr_term(e)));
    if (option_debug_on)
        message("[NNF    ] = !c%s!d", show(expr_term(e)));
    e = pass_rewrite_expr(filename, lineno, e);
    debug("R: !b%s", show(expr_term(e)));
    if (option_debug_on)
        message("[rewrite] = !b%s!d", show(expr_term(e)));
    pass_cnf_expr(filename, lineno, e, &e, &d);
    debug("C: !r%s", show(expr_term(e)));
    debug("D: !r%s", show(expr_term(d)));
    if (option_debug_on)
    {
        message("[CNF_SAT] = !r%s!d", show(expr_term(e)));
        message("[CNF_def] = !r%s!d", show(expr_term(d)));
    }

    // (5) (Re)Initialize the solvers:
    solver_t solver;
    for (solverinfoitr_t i = solverinfoitr(solverinfo);
            solverinfo_get(i, NULL, &solver); solverinfo_next(i))
    {
        if (solver == NULL)
            continue;
        if (solver->reset != NULL)
        {
            solver_reset_t rst = solver->reset;
            rst();
        }
    }

    // (6) Load the constraints:
    gc_collect();
    term_t answer = TERM_NIL;
    if (!backend(filename, lineno, e, d))
        goto smchr_execute_cleanup;

    // (7) Execute the compiled goal:
    debug_init();
    stats_start();
    result_t result = solve(NULL);
    stats_stop();

    // (8) Convert the result:
    switch (result)
    {
        case RESULT_UNKNOWN:
            answer = result();
            break;
        case RESULT_UNSAT:
            answer = TERM_FALSE;
            break;
        default:
            answer = TERM_NIL;
            break;
    }

    // (9) Cleanup:
smchr_execute_cleanup:
    solver_reset();
    sat_reset();
    gc_collect();

    return answer;
}

