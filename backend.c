/*
 * backend.c
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

#include "expr.h"
#include "log.h"
#include "map.h"
#include "sat.h"
#include "show.h"
#include "solver.h"
#include "term.h"

/*
 * Context.
 */
typedef word_t litword_t;
MAP_DECL(varlits, var_t, litword_t, compare_var);
MAP_DECL(varvars, var_t, var_t, compare_var);
struct context_s
{
    varlits_t varlits;          // SAT variables
    varvars_t varvars;          // Theory variables
    bool error;                 // Error occurred?
    const char *file;           // Filename.
    size_t line;                // Lineno.
};
typedef struct context_s *context_t;

/*
 * Prototypes.
 */
static void backend_clauses(context_t cxt, expr_t e);
static void backend_theory(context_t cxt, expr_t e);
static void backend_theory_iff(context_t cxt, expr_t e);
static void backend_sat_clause(context_t cxt, expr_t e);
static literal_t backend_sat_literal(context_t cxt, expr_t e);
static void backend_insert_sat_literal(context_t cxt, var_t v, literal_t lit);
static term_t backend_term(context_t cxt, term_t t);

/*
 * Solver backend.
 */
extern bool backend(const char *filename, size_t lineno, expr_t s, expr_t t)
{
    struct context_s cxt0 = {varlits_init(), varvars_init()};
    context_t cxt = &cxt0;
    cxt->error = false;
    cxt->file = filename;
    cxt->line = lineno;

    backend_theory(cxt, t);
    backend_clauses(cxt, s);

    return !cxt->error;
}

/****************************************************************************/
/* THEORY                                                                   */
/****************************************************************************/

/*
 * Theory solver backend.
 */
static void backend_theory(context_t cxt, expr_t e)
{
    switch (expr_gettype(e))
    {
        case EXPRTYPE_OP:
            break;
        case EXPRTYPE_BOOL:
            if (expr_getbool(e))
                return;
        default:
            error("(%s: %zu) failed to translate constraints; expected a bool "
                "or operator; found `!y%s!d'", cxt->file, cxt->line,
                show(expr_term(e)));
            cxt->error = true;
            return;
    }

    exprop_t op = expr_op(e);
    switch (op)
    {
        case EXPROP_AND:
        {
            expr_t iff;
            for (expritr_t i = expritr(e); expr_get(i, &iff); expr_next(i))
                backend_theory_iff(cxt, iff);
            break;
        }
        case EXPROP_IFF:
            backend_theory_iff(cxt, e);
            break;
        default:
            error("(%s: %zu) failed to translate constraints; expected a "
                "conjunction or iff, found `!y%s!d'", cxt->file, cxt->line,
                show(expr_term(e)));
            cxt->error = true;
            return;
    }
}

/*
 * Convert an IFF expr_t into a theory constraint.
 */
static void backend_theory_iff(context_t cxt, expr_t e)
{
    expr_t e0 = e;
    expr_t d = expr_arg(e, 0);
    e = expr_arg(e, 1);

    if (expr_gettype(d) != EXPRTYPE_VAR)
    {
        error("(%s: %zu) failed to translated term `!y%s!d'; expected a "
            "variable LHS, found `!y%s'!d", cxt->file, cxt->line, 
            show(expr_term(e0)), show(expr_term(d)));
        cxt->error = true;
        return;
    }
    if (expr_gettype(e) != EXPRTYPE_OP)
    {
        error("(%s: %zu) failed to translate term `!y%s!d'; expected a "
            "constraint RHS; found `!y%s!d'", cxt->file, cxt->line,
            show(expr_term(e0)), show(expr_term(e)));
        cxt->error = true;
        return;
    }

    term_t t = expr_term(e);
    if(type(t) != FUNC)
    {
        error("(%s: %zu) failed to translate term `!y%s!d' into a constraint; "
            "invalid term type `!y%s!d'", cxt->file, cxt->line, show(t),
            type_name(type(t)));
        cxt->error = true;
        return;
    }
    func_t f = func(t);
    size_t arity = atom_arity(f->atom);
    term_t args[arity];
    for (size_t i = 0; i < arity; i++)
    {
        switch (type(f->args[i]))
        {
            case VAR: case ATOM: case BOOL: case NUM: case STR:
            case NIL:
                args[i] = backend_term(cxt, f->args[i]);
                break;
            default:
                error("(%s: %zu) failed to translate term `!y%s!d' into a "
                    "constraint; invalid non-primitive argument `!y%s!d'",
                    cxt->file, cxt->line, show(t), show(f->args[i]));
                cxt->error = true;
                return;
        }
    }
    sym_t sym = make_sym(atom_name(f->atom), atom_arity(f->atom), true);
    typesig_t sig = typeinst_lookup_typesig(f->atom);
    register_typesig(sym, sig);
    cons_t c = make_cons_a(NULL, sym, args);
    var_t b = expr_getvar(d);
    backend_insert_sat_literal(cxt, b, c->b);
}

