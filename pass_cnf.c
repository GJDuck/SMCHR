/*
 * pass_cnf.c
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "expr.h"
#include "log.h"
#include "map.h"
#include "names.h"
#include "pass_cnf.h"
#include "show.h"

/*
 * Transformation context.
 */
MAP_DECL(iffinfo, expr_t, expr_t, expr_compare);
struct context_s
{
    iffinfo_t iffinfo;
    expr_t clauses;
    size_t varid;
};
typedef struct context_s *context_t;

/*
 * Prototypes.
 */
static expr_t iffelim_expr(expr_t e, bool c);
static expr_t cnf_expr_nextlevel(expr_t e, context_t context);
static expr_t cnf_arg(expr_t e, context_t context);
static expr_t cnf_new_var(context_t context);
static expr_t context_insertiff(context_t context, expr_t e, expr_t v0);
static void context_insertclause(context_t context, expr_t clause);
static expr_t context_to_iffs(context_t context);

/*
 * Eliminate <->s from an expression.
 */
static expr_t iffelim_expr(expr_t e, bool c)
{
    if (expr_gettype(e) != EXPRTYPE_OP)
        return e;

    exprop_t op = expr_op(e);
    switch (op)
    {
        case EXPROP_NOT: case EXPROP_AND: case EXPROP_OR:
        {
            expr_t acc = (op == EXPROP_OR? expr_bool(false): expr_bool(true));
            expr_t k, v;
            for (expritr_t i = expritr(e); expr_getpair(i, &k, &v);
                    expr_next(i))
            {
                bool d = (op != EXPROP_OR);
                d = (v == expr_bool(true)? !d: d);
                k = iffelim_expr(k, d);
                if (v == expr_bool(true))
                    k = expr_not(k);
                acc = (op == EXPROP_OR? expr_or(acc, k): expr_and(acc, k));
            }
            return acc;
        }
        case EXPROP_IFF:
        {
            expr_t x = iffelim_expr(expr_arg(e, 0), c);
            expr_t y = iffelim_expr(expr_arg(e, 1), c);
            if (c)
            {
                expr_t c1 = expr_or(expr_not(x), y);
                expr_t c2 = expr_or(x, expr_not(y));
                return expr_and(c1, c2);
            }
            else
            {
                expr_t c1 = expr_and(x, y);
                expr_t c2 = expr_and(expr_not(x), expr_not(y));
                return expr_or(c1, c2);
            }
        }
        default:
            return e;
    }
}

/*
 * NNF transformation.
 */
extern expr_t pass_nnf_expr(const char *filename, size_t lineno, expr_t e)
{
    // expr_t are already in NNF except for <->s:
    return iffelim_expr(e, true);
}

/*
 * CNF transformation.  Assumes `e' is in NNF.
 */
extern void pass_cnf_expr(const char *filename, size_t lineno, expr_t e,
    expr_t *b, expr_t *d)
{
    if (expr_gettype(e) != EXPRTYPE_OP)
    {
        *b = e;
        *d = expr_bool(true);
        return;
    }

    struct context_s context0;
    context_t context = &context0;
    context->iffinfo = iffinfo_init();
    context->clauses = expr_bool(true);
    context->varid = 0;
    exprop_t op = expr_op(e);
    switch (op)
    {
        case EXPROP_NOT:
        case EXPROP_AND:
        {
            expr_t and = expr_bool(true), k, v;
            for (expritr_t i = expritr(e); expr_getpair(i, &k, &v);
                    expr_next(i))
            {
                k = cnf_expr_nextlevel(k, context);
                if (v == expr_bool(true))
                    k = expr_not(k);
                and = expr_and(k, and);
            }
            e = and;
            break;
        }
        default:
            e = cnf_expr_nextlevel(e, context);
            break;
    }
    
    *b = expr_and(e, context->clauses);
    *d = context_to_iffs(context);
    return;
}

/*
 * CNF transformation one-level down.
 */
static expr_t cnf_expr_nextlevel(expr_t e, context_t context)
{
    if (expr_gettype(e) != EXPRTYPE_OP)
        return e;

    switch (expr_op(e))
    {
        case EXPROP_NOT:
        case EXPROP_OR:
        {
            expr_t or = expr_bool(false), k, v;
            for (expritr_t i = expritr(e); expr_getpair(i, &k, &v);
                    expr_next(i))
            {
                k = cnf_arg(k, context);
                if (v == expr_bool(true))
                    k = expr_not(k);
                or = expr_or(k, or);
            }
            return or;
        }
        default:
            e = cnf_arg(e, context);
            return e;
    }
}

/*
 * CNF transformation on a Boolean argument.
 */
static expr_t cnf_arg(expr_t e, context_t context)
{
    if (expr_gettype(e) != EXPRTYPE_OP)
        return e;

    expr_t b = cnf_new_var(context), nb = expr_not(b);
    switch (expr_op(e))
    {
        case EXPROP_NOT:
        case EXPROP_AND:
        {
            expr_t clause = expr_bool(false), k, nk, v;
            for (expritr_t i = expritr(e); expr_getpair(i, &k, &v);
                    expr_next(i))
            {
                k = cnf_arg(k, context);
                nk = expr_not(k);
                if (v == expr_bool(true))
                {
                    expr_t tmp = k;
                    k = nk;
                    nk = tmp;
                }
                clause = expr_or(nk, clause);
                context_insertclause(context, expr_or(k, nb));
            }
            clause = expr_or(b, clause);
            context_insertclause(context, clause);
            return b;
        }
        case EXPROP_OR:
        {
            expr_t clause = expr_bool(false), k, nk, v;
            for (expritr_t i = expritr(e); expr_getpair(i, &k, &v);
                    expr_next(i))
            {
                k = cnf_arg(k, context);
                nk = expr_not(k);
                if (v == expr_bool(true))
                {
                    expr_t tmp = k;
                    k = nk;
                    nk = tmp;
                }
                clause = expr_or(k, clause);
                context_insertclause(context, expr_or(nk, b));
            }
            clause = expr_or(nb, clause);
            context_insertclause(context, clause);
            return b;
        }
        default:
            return context_insertiff(context, e, (expr_t)NULL);
    }
}

/*
 * Create a new CNF Boolean variable.
 */
static expr_t cnf_new_var(context_t context)
{
    var_t v0 = make_var(unique_name("C", &context->varid));
    expr_t v = expr_var(v0);
    return v;
}

/*
 * Insert a new IFF definition.
 */
static expr_t context_insertiff(context_t context, expr_t e, expr_t v0)
{
    if (expr_gettype(e) != EXPRTYPE_OP)
        return e;
    expr_t v;
    if (iffinfo_search(context->iffinfo, e, &v))
        return v;
    if (v0 == (expr_t)NULL)
        v = cnf_new_var(context);
    else
        v = v0;
    context->iffinfo = iffinfo_insert(context->iffinfo, e, v);
    return v;
}

/*
 * Insert a new clause.
 */
static void context_insertclause(context_t context, expr_t clause)
{
    context->clauses = expr_and(context->clauses, clause);
}

/*
 * Context the context into a conjunction of IFFs.
 */
static expr_t context_to_iffs(context_t context)
{
    iffinfo_t iffinfo = context->iffinfo;
    expr_t k, v;
    expr_t and = expr_bool(true);
    for (iffinfoitr_t i = iffinfoitr(iffinfo); iffinfo_get(i, &k, &v);
            iffinfo_next(i))
        and = expr_and(expr_iff(v, k), and);
    return and;
}

