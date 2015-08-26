/*
 * pass_flatten.c
 * Copyright (C) 2014 National University of Singapore
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

#include "expr.h"
#include "log.h"
#include "map.h"
#include "names.h"
#include "pass_flatten.h"
#include "show.h"

/*
 * Common sub-expressions.
 */
MAP_DECL(cseinfo, expr_t, expr_t, expr_compare);

/*
 * Flattened expression context.
 */
struct context_s
{
    cseinfo_t cseinfo;
    size_t varid;
    const char *file;
    size_t line;
    bool error;
};
typedef struct context_s *context_t;

/*
 * Prototypes.
 */
static expr_t flatten(expr_t e, bool toplevel, context_t cxt);
static expr_t flatten_eq_to_builtin(exprop_t op, expr_t x, expr_t y,
    bool toplevel, context_t cxt);
static expr_t flatten_x_eq_f_to_builtin(expr_t x, expr_t f, typeinst_t type,
    context_t cxt);
static expr_t flatten_to_primitive(expr_t e, context_t cxt);
static expr_t flatten_to_var(expr_t e, context_t cxt);
static expr_t flatten_to_builtin(expr_t e, bool err, context_t cxt);
static expr_t flatten_func_to_builtin(typeinst_t type, expr_t x, expr_t f,
    context_t cxt);
static expr_t flatten_x_cmp_y_to_builtin(expr_t x, exprop_t cmp, expr_t y,
    context_t cxt);
static expr_t flatten_x_cmp_y_op_z_to_builtin(expr_t e, expr_t x, exprop_t cmp,
    expr_t y, exprop_t op, expr_t z, context_t cxt);
static expr_t context_update(context_t cxt, expr_t e);
static expr_t context_to_expr(context_t cxt);
static bool is_eq(atom_t atom);

/*
 * Flattening pass.
 */
extern expr_t pass_flatten_expr(const char *filename, size_t lineno, expr_t e)
{
    struct context_s context0;
    context_t context = &context0;
    context->cseinfo = cseinfo_init();
    context->varid = 0;
    context->file = filename;
    context->line = lineno;
    context->error = false;
    e = flatten(e, true, context);
    e = expr_and(e, context_to_expr(context));
    if (context->error)
        return (expr_t)NULL;
    else
        return e;
}

/*
 * Flattening transformation.
 */
