/*
 * expr.h
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
#ifndef __EXPR_H
#define __EXPR_H

#include "term.h"
#include "typecheck.h"

/*
 * An expression is a special term.
 */
typedef term_t expr_t;

/*
 * Expression tags.
 */
enum exprtag_e
{
    EXPRTAG_VAR   = TAG_VAR,
    EXPRTAG_ATOM  = TAG_ATOM,
    EXPRTAG_BOOL  = TAG_BOOL,
    EXPRTAG_NUM   = TAG_NUM,
    EXPRTAG_STR   = TAG_STR,
    EXPRTAG_NIL   = TAG_NIL,
    EXPRTAG_OP    = TAG_FUNC,
    EXPRTAG_AND   = TAG_MAX+1,
    EXPRTAG_OR    = TAG_MAX+2,
    EXPRTAG_ADD   = TAG_MAX+3,
    EXPRTAG_MUL   = TAG_MAX+4,
    EXPRTAG_FUNC  = TAG_MAX+5,
};
typedef enum exprtag_e exprtag_t;

/*
 * Expression types.
 */
enum exprtype_e
{
    EXPRTYPE_VAR = 0,
    EXPRTYPE_ATOM,
    EXPRTYPE_BOOL,
    EXPRTYPE_NUM,
    EXPRTYPE_STR,
    EXPRTYPE_NIL,
    EXPRTYPE_OP,
};
typedef enum exprtype_e exprtype_t;
#define EXPRTYPE_MAX    EXPRTYPE_OP

/*
 * Expression operators.
 */
enum exprop_e
{
    EXPROP_AND     = 1,
    EXPROP_OR      = 2,
    EXPROP_IMPLIES = 3,
    EXPROP_IFF     = 4,
    EXPROP_XOR     = 5,
    EXPROP_NOT     = 6,
    EXPROP_EQ      = 7,
    EXPROP_NEQ     = 8,
    EXPROP_LT      = 9,
    EXPROP_LEQ     = 10,
    EXPROP_GT      = 11,
    EXPROP_GEQ     = 12,
    EXPROP_ADD     = 13,
    EXPROP_SUB     = 14,
    EXPROP_MUL     = 15,
    EXPROP_DIV     = 16,
    EXPROP_POW     = 17,
    EXPROP_NEG     = 18,
};
#define EXPROP_MAX             (EXPROP_NEG+1)
typedef word_t exprop_t;

/*
 * Expression iterators.
 */
typedef word_t expritr_t;

/*
 * Init this module.
 */
extern void expr_init(void);

/*
 * Expression functions.
 */
static inline exprtag_t expr_gettag(expr_t e)
{
    return (exprtag_t)word_gettag((word_t)e);
}
extern exprtype_t expr_gettype(expr_t e);
extern typeinst_t expr_gettypeinst(expr_t e);
static inline bool expr_isvar(expr_t e)
{
    return (expr_gettag(e) == EXPRTAG_VAR);
}
static inline bool expr_isbool(expr_t e)
{
    return (expr_gettag(e) == EXPRTAG_BOOL);
}
static inline bool expr_isnum(expr_t e)
{
    return (expr_gettag(e) == EXPRTAG_NUM);
}
static inline bool expr_isop(expr_t e)
{
    return expr_gettype(e) == EXPRTYPE_OP;
}
static inline var_t expr_getvar(expr_t e)
{
    return var((term_t)e);
}
static inline bool_t expr_getbool(expr_t e)
{
    return boolean((term_t)e);
}
static inline num_t expr_getnum(expr_t e)
{
    return num((term_t)e);
}
static inline expr_t expr_var(var_t v)
{
    return (expr_t)term_var(v);
}
static inline expr_t expr_bool(bool_t b)
{
    return (expr_t)term_boolean(b);
}
static inline expr_t expr_num(num_t n)
{
    return (expr_t)term_num(n);
}
extern expr_t expr_add(expr_t a, expr_t b);
extern expr_t expr_sub(expr_t a, expr_t b);
extern expr_t expr_mul(expr_t a, expr_t b);
extern expr_t expr_div(expr_t a, expr_t b);
extern expr_t expr_pow(expr_t a, expr_t b);
extern expr_t expr_neg(expr_t a);
extern expr_t expr_inv(expr_t a);
extern expr_t expr_and(expr_t a, expr_t b);
extern expr_t expr_or(expr_t a, expr_t b);
extern expr_t expr_implies(expr_t a, expr_t b);
extern expr_t expr_iff(expr_t a, expr_t b);
extern expr_t expr_xor(expr_t a, expr_t b);
extern expr_t expr_not(expr_t a);
extern expr_t expr_eq(expr_t a, expr_t b);
extern expr_t expr_neq(expr_t a, expr_t b);
extern expr_t expr_lt(expr_t a, expr_t b);
extern expr_t expr_leq(expr_t a, expr_t b);
extern expr_t expr_gt(expr_t a, expr_t b);
extern expr_t expr_geq(expr_t a, expr_t b);
extern expr_t expr(exprop_t op, expr_t *args);

#define expr_make(op, ...)                                                  \
    expr((op), (expr_t []){__VA_ARGS__})

extern size_t expr_arity(expr_t e);
extern exprop_t exprop_make(const char *name, size_t aty);
extern exprop_t exprop_atom_make(atom_t atom);
extern exprop_t expr_op(expr_t e);
extern atom_t expr_sym(expr_t e);
extern void expr_args(expr_t e, expr_t *args);
extern expr_t expr_arg(expr_t e, size_t idx);

extern expritr_t expritr(expr_t e);
extern bool expr_get(expritr_t itr, expr_t *arg);
extern bool expr_getpair(expritr_t itr, expr_t *arg, expr_t *c);
extern void expr_next(expritr_t itr);

extern size_t expr_andview_arity(expr_t e);
extern void expr_andview_args(expr_t e, expr_t *args);
extern size_t expr_orview_arity(expr_t e);
extern void expr_orview_args(expr_t e, expr_t *args);
extern size_t expr_addview_arity(expr_t e);
extern void expr_addview_args(expr_t e, expr_t *args);
extern size_t expr_mulview_arity(expr_t e);
extern void expr_mulview_args(expr_t e, expr_t *args);

extern expr_t expr_compile(typeinfo_t info, term_t t);
extern int_t expr_compare(expr_t a, expr_t b);
extern term_t expr_term(expr_t e);

extern const char *exprop_getname(exprop_t op);

// Views:
extern bool expr_view_x_cmp_y(expr_t e, expr_t *x, exprop_t *cmp, expr_t *y);
extern bool expr_view_x_cmp_y_op_z(expr_t e, expr_t *x, exprop_t *cmp,
    expr_t *y, exprop_t *op, expr_t *z);
extern bool expr_view_x_eq_func(expr_t e, expr_t *x, expr_t *f);
extern bool expr_view_plus_sign_partition(expr_t e, expr_t *x, expr_t *y);
extern bool expr_view_plus_first_partition(expr_t e, expr_t *x, expr_t *y);

#endif      /* __EXPR_H */