/****************************************************************************/
/* SAT                                                                      */
/****************************************************************************/

/*
 * SAT solver backend.
 */
static void backend_clauses(context_t cxt, expr_t e)
{
    switch (expr_gettype(e))
    {
        case EXPRTYPE_OP:
        {
            exprop_t op = expr_op(e);
            switch (op)
            {
                case EXPROP_AND:
                {
                    expr_t clause;
                    for (expritr_t i = expritr(e); expr_get(i, &clause);
                            expr_next(i))
                        backend_sat_clause(cxt, clause);
                    break;
                }
                default:
                    backend_sat_clause(cxt, e);
                    break;
            }
            break;
        }
        case EXPRTYPE_VAR:
            backend_sat_clause(cxt, e);
            break;
        case EXPRTYPE_BOOL:
            if (!expr_getbool(e))
                // Assert the empty clause:
                sat_add_clause(NULL, 0, true, NULL, 0);
            break;
        default:
            error("(%s: %zu) failed to translate term `!y%s!d' into a "
                "clause-list; expected an operator, variable, or bool",
                cxt->file, cxt->line, show(expr_term(e)));
            cxt->error = true;
            return;
    }
}

/*
 * Convert an expr_t into a SAT clause.
 */
static void backend_sat_clause(context_t cxt, expr_t e)
{
    switch (expr_gettype(e))
    {
        case EXPRTYPE_OP:
        {
            exprop_t op = expr_op(e);
            switch (op)
            {
                case EXPROP_OR:
                {
                    size_t a = expr_orview_arity(e) / 2;
                    literal_t clause[a];
                    expr_t k, s;
                    size_t j = 0;
                    for (expritr_t i = expritr(e); expr_getpair(i, &k, &s);
                            expr_next(i), j++)
                    {
                        literal_t lit = backend_sat_literal(cxt, k);
                        if (expr_getbool(s))
                            lit = -lit;
                        clause[j] = lit;
                    }
                    sat_add_clause(clause, a, true, NULL, 0);
                    break;
                }
                case EXPROP_NOT:
                {
                    expr_t arg = expr_arg(e, 0);
                    literal_t lit = backend_sat_literal(cxt, arg);
                    lit = -lit;
                    sat_add_clause(&lit, 1, true, NULL, 0);
                    break;
                }
            }
            break;
        }
        case EXPRTYPE_VAR:
        {
            literal_t lit = backend_sat_literal(cxt, e);
            sat_add_clause(&lit, 1, true, NULL, 0);
            break;
        }
        default:
            error("(%s: %zu) failed to translate term `!y%s!d' into a clause; "
                "expected an operator or variable", cxt->file, cxt->line,
                show(expr_term(e)));
            cxt->error = true;
            return;
    }
}

/****************************************************************************/
/* MISC                                                                     */
/****************************************************************************/

/*
 * Convert a variable into a SAT literal.
 */
static literal_t backend_sat_literal(context_t cxt, expr_t e)
{
    if (expr_gettype(e) != EXPRTYPE_VAR)
        panic("expected a variable");
    var_t v = expr_getvar(e);
    litword_t w;
    if (!varlits_search(cxt->varlits, v, &w))
    {
        bvar_t b = sat_make_var(v, NULL);
        w = (litword_t)b;
        cxt->varlits = varlits_insert(cxt->varlits, v, w);
    }
    return (literal_t)w;
}

/*
 * Insert a SAT literal entry.
 */
static void backend_insert_sat_literal(context_t cxt, var_t v, literal_t lit)
{
    litword_t w = (litword_t)lit;
    cxt->varlits = varlits_insert(cxt->varlits, v, w);
}

/*
 * Convert a variable into a solver variable.
 */
static term_t backend_term(context_t cxt, term_t t)
{
    if (type(t) != VAR)
        return t;
    var_t v = var(t);
    var_t x;
    if (!varvars_search(cxt->varvars, v, &x))
    {
        x = make_var(v->name);
        cxt->varvars = varvars_insert(cxt->varvars, v, x);
    }
    return term_var(x);
}