static expr_t flatten(expr_t e, bool toplevel, context_t cxt)
{
    if (expr_gettype(e) != EXPRTYPE_OP)
        return e;

    exprop_t op = expr_op(e);
    switch (op)
    {
        case EXPROP_EQ:
        {
            expr_t x, y;
            if (expr_view_x_eq_func(e, &x, &y))
            {
                y = flatten(y, false, cxt);
                return flatten_eq_to_builtin(exprop_atom_make(ATOM_INT_EQ),
                    x, y, toplevel, cxt);

            }
            // Fall-through:
        }
        case EXPROP_NEQ: case EXPROP_LT: case EXPROP_LEQ: case EXPROP_GT:
        case EXPROP_GEQ:
        {
            // Check if already a flattened comparison:
            exprop_t binop, cmp;
            expr_t x, y, z;
            if (expr_view_x_cmp_y(e, &x, &cmp, &y))
                return flatten_x_cmp_y_to_builtin(x, cmp, y, cxt);
            if (expr_view_x_cmp_y_op_z(e, &x, &cmp, &y, &binop, &z))
            {
                y = expr_make(binop, y, z);
                return flatten_eq_to_builtin(exprop_atom_make(ATOM_INT_EQ),
                    x, y, toplevel, cxt);
            }

            e = expr_arg(e, 1);
            if (!expr_view_plus_sign_partition(e, &x, &y))
                panic("failed to partition (+) expression");
            if (op == EXPROP_EQ)
            {
                expr_t x0, x1;
                if (expr_view_plus_first_partition(x, &x0, &x1))
                {
                    x1 = flatten_to_primitive(x1, cxt);
                    y = flatten_to_var(y, cxt);
                    e = expr_make(EXPROP_ADD, x0, x1);
                    return flatten_eq_to_builtin(exprop_atom_make(ATOM_INT_EQ),
                        y, e, toplevel, cxt);
                }
                expr_t y0, y1;
                if (expr_view_plus_first_partition(y, &y0, &y1))
                {
                    y1 = flatten_to_primitive(y1, cxt);
                    x = flatten_to_var(x, cxt);
                    e = expr_make(EXPROP_ADD, y0, y1);
                    return flatten_eq_to_builtin(exprop_atom_make(ATOM_INT_EQ),
                        x, e, toplevel, cxt);
                }
                x = flatten_to_primitive(x, cxt);
                y = flatten_to_primitive(y, cxt);
                return flatten_eq_to_builtin(exprop_atom_make(ATOM_INT_EQ),
                    x, y, toplevel, cxt);
            }
            else
            {
                x = flatten_to_primitive(x, cxt);
                y = flatten_to_primitive(y, cxt);
                if (expr_gettype(y) == EXPRTYPE_NUM)
                {
                    num_t c = expr_getnum(y);
                    y = expr_num(c - 1);
                    e = flatten_x_cmp_y_to_builtin(x, cmp, y, cxt);
                    return expr_not(e);
                }
                else
                    return flatten_x_cmp_y_to_builtin(y, cmp, x, cxt);
            }
        }
        case EXPROP_NOT:
        {
            expr_t arg = expr_arg(e, 0);
            arg = flatten(arg, toplevel, cxt);
            return expr_not(arg);
        }
        case EXPROP_IFF:
        {
            expr_t arg0 = flatten(expr_arg(e, 0), false, cxt);
            expr_t arg1 = flatten(expr_arg(e, 1), false, cxt);
            return expr_iff(arg0, arg1);
        }
        case EXPROP_AND:
        {
            expr_t and = expr_bool(true), k, v;
            for (expritr_t i = expritr(e); expr_getpair(i, &k, &v);
                    expr_next(i))
            {
                k = flatten(k, toplevel, cxt);
                if (v == expr_bool(true))
                    k = expr_not(k);
                and = expr_and(k, and);
            }
            return and;
        }
        case EXPROP_OR:
        {
            expr_t or = expr_bool(false), k, v;
            for (expritr_t i = expritr(e); expr_getpair(i, &k, &v);
                    expr_next(i))
            {
                k = flatten(k, false, cxt);
                if (v == expr_bool(true))
                    k = expr_not(k);
                or = expr_or(k, or);
            }
            return or;
        }
        case EXPROP_ADD: case EXPROP_MUL:
        {
            size_t a = expr_arity(e);
            expr_t args[a];
            expr_args(e, args);
            for (size_t i = 0; i < a; i++)
                args[i] = flatten_to_primitive(args[i], cxt);
            e = expr(op, args);
            return e;
        }
        case EXPROP_POW:
        {
            expr_t arg1 = expr_arg(e, 1);
            if (expr_gettype(arg1) != EXPRTYPE_NUM)
            {
flatten_bad_pow:
                error("(%s: %zu) failed to flatten expression `!y%s!d'; "
                    "exponent must be a positive constant, found `!y%s!d'",
                    cxt->file, cxt->line, show(expr_term(e)),
                    show(expr_term(arg1)));
                cxt->error = true;
                return e;
            }
            num_t c = expr_getnum(arg1);
            if (c <= 1)
                goto flatten_bad_pow;
            expr_t arg0 = flatten_to_primitive(expr_arg(e, 0), cxt);
            expr_t e = expr_pow(arg0, arg1);
            return e;
        }
        default:
        {
            atom_t atom = expr_sym(e);
            if (atom == ATOM_NIL_EQ || atom == ATOM_STR_EQ ||
                atom == ATOM_ATOM_EQ || is_eq(atom))
            {
                expr_t x = expr_arg(e, 0), y = expr_arg(e, 1);
                x = flatten(x, false, cxt);
                y = flatten(y, false, cxt);
                return flatten_eq_to_builtin(expr_op(e), x, y, toplevel, cxt);
            }
            size_t a = expr_arity(e);
            expr_t args[a];
            expr_args(e, args);
            typesig_t sig = typeinst_get_decl((atom_t)op);
            for (size_t i = 0; i < a; i++)
            {
                typeinst_t t = typeinst_decl_arg(sig, i);
                if (t != typeinst_make_ground(t))
                    args[i] = flatten_to_var(args[i], cxt);
                else
                {
                    // Note: type check does not check instances; we check
                    //       here.
                    if (type(args[i]) == VAR || type(args[i]) == FUNC)
                    {
                        error("(%s: %zu) failed to flatten expression "
                            "`!y%s!d'; cannot flatten %s argument "
                            "`!y%s!d' to a ground term",  cxt->file,
                            cxt->line, show(expr_term(e)),
                            (type(args[i]) == VAR? "variable":
                                "function call"),
                            show(expr_term(args[i])));
                        cxt->error = true;
                    }
                }
            }
            e = expr(op, args);
            return e;
        }
    }
}

/*
 * Flatten an equality expression `x = y'.  Assumes x and y are already
 * flattened.
 */
static expr_t flatten_eq_to_builtin(exprop_t op, expr_t x, expr_t y,
    bool toplevel, context_t cxt)
{
    if (expr_gettype(x) == EXPRTYPE_VAR && expr_gettype(y) == EXPRTYPE_VAR)
        return expr_make(op, x, y);
    if (expr_gettype(y) == EXPRTYPE_VAR)
    {
        expr_t t = x;
        x = y;
        y = t;
    }
    if (expr_gettype(x) != EXPRTYPE_VAR)
        x = context_update(cxt, x);
    if (!toplevel)
        y = context_update(cxt, y);

    // Special-case handling of constants and variables:
    switch (expr_gettype(y))
    {
        case EXPRTYPE_VAR:
            if (expr_compare(x, y) < 0)
                return expr_make(op, x, y);
            else
                return expr_make(op, y, x);
        case EXPRTYPE_NUM:
            return expr_make(exprop_atom_make(ATOM_INT_EQ_C), x, y);
        case EXPRTYPE_NIL:
            return expr_make(exprop_atom_make(ATOM_NIL_EQ_C), x, y);
        case EXPRTYPE_STR:
            return expr_make(exprop_atom_make(ATOM_STR_EQ_C), x, y);
        case EXPRTYPE_ATOM:
            return expr_make(exprop_atom_make(ATOM_ATOM_EQ_C), x, y);
        case EXPRTYPE_OP:
            break;
        default:
            panic("unexpected expr type (%d)", expr_gettype(y));
    }

    // Special-case handling of add/mul:
    exprop_t fop = expr_op(y);
    switch (fop)
    {
        case EXPROP_ADD:
        {
            expr_t a = expr_arg(y, 0);
            expr_t b = expr_arg(y, 1);
            if (expr_gettype(a) == EXPRTYPE_NUM)
            {
                expr_t t = a; a = b; b = t;
            }
            if (expr_gettype(b) == EXPRTYPE_NUM)
            {
                num_t c = expr_getnum(b);
                if (c < 0)
                    return expr_make(exprop_atom_make(ATOM_INT_EQ_PLUS_C),
                        a, x, expr_num(-c));
                else
                    return expr_make(exprop_atom_make(ATOM_INT_EQ_PLUS_C),
                        x, a, b);
            }
            else
                return expr_make(exprop_atom_make(ATOM_INT_EQ_PLUS), x, a, b);
        }
        case EXPROP_MUL:
        {
            expr_t a = expr_arg(y, 0);
            expr_t b = expr_arg(y, 1);
            if (expr_gettype(a) == EXPRTYPE_NUM)
            {
                expr_t t = a; a = b; b = t;
            }
            if (expr_gettype(b) == EXPRTYPE_NUM)
                return expr_make(exprop_atom_make(ATOM_INT_EQ_MUL_C), x, a, b);
            else
                return expr_make(exprop_atom_make(ATOM_INT_EQ_MUL), x, a, b);
        }
        case EXPROP_POW:
        {
            expr_t a = expr_arg(y, 0);
            expr_t b = expr_arg(y, 1);
            return expr_make(exprop_atom_make(ATOM_INT_EQ_POW_C), x, a, b);
        }
        default:
            break;
    }

    // Generic function calls:
    atom_t atom = expr_sym(y);
    const char *name = atom_name(atom);
    size_t arity = atom_arity(atom);
    size_t len = strlen(name);
    char buf[len + 32];
    typesig_t sig = typeinst_lookup_typesig(atom);
    typeinst_t type = (sig == TYPESIG_DEFAULT? TYPEINST_NUM:
        typeinst_make_ground(sig->type));
    const char *type_name = typeinst_show(type);
    int r = snprintf(buf, sizeof(buf)-1, "%s_eq_call_%s", type_name, name);
    if (r <= 0 || r >= sizeof(buf)-1)
        panic("failed to create function constraint name");

    op = exprop_make(buf, arity+1);
    expr_t args[arity+1];
    expr_args(y, args+1);
    args[0] = x;
    expr_t e = expr(op, args);

    if (sig == TYPESIG_DEFAULT)
        return e;
    atom = expr_sym(e);
    typeinst_t sig_args[arity+1];
    memcpy(sig_args+1, sig->args, arity * sizeof(typeinst_t));
    sig_args[0] = typeinst_make_var(sig->type);
    sig = typeinst_make_typesig(arity+1, TYPEINST_BOOL, sig_args);
    if (!typeinst_declare(atom, sig))
    {
        error("(%s: %zu) failed to declare implied type for %s/%zu",
            cxt->file, cxt->line, atom_name(atom), atom_arity(arity));
        cxt->error = true;
    }
    return e;
}

/*
 * Flatten an expression to a primitive expression.
 */
static expr_t flatten_to_primitive(expr_t e, context_t cxt)
{
    e = flatten(e, false, cxt);
    switch (expr_gettype(e))
    {
        case EXPRTYPE_VAR: case EXPRTYPE_BOOL: case EXPRTYPE_NUM:
        case EXPRTYPE_ATOM: case EXPRTYPE_STR:
            return e;
        default:
            break;
    }
    return context_update(cxt, e);
}

/*
 * Flatten an expression to a variable.
 */
static expr_t flatten_to_var(expr_t e, context_t cxt)
{
    e = flatten(e, false, cxt);
    if (expr_gettype(e) == EXPRTYPE_VAR)
        return e;
    return context_update(cxt, e);
}

/*
 * Flatten (x CMP y) into a builtin.
 */
static expr_t flatten_x_cmp_y_to_builtin(expr_t x, exprop_t cmp, expr_t y,
    context_t cxt)
{
    exprtype_t ty = expr_gettype(y);
    exprop_t op;
    switch (cmp)
    {
        case EXPROP_EQ:
            op = (ty == EXPRTYPE_NUM? exprop_atom_make(ATOM_INT_EQ_C):
                                      exprop_atom_make(ATOM_INT_EQ));
            break;
        case EXPROP_GT:
            op = (ty == EXPRTYPE_NUM? exprop_atom_make(ATOM_INT_GT_C):
                                      exprop_atom_make(ATOM_INT_GT));
            break;
        default:
            panic("unexpected cmp");
    }
    return expr_make(op, x, y);
}

/*
 * Create a new variable V and add V=e to the cxt.
 */
static expr_t context_update(context_t cxt, expr_t e)
{
    if (expr_gettype(e) == EXPRTYPE_VAR)
        panic("unexpected CSE var type");
    expr_t v;
    if (cseinfo_search(cxt->cseinfo, e, &v))
        return v;
    char *name = unique_name("F", &cxt->varid);
    var_t v0 = make_var(name);
    v = expr_var(v0);
    cxt->cseinfo = cseinfo_destructive_insert(cxt->cseinfo, e, v);
    return v;
}

/*
 * Convert the cxt back into an expression.
 */
static expr_t context_to_expr(context_t cxt)
{
    expr_t k;
    expr_t v;
    expr_t and = expr_bool(true);
    cseinfo_t cseinfo = cxt->cseinfo;
    for (cseinfoitr_t i = cseinfoitr(cseinfo); cseinfo_get(i, &k, &v);
            cseinfo_next(i))
    {
        typeinst_t type = expr_gettypeinst(k);
        term_t arg;
        if (type == TYPEINST_BOOL)
            arg = expr_iff(v, k);
        else
            arg = flatten_eq_to_builtin((exprop_t)NULL, v, k, true, cxt);
        and = expr_and(arg, and);
    }
    return and;
}

/*
 * Test if a given atom is an equality.
 */
static bool is_eq(atom_t atom)
{
    if (atom_arity(atom) != 2)
        return false;
    const char *name = atom_name(atom);
    size_t len = strlen(name);
    if (len <= 3)
        return false;
    if (name[len-3] == '_' && name[len-2] == 'e' && name[len-1] == 'q')
        return true;
    return false;
}

