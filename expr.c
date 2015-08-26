/*
 * expr.c
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

/*
 * Expression normal form.
 */

#include <math.h>

#include "expr.h"
#include "log.h"
#include "map.h"
#include "misc.h"
#include "show.h"
#include "term.h"

MAP_DECL(add, expr_t, num_t,  expr_compare);
MAP_DECL(mul, expr_t, num_t,  expr_compare);
MAP_DECL(and, expr_t, bool_t, expr_compare);
MAP_DECL(or,  expr_t, bool_t, expr_compare);

#define additr(t)           add_itrinit(alloca(add_itrsize(t)), (t))
#define additr_geq(t, k)    add_itrinit_geq(alloca(add_itrsize(t)), (t), (k))
#define mulitr(t)           mul_itrinit(alloca(mul_itrsize(t)), (t))
#define mulitr_geq(t, k)    mul_itrinit_geq(alloca(mul_itrsize(t)), (t), (k))
#define anditr(t)           and_itrinit(alloca(and_itrsize(t)), (t))
#define anditr_geq(t, k)    and_itrinit_geq(alloca(and_itrsize(t)), (t), (k))
#define oritr(t)            or_itrinit(alloca(or_itrsize(t)), (t))
#define oritr_geq(t, k)     or_itrinit_geq(alloca(or_itrsize(t)), (t), (k))

static inline and_t expr_getand(expr_t e)
{
    return (and_t)word_untag((word_t)e, EXPRTAG_AND);
}
static inline expr_t expr_makeand(and_t a)
{
    return (expr_t)word_settag((word_t)a, EXPRTAG_AND);
}
static inline or_t expr_getor(expr_t e)
{
    return (or_t)word_untag((word_t)e, EXPRTAG_OR);
}
static inline expr_t expr_makeor(or_t o)
{
    return (expr_t)word_settag((word_t)o, EXPRTAG_OR);
}
static inline add_t expr_getadd(expr_t e)
{
    return (add_t)word_untag((word_t)e, EXPRTAG_ADD);
}
static inline expr_t expr_makeadd(add_t a)
{
    return (expr_t)word_settag((word_t)a, EXPRTAG_ADD);
}
static inline mul_t expr_getmul(expr_t e)
{
    return (mul_t)word_untag((word_t)e, EXPRTAG_MUL);
}
static inline expr_t expr_makemul(mul_t m)
{
    return (expr_t)word_settag((word_t)m, EXPRTAG_MUL);
}
static inline func_t expr_getop(expr_t e)
{
    return (func_t)func((term_t)e);
}
static inline expr_t expr_makeop(func_t f)
{
    return (expr_t)term_func(f);
}
static inline func_t expr_getfunc(expr_t e)
{
    return (func_t)word_untag((word_t)e, EXPRTAG_FUNC);
}
static inline expr_t expr_makefunc(func_t f)
{
    return (expr_t)word_settag((word_t)f, EXPRTAG_FUNC);
}
static inline func_t expr_getopfunc(expr_t e)
{
    return (func_t)word_striptag((word_t)e);
}
static inline expr_t expr_makevar(var_t v)
{
    return (expr_t)term_var(v);
}
static inline expr_t expr_makebool(bool_t b)
{
    return (expr_t)term_boolean(b);
}
static inline expr_t expr_makenum(num_t n)
{
    return (expr_t)term_num(n);
}

/*
 * Prototypes.
 */
static expr_t expr_compile_eq(typeinst_t type, expr_t x, expr_t y);
static expr_t expr_not_propagate(expr_t a, bool_t *s);
static expr_t expr_getnumfactor(expr_t a, num_t *n);
static expr_t expr_getnotfactor(expr_t a, bool_t *n);
static bool_t expr_getsign(expr_t a);
static add_t add_addexpr_addexpr(expr_t a, expr_t b);
static add_t add_addexpr_expr(expr_t a, expr_t b);
static add_t add_expr_expr(expr_t a, expr_t b);
static add_t add_getnumfactor(expr_t a, num_t *n);
static add_t add_update(add_t add, expr_t k, num_t v);
static expr_t add_to_expr(add_t add);
static add_t neg_addexpr(expr_t a);
static mul_t neg_mulexpr(expr_t a);
static add_t neg_expr(expr_t a);
static mul_t mul_mulexpr_mulexpr(expr_t a, expr_t b);
static add_t mul_addexpr_numexpr(expr_t a, expr_t b);
static add_t mul_expr_numexpr(expr_t a, expr_t b);
static mul_t mul_mulexpr_expr(expr_t a, expr_t b);
static mul_t mul_expr_expr(expr_t a, expr_t b);
static mul_t mul_getnumfactor(expr_t a, num_t *n);
static mul_t mul_update(mul_t mul, expr_t k, num_t v);
static expr_t mul_to_expr(mul_t mul);
static mul_t pow_mulexpr_numexpr(expr_t a, expr_t b);
static mul_t pow_expr_numexpr(expr_t a, expr_t b);
static func_t pow_expr_expr(expr_t a, expr_t b);
static and_t and_andexpr_andexpr(expr_t a, expr_t b);
static and_t and_andexpr_expr(expr_t a, expr_t b);
static and_t and_expr_expr(expr_t a, expr_t b);
static and_t and_getnotfactor(expr_t a, bool_t *n);
static and_t and_update(and_t and, expr_t k, bool_t v);
static expr_t and_to_expr(and_t and);
static or_t or_orexpr_orexpr(expr_t a, expr_t b);
static or_t or_orexpr_expr(expr_t a, expr_t b);
static or_t or_expr_expr(expr_t a, expr_t b);
static or_t or_update(or_t or, expr_t k, bool_t v);
static expr_t or_to_expr(or_t or);
static or_t not_andexpr(expr_t a);
static and_t not_orexpr(expr_t a);
static and_t not_expr(expr_t a);
static func_t cmp_expr_expr(atom_t cmp, expr_t a, expr_t b, bool_t *s);
static expr_t cmp_to_expr(func_t f);
static size_t factor(num_t n, num_t *f, num_t *p);
static size_t factor_uint(uint_t n, num_t *f, num_t *p);

#define FACTOR_MAX          16

/*
 * Pre-canned symbols.
 */
static atom_t SYM_NOT, SYM_AND, SYM_OR, SYM_ADD, SYM_MUL, SYM_IFF, SYM_EQ,
    SYM_LT, SYM_LEQ, SYM_POW;

struct syminfo_s
{
    atom_t atom;
    exprop_t op;
};
typedef struct syminfo_s *syminfo_t;

static struct syminfo_s syminfo[EXPROP_MAX];

static int syminfo_compare(const void *a, const void *b)
{
    syminfo_t infoa = (syminfo_t)a;
    syminfo_t infob = (syminfo_t)b;
    return (int)compare_atom(infoa->atom, infob->atom);
}

/*
 * Get the annotated symbol for the given symbol.
 */
static exprop_t expr_atom_op(atom_t sym)
{
    syminfo_t info = (syminfo_t)bsearch(&sym, syminfo, EXPROP_MAX,
        sizeof(struct syminfo_s), syminfo_compare);
    if (info == NULL)
        return (exprop_t)sym;
    return info->op;
}

/*
 * Initialise this module.
 */
extern void expr_init(void)
{
    SYM_NOT = ATOM_NOT;
    SYM_AND = ATOM_AND;
    SYM_OR  = ATOM_OR;
    SYM_ADD = ATOM_ADD;
    SYM_MUL = ATOM_MUL;
    SYM_IFF = ATOM_IFF;
    SYM_EQ  = ATOM_EQ;
    SYM_LT  = ATOM_LT;
    SYM_LEQ = ATOM_LEQ;
    SYM_POW = make_atom("^", 2);
    size_t i = 0;
    syminfo[i].atom = ATOM_ADD;
    syminfo[i].op   = EXPROP_ADD;
    i++;
    syminfo[i].atom = ATOM_SUB;
    syminfo[i].op   = EXPROP_SUB;
    i++;
    syminfo[i].atom = ATOM_MUL;
    syminfo[i].op   = EXPROP_MUL;
    i++;
    syminfo[i].atom = ATOM_DIV;
    syminfo[i].op   = EXPROP_DIV;
    i++;
    syminfo[i].atom = make_atom("^", 2);
    syminfo[i].op   = EXPROP_POW;
    i++;
    syminfo[i].atom = ATOM_NEG;
    syminfo[i].op   = EXPROP_NEG;
    i++;
    syminfo[i].atom = ATOM_AND;
    syminfo[i].op   = EXPROP_AND;
    i++;
    syminfo[i].atom = ATOM_OR;
    syminfo[i].op   = EXPROP_OR;
    i++;
    syminfo[i].atom = ATOM_IMPLIES;
    syminfo[i].op   = EXPROP_IMPLIES;
    i++;
    syminfo[i].atom = ATOM_IFF;
    syminfo[i].op   = EXPROP_IFF;
    i++;
    syminfo[i].atom = ATOM_XOR;
    syminfo[i].op   = EXPROP_XOR;
    i++;
    syminfo[i].atom = ATOM_NOT;
    syminfo[i].op   = EXPROP_NOT;
    i++;
    syminfo[i].atom = ATOM_EQ;
    syminfo[i].op   = EXPROP_EQ;
    i++;
    syminfo[i].atom = ATOM_NEQ;
    syminfo[i].op   = EXPROP_NEQ;
    i++;
    syminfo[i].atom = ATOM_LT;
    syminfo[i].op   = EXPROP_LT;
    i++;
    syminfo[i].atom = ATOM_LEQ;
    syminfo[i].op   = EXPROP_LEQ;
    i++;
    syminfo[i].atom = ATOM_GT;
    syminfo[i].op   = EXPROP_GT;
    i++;
    syminfo[i].atom = ATOM_GEQ;
    syminfo[i].op   = EXPROP_GEQ;
    i++;
    qsort(syminfo, i, sizeof(struct syminfo_s), syminfo_compare);
}

/*
 * Iterators.
 */
struct funcitr_s
{
    func_t f;
    size_t idx;
};
typedef struct funcitr_s *funcitr_t;

static inline exprtag_t expritr_gettag(expritr_t i)
{
    return (exprtag_t)word_gettag((word_t)i);
}
static inline expritr_t expritr_makefuncitr(funcitr_t i)
{
    return (expritr_t)word_settag((word_t)i, EXPRTAG_OP);
}
static inline expritr_t expritr_makeanditr(anditr_t i)
{
    return (expritr_t)word_settag((word_t)i, EXPRTAG_AND);
}
static inline expritr_t expritr_makeoritr(oritr_t i)
{
    return (expritr_t)word_settag((word_t)i, EXPRTAG_OR);
}
static inline expritr_t expritr_makeadditr(additr_t i)
{
    return (expritr_t)word_settag((word_t)i, EXPRTAG_ADD);
}
static inline expritr_t expritr_makemulitr(mulitr_t i)
{
    return (expritr_t)word_settag((word_t)i, EXPRTAG_MUL);
}
static inline funcitr_t expritr_getfuncitr(expritr_t i)
{
    return (funcitr_t)word_untag((word_t)i, EXPRTAG_OP);
}
static inline anditr_t expritr_getanditr(expritr_t i)
{
    return (anditr_t)word_untag((word_t)i, EXPRTAG_AND);
}
static inline oritr_t expritr_getoritr(expritr_t i)
{
    return (oritr_t)word_untag((word_t)i, EXPRTAG_OR);
}
static inline additr_t expritr_getadditr(expritr_t i)
{
    return (additr_t)word_untag((word_t)i, EXPRTAG_ADD);
}
static inline mulitr_t expritr_getmulitr(expritr_t i)
{
    return (mulitr_t)word_untag((word_t)i, EXPRTAG_MUL);
}

/*
 * Initialize and expritr.
 */
extern expritr_t expritr(expr_t e)
{
    switch (expr_gettag(e))
    {
        case EXPRTAG_OP: case EXPRTAG_FUNC:
        {
            funcitr_t itr = (funcitr_t)gc_malloc(sizeof(struct funcitr_s));
            itr->f = expr_getopfunc(e);
            itr->idx = 0;
            return expritr_makefuncitr(itr);
        }
        case EXPRTAG_AND:
        {
            and_t and = expr_getand(e);
            anditr_t itr = (anditr_t)gc_malloc(and_itrsize(and));
            and_itrinit(itr, and);
            return expritr_makeanditr(itr);
        }
        case EXPRTAG_OR:
        {
            or_t or = expr_getor(e);
            oritr_t itr = (oritr_t)gc_malloc(or_itrsize(or));
            or_itrinit(itr, or);
            return expritr_makeoritr(itr);
        }
        case EXPRTAG_ADD:
        {
            add_t add = expr_getadd(e);
            additr_t itr = (additr_t)gc_malloc(add_itrsize(add));
            add_itrinit(itr, add);
            return expritr_makeadditr(itr);
        }
        case EXPRTAG_MUL:
        {
            mul_t mul = expr_getmul(e);
            mulitr_t itr = (mulitr_t)gc_malloc(mul_itrsize(mul));
            mul_itrinit(itr, mul);
            return expritr_makemulitr(itr);
        }
        default:
            return (expritr_t)NULL;
    }
}

/*
 * Get the current argument.
 */
extern bool expr_get(expritr_t itr, expr_t *arg)
{
    if (itr == (expritr_t)NULL)
        return false;

    switch (expritr_gettag(itr))
    {
        case EXPRTAG_OP:
        {
            funcitr_t fitr = expritr_getfuncitr(itr);
            uint_t a = atom_arity(fitr->f->atom);
            if (a >= fitr->idx)
                return false;
            if (arg != NULL)
                *arg = fitr->f->args[fitr->idx];
            return true;
        }
        case EXPRTAG_AND:
        {
            anditr_t aitr = expritr_getanditr(itr);
            expr_t k; bool_t v;
            if (!and_get(aitr, &k, &v))
                return false;
            if (arg == NULL)
                return true;
            if (v)
                k = expr_not(k);
            *arg = k;
            return true;
        }
        case EXPRTAG_OR:
        {
            oritr_t oitr = expritr_getoritr(itr);
            expr_t k; bool_t v;
            if (!or_get(oitr, &k, &v))
                return false;
            if (arg == NULL)
                return true;
            if (v)
                k = expr_not(k);
            *arg = k;
            return true;
        }
        case EXPRTAG_ADD:
        {
            additr_t aitr = expritr_getadditr(itr);
            expr_t k; num_t v;
            if (!add_get(aitr, &k, &v))
                return false;
            if (arg == NULL)
                return true;
            if (v != expr_makenum(1))
                k = expr_mul(k, expr_makenum(v));
            *arg = k;
            return true;
        }
        case EXPRTAG_MUL:
        {
            mulitr_t mitr = expritr_getmulitr(itr);
            expr_t k; num_t v;
            if (!mul_get(mitr, &k, &v))
                return false;
            if (arg == NULL)
                return true;
            if (v != expr_makenum(1))
                k = expr_pow(k, expr_makenum(v));
            *arg = k;
            return true;
        }
        default:
            return false;
    }
}

/*
 * Get the current argument pair.
 */
extern bool expr_getpair(expritr_t itr, expr_t *arg, expr_t *c)
{
    if (itr == (expritr_t)NULL)
        return false;

    switch (expritr_gettag(itr))
    {
        case EXPRTAG_OP:
        {
            funcitr_t fitr = expritr_getfuncitr(itr);
            uint_t a = atom_arity(fitr->f->atom);
            if (a >= fitr->idx)
                return false;
            if (c != NULL)
                *c = (expr_t)NULL;
            if (arg != NULL)
                *arg = fitr->f->args[fitr->idx];
            return true;
        }
        case EXPRTAG_AND:
        {
            anditr_t aitr = expritr_getanditr(itr);
            bool_t v;
            if (!and_get(aitr, arg, &v))
                return false;
            if (c != NULL)
                *c = expr_makebool(v);
            return true;
        }
        case EXPRTAG_OR:
        {
            oritr_t oitr = expritr_getoritr(itr);
            bool_t v;
            if (!or_get(oitr, arg, &v))
                return false;
            if (c != NULL)
                *c = expr_makebool(v);
            return true;
        }
        case EXPRTAG_ADD:
        {
            additr_t aitr = expritr_getadditr(itr);
            num_t v;
            if (!add_get(aitr, arg, &v))
                return false;
            if (c != NULL)
                *c = expr_makenum(v);
            return true;
        }
        case EXPRTAG_MUL:
        {
            mulitr_t mitr = expritr_getmulitr(itr);
            num_t v;
            if (!mul_get(mitr, arg, &v))
                return false;
            if (c != NULL)
                *c = expr_makenum(v);
            return true;
        }
        default:
            return false;
    }
}

/*
 * Advance the iterator.
 */
extern void expr_next(expritr_t itr)
{
    if (itr == (expritr_t)NULL)
        return;

    exprtag_t t = expritr_gettag(itr);
    switch (t)
    {
        case EXPRTAG_OP:
        {
            funcitr_t fitr = expritr_getfuncitr(itr);
            fitr->idx++;
            return;
        }
        case EXPRTAG_AND:
        {
            anditr_t aitr = expritr_getanditr(itr);
            and_next(aitr);
            return;
        }
        case EXPRTAG_OR:
        {
            oritr_t oitr = expritr_getoritr(itr);
            or_next(oitr);
            return;
        }
        case EXPRTAG_ADD:
        {
            additr_t aitr = expritr_getadditr(itr);
            add_next(aitr);
            return;
        }
        case EXPRTAG_MUL:
        {
            mulitr_t mitr = expritr_getmulitr(itr);
            mul_next(mitr);
            return;
        }
        default:
            panic("expression iterator with invalid tag (%d)", t);
    }
}

/*
 * expr_t type.
 */
extern exprtype_t expr_gettype(expr_t e)
{
    switch (expr_gettag(e))
    {
        case EXPRTAG_VAR: 
            return EXPRTYPE_VAR;
        case EXPRTAG_ATOM:
            return EXPRTYPE_ATOM;
        case EXPRTAG_BOOL:
            return EXPRTYPE_BOOL;
        case EXPRTAG_NUM:
            return EXPRTYPE_NUM;
        case EXPRTAG_STR:
            return EXPRTYPE_STR;
        case EXPRTAG_NIL:
            return EXPRTYPE_NIL;
        default:
            return EXPRTYPE_OP;
    }
}

/*
 * expr_t typeinst.
 */
extern typeinst_t expr_gettypeinst(expr_t e)
{
    switch (expr_gettag(e))
    {
        case EXPRTAG_VAR: 
            return TYPEINST_ANY;
        case EXPRTAG_ATOM:
            return TYPEINST_ATOM;
        case EXPRTAG_BOOL:
            return TYPEINST_BOOL;
        case EXPRTAG_NUM:
            return TYPEINST_NUM;
        case EXPRTAG_STR:
            return TYPEINST_STRING;
        case EXPRTAG_NIL:
            return TYPEINST_NIL;
        case EXPRTAG_AND:
            return TYPEINST_BOOL;
        case EXPRTAG_OR:
            return TYPEINST_BOOL;
        case EXPRTAG_ADD:
            return TYPEINST_NUM;
        case EXPRTAG_MUL:
            return TYPEINST_NUM;
        case EXPRTAG_OP: case EXPRTAG_FUNC:
        {
            func_t f = expr_getopfunc(e);
            typesig_t sig = typeinst_lookup_typesig(f->atom);
            if (sig == TYPESIG_DEFAULT)
                return TYPEINST_ANY;
            return sig->type;
        }
        default:
            return TYPEINST_ANY;
    }
}

/*
 * expr_t arity.
 */
extern size_t expr_arity(expr_t e)
{
    switch (expr_gettag(e))
    {
        case EXPRTAG_VAR: case EXPRTAG_ATOM: case EXPRTAG_BOOL:
        case EXPRTAG_NUM: case EXPRTAG_STR: case EXPRTAG_NIL:
            return 0;
        case EXPRTAG_OP: case EXPRTAG_FUNC:
        {
            func_t f = expr_getopfunc(e);
            return atom_arity(f->atom);
        }
        case EXPRTAG_AND:
        {
            and_t and = expr_getand(e);
            return (and_issingleton(and)? 1: 2);
        }
        case EXPRTAG_OR: case EXPRTAG_ADD: case EXPRTAG_MUL:
            return 2;
        default:
            panic("expression %s is not a function", show(expr_term(e)));
    }
}

/*
 * Make an exprop_t
 */
extern exprop_t exprop_make(const char *name, size_t aty)
{
    atom_t atom = make_atom(name, aty);
    return expr_atom_op(atom);
}

/*
 * Make an exprop_t
 */
extern exprop_t exprop_atom_make(atom_t atom)
{
    return expr_atom_op(atom);
}

/*
 * expr_t op.
 */
extern exprop_t expr_op(expr_t e)
{
    switch (expr_gettag(e))
    {
        case EXPRTAG_OP:
        {
            func_t f = expr_getop(e);
            return expr_atom_op(f->atom);
        }
        case EXPRTAG_FUNC:
        {
            func_t f = expr_getfunc(e);
            return (exprop_t)f->atom;
        }
        case EXPRTAG_AND:
        {
            and_t and = expr_getand(e);
            if (and_issingleton(and))
                return EXPROP_NOT;
            return EXPROP_AND;
        }
        case EXPRTAG_OR:
            return EXPROP_OR;
        case EXPRTAG_ADD:
        {
            add_t add = expr_getadd(e);
            if (add_issingleton(add))
                return EXPROP_MUL;
            return EXPROP_ADD;
        }
        case EXPRTAG_MUL:
        {
            mul_t mul = expr_getmul(e);
            if (mul_issingleton(mul))
                return EXPROP_POW;
            return EXPROP_MUL;
        }
        default:
        {
            panic("expression %s is not a function", show(expr_term(e)));
        }
    }
}

/*
 * Get the name of an exprop_t.
 */
extern const char *exprop_getname(exprop_t op)
{
    switch (op)
    {
        case EXPROP_AND:
            return "/\\";
        case EXPROP_OR:
            return "\\/";
        case EXPROP_IMPLIES:
            return "->";
        case EXPROP_IFF:
            return "<->";
        case EXPROP_XOR:
            return "xor";
        case EXPROP_NOT:
            return "not";
        case EXPROP_EQ:
            return "=";
        case EXPROP_NEQ:
            return "!=";
        case EXPROP_LT:
            return "<";
        case EXPROP_LEQ:
            return "<=";
        case EXPROP_GT:
            return ">";
        case EXPROP_GEQ:
            return ">=";
        case EXPROP_ADD:
            return "+";
        case EXPROP_SUB:
            return "-";
        case EXPROP_MUL:
            return "*";
        case EXPROP_DIV:
            return "/";
        case EXPROP_POW:
            return "^";
        case EXPROP_NEG:
            return "-";
        default:
            return "<unknown>";
    }
}

/*
 * expr_t sym.
 */
extern atom_t expr_sym(expr_t e)
{
    switch (expr_gettag(e))
    {
        case EXPRTAG_OP:
        {
            func_t f = expr_getop(e);
            return f->atom;
        }
        case EXPRTAG_FUNC:
        {
            func_t f = expr_getfunc(e);
            return f->atom;
        }
        case EXPRTAG_AND:
            return SYM_AND;
        case EXPRTAG_OR:
            return SYM_OR;
        case EXPRTAG_ADD:
            return SYM_ADD;
        case EXPRTAG_MUL:
            return SYM_MUL;
        default:
            panic("expression %s is not a function", show(expr_term(e)));
    }
}

/*
 * expr_t args.
 */
extern void expr_args(expr_t e, expr_t *args)
{
    switch (expr_gettag(e))
    {
        case EXPRTAG_OP: case EXPRTAG_FUNC:
        {
            func_t f = expr_getopfunc(e);
            uint_t a = atom_arity(f->atom);
            memcpy(args, f->args, a*sizeof(expr_t));
            return;
        }
        case EXPRTAG_AND:
        {
            and_t and = expr_getand(e);
            if (and_issingleton(and))
            {
                and_search_any(and, args+0, NULL);
                return;
            }
            expr_t k;
            bool_t v;
            and = and_delete_min(and, &k, &v);
            args[0] = (v? expr_not(k): k);
            args[1] = and_to_expr(and);
            return;
        }
        case EXPRTAG_OR:
        {
            or_t or = expr_getor(e);
            expr_t k;
            bool_t v;
            or = or_delete_min(or, &k, &v);
            args[0] = (v? expr_not(k): k);
            args[1] = or_to_expr(or);
            return;
        }
        case EXPRTAG_ADD:
        {
            add_t add = expr_getadd(e);
            num_t v;
            if (add_issingleton(add))
            {
                add_search_any(add, args+1, &v);
                args[0] = expr_makenum(v);
                return;
            }
            expr_t k;
            add = add_delete_min(add, &k, &v);
            if (v != 1)
                k = expr_mul(expr_makenum(v), k);
            args[0] = k;
            args[1] = add_to_expr(add);
            return;
        }
        case EXPRTAG_MUL:
        {
            mul_t mul = expr_getmul(e);
            num_t v;
            if (mul_issingleton(mul))
            {
                mul_search_any(mul, args+0, &v);
                args[1] = expr_makenum(v);
                return;
            }
            expr_t k;
            mul = mul_delete_min(mul, &k, &v);
            if (v != 1)
                k = expr_pow(k, expr_makenum(-1));
            args[0] = k;
            args[1] = mul_to_expr(mul);
            return;
        }
        default:
            panic("expression %s is not a function", show(expr_term(e)));
    }
}

/*
 * expr_t arg.
 */
extern expr_t expr_arg(expr_t e, size_t idx)
{
    switch (expr_gettag(e))
    {
        case EXPRTAG_OP: case EXPRTAG_FUNC:
        {
            func_t f = expr_getopfunc(e);
            uint_t a = atom_arity(f->atom);
            if (idx >= a)
                return (expr_t)NULL;
            return f->args[idx];
        }
        case EXPRTAG_AND:
        {
            and_t and = expr_getand(e);
            if (and_issingleton(and))
            {
                and_search_any(and, &e, NULL);
                return (idx == 0? e: (expr_t)NULL);
            }
            expr_t k; bool_t v;
            switch (idx)
            {
                case 0:
                    and_search_min(and, &k, &v);
                    k = (v? expr_not(k): k);
                    return k;
                case 1:
                    and = and_delete_min(and, NULL, NULL);
                    return and_to_expr(and);
                default:
                    return (expr_t)NULL;
            }
        }
        case EXPRTAG_OR:
        {
            or_t or = expr_getor(e);
            expr_t k; bool_t v;
            switch (idx)
            {
                case 0:
                    or_search_min(or, &k, &v);
                    k = (v? expr_not(k): k);
                    return k;
                case 1:
                    or = or_delete_min(or, NULL, NULL);
                    return or_to_expr(or);
                default:
                    return (expr_t)NULL;
            }
        }
        case EXPRTAG_ADD:
        {
            add_t add = expr_getadd(e);
            expr_t k; num_t v;
            if (add_issingleton(add))
            {
                add_search_any(add, &k, &v);
                switch (idx)
                {
                    case 0:
                        return expr_makenum(v);
                    case 1:
                        return k;
                    default:
                        return (expr_t)NULL;
                }
            }
            switch (idx)
            {
                case 0:
                    add_search_min(add, &k, &v);
                    k = expr_mul(k, expr_makenum(v));
                    return k;
                case 1:
                    add = add_delete_min(add, NULL, NULL);
                    return add_to_expr(add);
                default:
                    return (expr_t)NULL;
            }
        }
        case EXPRTAG_MUL:
        {
            mul_t mul = expr_getmul(e);
            expr_t k; num_t v;
            if (mul_issingleton(mul))
            {
                mul_search_any(mul, &k, &v);
                switch (idx)
                {
                    case 0:
                        return k;
                    case 1:
                        return expr_makenum(v);
                    default:
                        return (expr_t)NULL;
                }
            }
            switch (idx)
            {
                case 0:
                    mul_search_min(mul, &k, &v);
                    k = expr_pow(k, expr_makenum(v));
                    return k;
                case 1:
                    mul = mul_delete_min(mul, &k, &v);
                    return mul_to_expr(mul);
                default:
                    return (expr_t)NULL;
            }
        }
        default:
            return (expr_t)NULL;
    }
}
/*
 * /\-view arity.
 */
extern size_t expr_andview_arity(expr_t e)
{
    if (expr_gettag(e) == EXPRTAG_AND)
    {
        and_t and = expr_getand(e);
        return 2*and_size(and);
    }
    return 2;
}

/*
 * /\-view args.
 */
extern void expr_andview_args(expr_t e, expr_t *args)
{
    switch (expr_gettag(e))
    {
        case EXPRTAG_AND:
        {
            and_t and = expr_getand(e);
            expr_t k;
            bool_t v;
            anditr_t i = anditr(and);
            for (size_t j = 0; and_get(i, &k, &v); j += 2, and_next(i))
            {
                args[j] = expr_makebool(v);
                args[j+1] = k;
            }
            return;
        }
        default:
            args[0] = expr_makebool(false);
            args[1] = e;
            return;
    }
}

/*
 * \/-view arity.
 */
extern size_t expr_orview_arity(expr_t e)
{
    if (expr_gettag(e) == EXPRTAG_OR)
    {
        or_t or = expr_getor(e);
        return 2*or_size(or);
    }
    return 2;
}

/*
 * \/-view args.
 */
extern void expr_orview_args(expr_t e, expr_t *args)
{
    switch (expr_gettag(e))
    {
        case EXPRTAG_OR:
        {
            or_t or = expr_getor(e);
            expr_t k;
            bool_t v;
            oritr_t i = oritr(or);
            for (size_t j = 0; or_get(i, &k, &v); j += 2, or_next(i))
            {
                args[j] = expr_makebool(v);
                args[j+1] = k;
            }
            return;
        }
        case EXPRTAG_AND:
        {
            and_t and = expr_getand(e);
            if (and_issingleton(and))
            {
                and_search_any(and, args+1, NULL);
                args[0] = expr_makebool(true);
                return;
            }
            // Fallthrough
        }
        default:
            args[0] = expr_makebool(false);
            args[1] = e;
            return;
    }
}

/*
 * +-view arity.
 */
extern size_t expr_addview_arity(expr_t e)
{
    if (expr_gettag(e) == EXPRTAG_ADD)
    {
        add_t add = expr_getadd(e);
        return 2*add_size(add);
    }
    return 2;
}

/*
 * +-view args.
 */
extern void expr_addview_args(expr_t e, expr_t *args)
{
    switch (expr_gettag(e))
    {
        case EXPRTAG_ADD:
        {
            add_t add = expr_getadd(e);
            expr_t k;
            num_t v;
            additr_t i = additr(add);
            for (size_t j = 0; add_get(i, &k, &v); j += 2, add_next(i))
            {
                args[j] = expr_makenum(v);
                args[j+1] = k;
            }
            return;
        }
        default:
        {
            num_t n;
            e = expr_getnumfactor(e, &n);
            args[0] = expr_makenum(n);
            args[1] = e;
            return;
        }
    }
}

/*
 * *-view arity.
 */
extern size_t expr_mulview_arity(expr_t e)
{
    if (expr_gettag(e) == EXPRTAG_MUL)
    {
        mul_t mul = expr_getmul(e);
        return 2*mul_size(mul);
    }
    return 2;
}

/*
 * *-view args.
 */
extern void expr_mulview_args(expr_t e, expr_t *args)
{
    switch (expr_gettag(e))
    {
        case EXPRTAG_MUL:
        {
            mul_t mul = expr_getmul(e);
            expr_t k;
            num_t v;
            mulitr_t i = mulitr(mul);
            for (size_t j = 0; mul_get(i, &k, &v); j += 2, mul_next(i))
            {
                args[j] = expr_makenum(v);
                args[j+1] = k;
            }
            return;
        }
        default:
            args[0] = expr_makenum(0);
            args[1] = e;
            return;
    }
}

/*
 * Make an expr_t form argument array.
 */
extern expr_t expr(exprop_t op, expr_t *args)
{
    switch (op)
    {
        case EXPROP_AND:
            return expr_and(args[0], args[1]);
        case EXPROP_OR:
            return expr_or(args[0], args[1]);
        case EXPROP_IMPLIES:
            return expr_implies(args[0], args[1]);
        case EXPROP_IFF:
            return expr_iff(args[0], args[1]);
        case EXPROP_XOR:
            return expr_xor(args[0], args[1]);
        case EXPROP_NOT:
            return expr_not(args[0]);
        case EXPROP_EQ:
            return expr_eq(args[0], args[1]);
        case EXPROP_NEQ:
            return expr_neq(args[0], args[1]);
        case EXPROP_LT:
            return expr_lt(args[0], args[1]);
        case EXPROP_LEQ:
            return expr_leq(args[0], args[1]);
        case EXPROP_GT:
            return expr_gt(args[0], args[1]);
        case EXPROP_GEQ:
            return expr_geq(args[0], args[1]);
        case EXPROP_ADD:
            return expr_add(args[0], args[1]);
        case EXPROP_SUB:
            return expr_sub(args[0], args[1]);
        case EXPROP_MUL:
            return expr_mul(args[0], args[1]);
        case EXPROP_POW:
            return expr_pow(args[0], args[1]);
        case EXPROP_DIV:
            return expr_div(args[0], args[1]);
        case EXPROP_NEG:
            return expr_neg(args[0]);
        default:
        {
            atom_t sym = (atom_t)op;
            func_t f = make_func_a(sym, args);
            return expr_makefunc(f);
        }
    }
}

/*
 * Compile a term to an expr_t.
 */
extern expr_t expr_compile(typeinfo_t info, term_t t)
{
    type_t tt = type(t);
    expr_t e;
    switch (tt)
    {
        case NIL: case ATOM: case BOOL: case NUM: case STR: case VAR:
            return (expr_t)t;
        case FUNC:
        {
            func_t f = func(t);
            atom_t atom = f->atom;
            exprop_t op = expr_atom_op(atom);
            if (op < EXPROP_MAX)
            {
                // Built-in operator:
                size_t arity = atom_arity(atom);
                switch (arity)
                {
                    case 1:
                    {
                        expr_t a = expr_compile(info, f->args[0]);
                        switch (op)
                        {
                            case EXPROP_NEG:
                                return expr_neg(a);
                            case EXPROP_NOT:
                                return expr_not(a);
                            default:
                                panic("bad unary operator code (%d)", op);
                        }
                    }
                    case 2:
                    {
                        expr_t a = expr_compile(info, f->args[0]);
                        expr_t b = expr_compile(info, f->args[1]);
                        switch (op)
                        {
                            case EXPROP_ADD:
                                return expr_add(a, b);
                            case EXPROP_SUB:
                                return expr_sub(a, b);
                            case EXPROP_MUL:
                                return expr_mul(a, b);
                            case EXPROP_DIV:
                                return expr_div(a, b);
                            case EXPROP_POW:
                                return expr_pow(a, b);
                            case EXPROP_AND:
                                return expr_and(a, b);
                            case EXPROP_OR:
                                return expr_or(a, b);
                            case EXPROP_IMPLIES:
                                return expr_implies(a, b);
                            case EXPROP_IFF:
                                return expr_iff(a, b);
                            case EXPROP_XOR:
                                return expr_xor(a, b);
                            case EXPROP_EQ:
                            {
                                typeinst_t type = typecheck_typeof(info,
                                    f->args[0]);
                                return expr_compile_eq(type, a, b);
                            }
                            case EXPROP_NEQ:
                            {
                                typeinst_t type = typecheck_typeof(info,
                                    f->args[0]);
                                e = expr_compile_eq(type, a, b);
                                return expr_not(e);
                            }
                            case EXPROP_LT:
                                return expr_lt(a, b);
                            case EXPROP_LEQ:
                                return expr_leq(a, b);
                            case EXPROP_GT:
                                return expr_gt(a, b);
                            case EXPROP_GEQ:
                                return expr_geq(a, b);
                            default:
                                panic("bad binary operator code (%d)", op);
                        }
                    }
                    default:
                        panic("bad operator arity (%zu)", arity);
                }
            }
            else
            {
                // User function:
                size_t a = atom_arity(atom);
                func_t fe = (func_t)gc_malloc(sizeof(struct func_s) +
                    a*sizeof(term_t));
                fe->atom = atom;
                for (size_t i = 0; i < a; i++)
                    fe->args[i] = expr_compile(info, f->args[i]);
                return expr_makefunc(fe);
            }
        }
        default:
            panic("bad term type (%d)", tt);
    }
}

/*
 * Compile x = y based on type.
 */
static expr_t expr_compile_eq(typeinst_t type, expr_t x, expr_t y)
{
    switch (type)
    {
        case TYPEINST_BOOL:
            return expr_iff(x, y);
        case TYPEINST_NUM: case TYPEINST_ANY:
            return expr_eq(x, y);
        case TYPEINST_NIL:
            return expr_make(expr_atom_op(ATOM_NIL_EQ), x, y);
        case TYPEINST_STRING:
            return expr_make(expr_atom_op(ATOM_STR_EQ), x, y);
        case TYPEINST_ATOM:
            return expr_make(expr_atom_op(ATOM_ATOM_EQ), x, y);
        default:
        {
            const char *format = "%s_eq";
            int r = snprintf(NULL, 0, format, typeinst_show(type));
            if (r <= 0)
            {
expr_compile_eq_error:
                panic("snprintf failed to create eq name");
            }
            char buf[r+1];
            int s = snprintf(buf, sizeof(buf), format, typeinst_show(type));
            if (s != r)
                goto expr_compile_eq_error;
            atom_t atom = make_atom(buf, 2);

            typeinst_t tt = typeinst_make_var(type);
            typesig_t sig = make_typesig(TYPEINST_BOOL, tt, tt);
            if (!typeinst_declare(atom, sig))
            {
                // TODO: make this an error.
                panic("failed to declare implied type for equality constraint "
                    "`!y%s/%zu!d'", buf, 2);
            }
            return expr_make(expr_atom_op(atom), x, y);
        }
    }
}

/****************************************************************************/
/* ARITHMETIC                                                               */
/****************************************************************************/

/*
 * a + b
 */
extern expr_t expr_add(expr_t a, expr_t b)
{
    exprtag_t ta = expr_gettag(a);
    exprtag_t tb = expr_gettag(b);
    switch (ta)
    {
        case EXPRTAG_ADD:
            switch (tb)
            {
                case EXPRTAG_ADD:
                    return add_to_expr(add_addexpr_addexpr(a, b));
                default:
                    return add_to_expr(add_addexpr_expr(a, b));
            }
        case EXPRTAG_NUM:
            switch (tb)
            {
                case EXPRTAG_ADD:
                    return add_to_expr(add_addexpr_expr(b, a));
                case EXPRTAG_NUM:
                    return expr_makenum(expr_getnum(a) + expr_getnum(b));
                default:
                    return add_to_expr(add_expr_expr(a, b));
            }
        default:
            switch (tb)
            {
                case EXPRTAG_ADD:
                    return add_to_expr(add_addexpr_expr(b, a));
                default:
                    return add_to_expr(add_expr_expr(a, b));
            }
    }
}

/*
 * a - b
 */
extern expr_t expr_sub(expr_t a, expr_t b)
{
    b = expr_neg(b);
    return expr_add(a, b);
}

/*
 * -a
 */
extern expr_t expr_neg(expr_t a)
{
    exprtag_t ta = expr_gettag(a);
    switch (ta)
    {
        case EXPRTAG_ADD:
            return add_to_expr(neg_addexpr(a));
        case EXPRTAG_MUL:
            return expr_makemul(neg_mulexpr(a));
        case EXPRTAG_NUM:
            return expr_makenum(-expr_getnum(a));
        default:
            return expr_makeadd(neg_expr(a));
    }
}

/*
 * a * b
 */
extern expr_t expr_mul(expr_t a, expr_t b)
{
    exprtag_t ta = expr_gettag(a);
    exprtag_t tb = expr_gettag(b);
    switch (ta)
    {
        case EXPRTAG_MUL:
            switch (tb)
            {
                case EXPRTAG_MUL:
                    return mul_to_expr(mul_mulexpr_mulexpr(a, b));
                default:
                    return mul_to_expr(mul_mulexpr_expr(a, b));
            }
        case EXPRTAG_ADD:
            switch (tb)
            {
                case EXPRTAG_MUL:
                    return mul_to_expr(mul_mulexpr_expr(b, a));
                case EXPRTAG_NUM:
                    return add_to_expr(mul_addexpr_numexpr(a, b));
                default:
                    return mul_to_expr(mul_expr_expr(a, b));
            }
        case EXPRTAG_NUM:
            switch (tb)
            {
                case EXPRTAG_MUL:
                    return mul_to_expr(mul_mulexpr_expr(b, a));
                case EXPRTAG_ADD:
                    return add_to_expr(mul_addexpr_numexpr(b, a));
                case EXPRTAG_NUM:
                    return expr_makenum(expr_getnum(a) * expr_getnum(b));
                default:
                    return add_to_expr(mul_expr_numexpr(b, a));
            }
        default:
            switch (tb)
            {
                case EXPRTAG_MUL:
                    return mul_to_expr(mul_mulexpr_expr(b, a));
                case EXPRTAG_NUM:
                    return add_to_expr(mul_expr_numexpr(a, b));
                default:
                    return mul_to_expr(mul_expr_expr(a, b));
            }
    }
}

/*
 * a / b
 */
extern expr_t expr_div(expr_t a, expr_t b)
{
    b = expr_inv(b);
    return expr_mul(a, b);
}

/*
 * 1 / a
 */
extern expr_t expr_inv(expr_t a)
{
    exprtag_t ta = expr_gettag(a);
    switch (ta)
    {
        case EXPRTAG_MUL:
            return expr_makemul(pow_mulexpr_numexpr(a, expr_makenum(-1)));
        default:
            return expr_makemul(pow_expr_numexpr(a, expr_makenum(-1)));
    }
}

/*
 * a ^ b
 */
extern expr_t expr_pow(expr_t a, expr_t b)
{
    exprtag_t ta = expr_gettag(a);
    exprtag_t tb = expr_gettag(b);
    switch (tb)
    {
        case EXPRTAG_NUM:
            switch (ta)
            {
                case EXPRTAG_NUM:
                {
                    num_t nb = expr_getnum(b);
                    if (nb >= 0)
                        return expr_makenum(pow(expr_getnum(a), nb));
                    return mul_to_expr(pow_expr_numexpr(a, b));
                }
                case EXPRTAG_MUL:
                    return mul_to_expr(pow_mulexpr_numexpr(a, b));
                default:
                    return mul_to_expr(pow_expr_numexpr(a, b));
            }
        default:
            switch (ta)
            {
                case EXPRTAG_OP:
                {
                    func_t f = expr_getop(a);
                    if (f->atom == SYM_POW)
                    {
                        a = f->args[0];
                        b = expr_mul(b, f->args[1]);
                    }
                    // Fallthrough
                }
            default:
                return expr_makeop(pow_expr_expr(a, b));
            }
    }
}

/****************************************************************************/
/* BOOLEAN                                                                  */
/****************************************************************************/

/*
 * a /\ b
 */
extern expr_t expr_and(expr_t a, expr_t b)
{
    exprtag_t ta = expr_gettag(a);
    exprtag_t tb = expr_gettag(b);
    switch (ta)
    {
        case EXPRTAG_AND:
            switch (tb)
            {
                case EXPRTAG_AND:
                    return and_to_expr(and_andexpr_andexpr(a, b));
                case EXPRTAG_BOOL:
                    if (expr_getbool(b))
                        return a;
                    else
                        return b;
                default:
                    return and_to_expr(and_andexpr_expr(a, b));

            }
        case EXPRTAG_BOOL:
            if (expr_getbool(a))
                return b;
            else
                return a;
        default:
            switch (tb)
            {
                case EXPRTAG_AND:
                    return and_to_expr(and_andexpr_expr(b, a));
                case EXPRTAG_BOOL:
                    if (expr_getbool(b))
                        return a;
                    else
                        return b;
                default:
                    return and_to_expr(and_expr_expr(a, b));
            }
    }
}

/*
 * a \/ b
 */
extern expr_t expr_or(expr_t a, expr_t b)
{
    exprtag_t ta = expr_gettag(a);
    exprtag_t tb = expr_gettag(b);
    switch (ta)
    {
        case EXPRTAG_OR:
            switch (tb)
            {
                case EXPRTAG_OR:
                    return or_to_expr(or_orexpr_orexpr(a, b));
                case EXPRTAG_BOOL:
                    if (expr_getbool(b))
                        return b;
                    else
                        return a;
                default:
                    return or_to_expr(or_orexpr_expr(a, b));
            }
        case EXPRTAG_BOOL:
            if (expr_getbool(a))
                return a;
            else
                return b;
        default:
            switch (tb)
            {
                case EXPRTAG_OR:
                    return or_to_expr(or_orexpr_expr(b, a));
                case EXPRTAG_BOOL:
                    if (expr_getbool(b))
                        return b;
                    else
                        return a;
                default:
                    return or_to_expr(or_expr_expr(a, b));
            }
    }
}

/*
 * a -> b
 */
extern expr_t expr_implies(expr_t a, expr_t b)
{
    return expr_or(expr_not(a), b);
}

/*
 * a <-> b
 */
extern expr_t expr_iff(expr_t a, expr_t b)
{
    if (expr_gettag(a) == EXPRTAG_BOOL)
    {
        bool_t ba = expr_getbool(a);
        return (ba? b: expr_not(b));
    }
    if (expr_gettag(b) == EXPRTAG_BOOL)
    {
        bool_t bb = expr_getbool(b);
        return (bb? a: expr_not(a));
    }

    func_t f;
    int_t r = expr_compare(a, b);
    if (r < 0)
        f = make_func(SYM_IFF, a, b);
    else if (r > 0)
        f = make_func(SYM_IFF, b, a);
    else
        return expr_makebool(true);

    return expr_makeop(f);
}

/*
 * a xor b
 */
extern expr_t expr_xor(expr_t a, expr_t b)
{
    return expr_not(expr_iff(a, b));
}

/*
 * not a
 */
extern expr_t expr_not(expr_t a)
{
    bool_t s;
    a = expr_not_propagate(a, &s);
    if (s)
        return and_to_expr(not_expr(a));
    return a;
}

/*
 * Propagate negation if possible.
 */
static expr_t expr_not_propagate(expr_t a, bool_t *s)
{
    switch (expr_gettag(a))
    {
        case EXPRTAG_AND:
            *s = false;
            return or_to_expr(not_andexpr(a));
        case EXPRTAG_OR:
            *s = false;
            return and_to_expr(not_orexpr(a));
        case EXPRTAG_BOOL:
            *s = false;
            return expr_makebool(!expr_getbool(a));
        default:
            *s = true;
            return a;
    }
}

/****************************************************************************/
/* FUNCTIONS                                                                */
/****************************************************************************/

/*
 * a = b
 */
extern expr_t expr_eq(expr_t a, expr_t b)
{
    bool_t s;
    func_t f = cmp_expr_expr(SYM_EQ, a, b, &s);
    expr_t c = cmp_to_expr(f);
    return c;
}

/*
 * a != b
 */
extern expr_t expr_neq(expr_t a, expr_t b)
{
    bool_t s;
    func_t f = cmp_expr_expr(SYM_EQ, a, b, &s);
    expr_t c = cmp_to_expr(f);
    c = expr_not(c);
    return c;
}

/*
 * a < b
 */
extern expr_t expr_lt(expr_t a, expr_t b)
{
    bool_t s;
    func_t f = cmp_expr_expr(SYM_LT, a, b, &s);
    expr_t c = cmp_to_expr(f);
    if (s)
        c = expr_not(c);
    return c;
}

/*
 * a <= b
 */
extern expr_t expr_leq(expr_t a, expr_t b)
{
    bool_t s;
    func_t f = cmp_expr_expr(SYM_LEQ, a, b, &s);
    expr_t c = cmp_to_expr(f);
    if (s)
        c = expr_not(c);
    return c;
}

/*
 * a > b
 */
extern expr_t expr_gt(expr_t a, expr_t b)
{
    bool_t s;
    func_t f = cmp_expr_expr(SYM_LT, b, a, &s);
    expr_t c = cmp_to_expr(f);
    if (s)
        c = expr_not(c);
    return c;
}

/*
 * a >= b
 */
extern expr_t expr_geq(expr_t a, expr_t b)
{
    bool_t s;
    func_t f = cmp_expr_expr(SYM_LEQ, b, a, &s);
    expr_t c = cmp_to_expr(f);
    if (s)
        c = expr_not(c);
    return c;
}

/****************************************************************************/
/* MISC                                                                     */
/****************************************************************************/

/*
 * Split an expr_t a into a constant times another expr_t.
 */
static expr_t expr_getnumfactor(expr_t a, num_t *n)
{
    switch (expr_gettag(a))
    {
        case EXPRTAG_NUM:
            *n = expr_getnum(a);
            return expr_makenum(1);
        case EXPRTAG_ADD:
        {
            add_t add = add_getnumfactor(a, n);
            return add_to_expr(add);
        }
        case EXPRTAG_MUL:
        {
            mul_t mul = mul_getnumfactor(a, n);
            return expr_makemul(mul);
        }
        default:
            *n = 1;
            return a;
    }
}

/*
 * Split an expr_t into a Boolean sign and another expr_t.
 */
static expr_t expr_getnotfactor(expr_t a, bool_t *n)
{
    switch (expr_gettag(a))
    {
        case EXPRTAG_AND:
        {
            and_t and = and_getnotfactor(a, n);
            return and_to_expr(and);
        }
        default:
            *n = false;
            return a;
    }
}

/*
 * Get the sign of an expr_t.
 */
static bool_t expr_getsign(expr_t a)
{
    switch (expr_gettag(a))
    {
        case EXPRTAG_NUM:
            return (expr_getnum(a) < 0);
        case EXPRTAG_ADD:
        {
            num_t n;
            add_search_min(expr_getadd(a), NULL, &n);
            return (n < 0);
        }
        case EXPRTAG_MUL:
            return mul_search(expr_getmul(a), expr_makenum(-1), NULL);
        default:
            return false;
    }
}

/****************************************************************************/
/* COMPARISON                                                               */
/****************************************************************************/

extern int_t expr_compare(expr_t a, expr_t b)
{
    if (a == b)
        return 0;
    exprtag_t ta = expr_gettag(a);
    exprtag_t tb = expr_gettag(b);
    int_t r = (int_t)ta - (int_t)tb;
    if (r != 0)
        return r;
    switch (ta)
    {
        case EXPRTAG_VAR: 
            return compare_var(expr_getvar(a), expr_getvar(b));
        case EXPRTAG_BOOL:
            return compare_boolean(expr_getbool(a), expr_getbool(b));
        case EXPRTAG_NUM:
            return compare_num(expr_getnum(a), expr_getnum(b));
        case EXPRTAG_OP: case EXPRTAG_FUNC:
        {
            func_t fa = expr_getopfunc(a);
            func_t fb = expr_getopfunc(b);
            r = (int_t)fa->atom - (int_t)fb->atom;
            if (r != 0)
                return r;
            uint_t a = atom_arity(fa->atom);
            for (uint_t i = 0; i < a; i++)
            {
                r = expr_compare((expr_t)fa->args[i], (expr_t)fb->args[i]);
                if (r != 0)
                    return r;
            }
            return 0;
        }
        case EXPRTAG_ADD:
        {
            add_t adda = expr_getadd(a), addb = expr_getadd(b);
            size_t sa = add_depth(adda), sb = add_depth(addb);
            r = (int_t)sa - (int_t)sb;
            if (r != 0)
                return r;
            expr_t ka, kb;
            num_t va, vb;
            additr_t ia, ib;
            for (ia = additr(adda), ib = additr(addb);
                    add_get(ia, &ka, &va); add_next(ia), add_next(ib))
            {
                if (!add_get(ib, &kb, &vb))
                    return 1;
                if (va != vb)
                    return (va < vb? -1: 1);
                r = expr_compare(ka, kb);
                if (r != 0)
                    return r;
            }
            if (add_get(ib, NULL, NULL))
                return -1;
            return 0;
        }
        case EXPRTAG_MUL:
        {
            mul_t mula = expr_getmul(a), mulb = expr_getmul(b);
            size_t sa = mul_depth(mula), sb = mul_depth(mulb);
            r = (int_t)sa - (int_t)sb;
            if (r != 0)
                return r;
            expr_t ka, kb;
            num_t va, vb;
            mulitr_t ia, ib;
            for (ia = mulitr(mula), ib = mulitr(mulb);
                    mul_get(ia, &ka, &va); mul_next(ia), mul_next(ib))
            {
                if (!mul_get(ib, &kb, &vb))
                    return 1;
                if (va != vb)
                    return (va < vb? -1: 1);
                r = expr_compare(ka, kb);
                if (r != 0)
                    return r;
            }
            if (mul_get(ib, NULL, NULL))
                return -1;
            return 0;
        }
        case EXPRTAG_AND:
        {
            and_t anda = expr_getand(a), andb = expr_getand(b);
            size_t sa = and_depth(anda), sb = and_depth(andb);
            r = (int_t)sa - (int_t)sb;
            if (r != 0)
                return r;
            expr_t ka, kb;
            bool_t va, vb;
            anditr_t ia, ib;
            for (ia = anditr(anda), ib = anditr(andb);
                    and_get(ia, &ka, &va); and_next(ia), and_next(ib))
            {
                if (!and_get(ib, &kb, &vb))
                    return 1;
                r = (int_t)va - (int_t)vb;
                if (r != 0)
                    return r;
                r = expr_compare(ka, kb);
                if (r != 0)
                    return r;
            }
            if (and_get(ib, NULL, NULL))
                return -1;
            return 0;
        }
        case EXPRTAG_OR:
        {
            or_t ora = expr_getor(a), orb = expr_getor(b);
            size_t sa = or_depth(ora), sb = or_depth(orb);
            r = (int_t)sa - (int_t)sb;
            if (r != 0)
                return r;
            expr_t ka, kb;
            bool_t va, vb;
            oritr_t ia, ib;
            for (ia = oritr(ora), ib = oritr(orb);
                    or_get(ia, &ka, &va); or_next(ia), or_next(ib))
            {
                if (!or_get(ib, &kb, &vb))
                    return 1;
                r = (int_t)va - (int_t)vb;
                if (r != 0)
                    return r;
                r = expr_compare(ka, kb);
                if (r != 0)
                    return r;
            }
            if (or_get(ib, NULL, NULL))
                return -1;
            return 0;
        }
        default:
            return 0;
    }
}

/****************************************************************************/
/* CONVERSION                                                               */
/****************************************************************************/

/*
 * Convert an expr_t into a term_t.
 */
extern term_t expr_term(expr_t e)
{
    exprtag_t et = expr_gettag(e);
    switch (et)
    {
        case EXPRTAG_VAR: case EXPRTAG_ATOM: case EXPRTAG_BOOL:
        case EXPRTAG_NUM: case EXPRTAG_STR: case EXPRTAG_NIL:
            return (term_t)e;
        case EXPRTAG_OP: case EXPRTAG_FUNC:
        {
            func_t fe = expr_getopfunc(e);
            size_t a = atom_arity(fe->atom);
            func_t ft = (func_t)gc_malloc(sizeof(struct func_s) +
                a*sizeof(term_t));
            ft->atom = fe->atom;
            for (size_t i = 0; i < a; i++)
                ft->args[i] = expr_term(fe->args[i]);
            term_t tt = term_func(ft);
            return tt;
        }
        case EXPRTAG_AND:
        {
            and_t and = expr_getand(e);
            expr_t k; bool_t v; term_t tt = (term_t)NULL;
            for (anditr_t i = anditr(and); and_get(i, &k, &v); and_next(i))
            {
                term_t t = expr_term(k);
                t = (v? term_func(make_func(SYM_NOT, t)): t);
                tt = (tt == (term_t)NULL? t:
                    term_func(make_func(SYM_AND, tt, t)));
            }
            return tt;
        }
        case EXPRTAG_OR:
        {
            or_t or = expr_getor(e);
            expr_t k; bool_t v; term_t tt = (term_t)NULL;
            for (oritr_t i = oritr(or); or_get(i, &k, &v); or_next(i))
            {
                term_t t = expr_term(k);
                t = (v? term_func(make_func(SYM_NOT, t)): t);
                tt = (tt == (term_t)NULL? t:
                    term_func(make_func(SYM_OR, tt, t)));
            }
            return tt;
        }
        case EXPRTAG_ADD:
        {
            add_t add = expr_getadd(e);
            expr_t k; num_t v; term_t tt = (term_t)NULL;
            for (additr_t i = additr(add); add_get(i, &k, &v); add_next(i))
            {
                term_t t;
                if (k == expr_num(1))
                    t = term_num(v);
                else
                {
                    t = expr_term(k);
                    t = (v != 1?
                        term_func(make_func(SYM_MUL, term_num(v), t)): t);
                }
                tt = (tt == (term_t)NULL? t:
                    term_func(make_func(SYM_ADD, tt, t)));
            }
            return tt;
        }
        case EXPRTAG_MUL:
        {
            mul_t mul = expr_getmul(e);
            expr_t k; num_t v; term_t tt = (term_t)NULL;
            for (mulitr_t i = mulitr(mul); mul_get(i, &k, &v); mul_next(i))
            {
                term_t t = expr_term(k);
                t = (v != 1? term_func(make_func(SYM_POW, t, term_num(v))): t);
                tt = (tt == (term_t)NULL? t:
                    term_func(make_func(SYM_MUL, tt, t)));
            }
            return tt;
        }
        default:
            panic("bad expr tag (%d)", et);
    }
}

/****************************************************************************/
/* ADDITION                                                                 */
/****************************************************************************/

/*
 * Add add_t exprs.
 */
static add_t add_addexpr_addexpr(expr_t a, expr_t b)
{
    add_t adda = expr_getadd(a);
    add_t addb = expr_getadd(b);
    size_t sa = add_depth(adda);
    size_t sb = add_depth(addb);
    if (sb < sa)
    {
        add_t addt = adda;
        adda = addb;
        addb = addt;
    }
    expr_t k;
    num_t v;
    for (additr_t i = additr(adda); add_get(i, &k, &v); add_next(i))
        addb = add_update(addb, k, v);
    return addb;
}

/*
 * Add a add_t with a (non-add_t) expr.
 */
static add_t add_addexpr_expr(expr_t a, expr_t b)
{
    num_t n;
    add_t adda = expr_getadd(a);
    b = expr_getnumfactor(b, &n);
    if (n == 0)
        return adda;
    adda = add_update(adda, b, n);
    return adda;
}

/*
 * Add two non-add_t exprs.
 */
static add_t add_expr_expr(expr_t a, expr_t b)
{
    add_t add = add_init();
    add = add_addexpr_expr(expr_makeadd(add), a);
    add = add_addexpr_expr(expr_makeadd(add), b);
    return add;
}

/*
 * Get the num_t factor of an add_t.
 */
static word_t mapval_div(word_t d, word_t k, word_t v)
{
    num_t n = word_getdouble(d);
    num_t m = word_getdouble(v);
    return word_makedouble(m / n);
}
static add_t add_getnumfactor(expr_t a, num_t *n)
{
    add_t add = expr_getadd(a);
    expr_t k;
    num_t v, m;
    additr_t i = additr(add);
    add_get(i, NULL, &m);
    for (; m != 1 && add_get(i, &k, &v); add_next(i))
        m = (num_t)gcd((int64_t)m, (int64_t)v);
    if (m == 1)
    {
        *n = 1;
        return add;
    }
    add = add_map(add, word_makedouble(m), mapval_div);
    *n = m;
    return add;
}

/*
 * Update an add_t.
 */
static add_t add_update(add_t add, expr_t k, num_t v)
{
    if (v == 0)
        return add;
    num_t n;
    if (add_search(add, k, &n))
    {
        v += n;
        if (v == 0)
            return add_delete(add, k, NULL);
    }
    return add_insert(add, k, v);
}

/*
 * Normalize an add_t to an expr_t.
 */
static expr_t add_to_expr(add_t add)
{
    switch (add_depth(add))
    {
        case 0:
            return expr_makenum(0);
        case 1:
            if (add_size(add) == 1)
                break;
        default:
            return expr_makeadd(add);
    }

    expr_t k;
    num_t v;
    add_search_any(add, &k, &v);
    if (k == expr_makenum(1))
        return expr_makenum(v);
    else if (v == 1)
        return k;
    else
        return expr_makeadd(add);
}

/***************************************************************************/
/* NEGATION                                                                */
/***************************************************************************/

/*
 * Negate a add_t.
 */
static word_t mapval_neg(word_t a, word_t k, word_t v)
{
    return word_makedouble(-word_getdouble(v));
}
static add_t neg_addexpr(expr_t a)
{
    add_t add = expr_getadd(a);
    add = add_map(add, (word_t)NULL, mapval_neg);
    return add;
}

/*
 * Negate a mul_t.
 */
static mul_t neg_mulexpr(expr_t a)
{
    mul_t mul = expr_getmul(a);
    expr_t k = expr_makenum(-1);
    if (mul_search(mul, k, NULL))
        mul = mul_delete(mul, k, NULL);
    else
        mul = mul_insert(mul, k, 1);
    return mul;
}

/*
 * Negate a (non-add_t nor mul_t) expr.
 */
static add_t neg_expr(expr_t a)
{
    add_t add = add_init();
    add = add_insert(add, a, -1);
    return add;
}

/***************************************************************************/
/* MULIPLICATION                                                           */
/***************************************************************************/

/*
 * Multiply mul_t exprs.
 */
static mul_t mul_mulexpr_mulexpr(expr_t a, expr_t b)
{
    mul_t mula = expr_getmul(a);
    mul_t mulb = expr_getmul(b);
    size_t sa = mul_depth(mula);
    size_t sb = mul_depth(mulb);
    if (sb < sa)
    {
        mul_t mult = mula;
        mula = mulb;
        mulb = mult;
    }
    expr_t k;
    num_t v;
    for (mulitr_t i = mulitr(mula); mul_get(i, &k, &v); mul_next(i))
        mulb = mul_update(mulb, k, v);
    return mulb;
}

/*
 * Multiply mul_t with a (non-mul_t) expr.
 */
static mul_t mul_mulexpr_expr(expr_t a, expr_t b)
{
    num_t n;
    b = expr_getnumfactor(b, &n);
    if (n == 0)
        return mul_insert(mul_init(), 0, 1);
    mul_t mula = expr_getmul(a);
    if (n != 1)
    {
        num_t f[FACTOR_MAX], p[FACTOR_MAX];
        size_t l = factor(n, f, p);
        for (size_t i = 0; i < l; i++)
            mula = mul_update(mula, expr_makenum(f[i]), p[i]);
    }
    if (b == expr_makenum(1))
        return mula;
    mula = mul_update(mula, b, 1);
    return mula;
}

/*
 * Multiply an add_t with a num_t.
 */
static add_t mul_addexpr_numexpr(expr_t a, expr_t b)
{
    num_t n = expr_getnum(b);
    if (n == 0)
        return add_init();
    add_t add = expr_getadd(a);
    expr_t k;
    num_t v;
    add_t addb = add_init();
    for (additr_t i = additr(add); add_get(i, &k, &v); add_next(i))
        addb = add_insert(addb, k, n*v);
    return addb;
}

/*
 * Multiple an (non-add, non-mul) expr with a num_t.
 */
static add_t mul_expr_numexpr(expr_t a, expr_t b)
{
    num_t n = expr_getnum(b);
    if (n == 0)
        return add_init();
    add_t add = add_init();
    add = add_insert(add, a, n);
    return add;
}

/*
 * Multiply two non-mul_t exprs.
 */
static mul_t mul_expr_expr(expr_t a, expr_t b)
{
    mul_t mul = mul_init();
    mul = mul_mulexpr_expr(expr_makemul(mul), a);
    mul = mul_mulexpr_expr(expr_makemul(mul), b);
    return mul;
}

/*
 * Get the num_t factor of a mul_t.
 */
static mul_t mul_getnumfactor(expr_t a, num_t *n)
{
    mul_t mul = expr_getmul(a);
    expr_t k;
    num_t v, m = 1;
    for (mulitr_t i = mulitr_geq(mul, expr_makenum(-inf));
            mul_get(i, &k, &v); mul_next(i))
    {
        if (expr_gettag(k) != EXPRTAG_NUM)
            break;
        if (v < 0)
            continue;
        mul = mul_delete(mul, k, NULL);
        m *= pow(expr_getnum(k), v);
    }

    *n = m;
    return mul;
}

/*
 * Update a mul_t.
 */
static mul_t mul_update(mul_t mul, expr_t k, num_t v)
{
    if (v == 0)
        return mul;
    num_t n;
    if (mul_search(mul, k, &n))
    {
        v += n;
        if (k == expr_makenum(-1))
            v = (num_t)((uint_t)v % 2);
        if (v == 0)
            return mul_delete(mul, k, NULL);
    }
    return mul_insert(mul, k, v);
}

/*
 * Normalise a mul_t to an expr_t.
 */
static expr_t mul_to_expr(mul_t mul)
{
    expr_t k;
    num_t v;
    switch (mul_depth(mul))
    {
        case 0:
            return expr_makenum(1);
        case 1:
            if (mul_size(mul) == 1)
            {
                mul_search_any(mul, &k, &v);
                if (v == 1)
                    return k;
            }
            break;
        default:
            break;
    }

    if (mul_search_lt(mul, expr_makenum(-inf), &k, &v))
    {
        if (mul_search_lt(mul, k, NULL, NULL))
            return expr_makemul(mul);
        if (mul_search_gt(mul, expr_makenum(inf), NULL, NULL))
            return expr_makemul(mul);
    }
    else if (mul_search_gt(mul, expr_makenum(inf), &k, &v))
    {
        if (mul_search_gt(mul, k, NULL, NULL))
            return expr_makemul(mul);
    }
    else
        k = (expr_t)NULL;

    // attempt to evaluate to integer:
    num_t n = 1;
    expr_t k1;
    num_t v1;
    for (mulitr_t i = mulitr_geq(mul, expr_makenum(-inf));
            mul_get(i, &k1, &v1) && expr_gettag(k1) == EXPRTAG_NUM;
            mul_next(i))
    {
        if (v1 < 0)
            return expr_makemul(mul);
        n *= pow(expr_getnum(k1), v1);
    }

    if (k == (expr_t)NULL)
        return expr_makenum(n);
    if (v != 1)
    {
        mul_t mul = mul_init();
        mul = mul_insert(mul, k, v);
        k = expr_makemul(mul);
    }
    else if (expr_gettag(k) == EXPRTAG_ADD)
        return add_to_expr(mul_addexpr_numexpr(k, expr_makenum(n)));
    if (n == 1)
        return k;
    add_t add = add_init();
    add = add_insert(add, k, n);
    return expr_makeadd(add);
}

/****************************************************************************/
/* POWER                                                                    */
/****************************************************************************/

/*
 * Power of a mul_t expr and num_t expr.
 */
static word_t mapval_pow(word_t a, word_t k, word_t v)
{
    num_t p = word_getdouble(a);
    num_t n = word_getdouble(v);
    n *= p;
    if (k == expr_makenum(-1))
        n = (num_t)((int_t)n % 2);
    return word_makedouble(n);
}
static mul_t pow_mulexpr_numexpr(expr_t a, expr_t b)
{
    num_t n = expr_getnum(b);
    mul_t mul = expr_getmul(a);
    mul = mul_map(mul, word_makedouble(n), mapval_pow);
    expr_t k = expr_makenum(-1);
    num_t v;
    if (mul_search(mul, k, &v) && v == 0)
        mul = mul_delete(mul, k, NULL);
    return mul;
}

/*
 * Power of a non-mul_t expr and num_t expr.
 */
static mul_t pow_expr_numexpr(expr_t a, expr_t b)
{
    num_t n, m = expr_getnum(b);
    mul_t mul = mul_init();
    if (m == 1)
    {
        mul = mul_insert(mul, a, 1);
        return mul;
    }
    a = expr_getnumfactor(a, &n);
    if (n == 0)
    {
        if (m <= 0)
            panic("negative exponent");     // TODO: Make this an error.
        mul = mul_insert(mul, expr_makenum(0), 1);
        return mul;
    }
    if (m == 0)
    {
        mul = mul_insert(mul, expr_makenum(1), 1);
        return mul;
    }
    if (n != 1)
    {
        num_t f[FACTOR_MAX], p[FACTOR_MAX];
        size_t l = factor(n, f, p);
        for (size_t i = 0; i < l; i++)
            mul = mul_update(mul, expr_makenum(f[i]), m*p[i]);
    }
    if (a == expr_makenum(1))
        return mul;
    mul = mul_update(mul, a, m);
    return mul;
}

/*
 * Power of two expr_ts.
 */
static func_t pow_expr_expr(expr_t a, expr_t b)
{
    return make_func(SYM_POW, a, b);
}

/****************************************************************************/
/* CONJUNCTION                                                              */
/****************************************************************************/

/*
 * And two and_t exprs.
 */
static and_t and_andexpr_andexpr(expr_t a, expr_t b)
{
    and_t anda = expr_getand(a);
    and_t andb = expr_getand(b);
    size_t sa = and_depth(anda);
    size_t sb = and_depth(andb);
    if (sb < sa)
    {
        and_t andt = anda;
        anda = andb;
        andb = andt;
    }
    expr_t k;
    bool_t v;
    for (anditr_t i = anditr(anda); and_get(i, &k, &v) && andb != NULL;
            and_next(i))
        andb = and_update(andb, k, v);
    return andb;
}

/*
 * And an and_t with a non-and_t.
 */
static and_t and_andexpr_expr(expr_t a, expr_t b)
{
    and_t anda = expr_getand(a);
    anda = and_update(anda, b, false);
    return anda;
}

/*
 * And to non-and_t exprs.
 */
static and_t and_expr_expr(expr_t a, expr_t b)
{
    and_t and = and_init();
    and = and_insert(and, a, false);
    and = and_update(and, b, false);
    return and;
}

/*
 * Split the and_t into a Boolean sign and an and_t.
 */
static and_t and_getnotfactor(expr_t a, bool_t *n)
{
    and_t and = expr_getand(a);
    if (and_depth(and) == 1 && and_size(and) == 1)
    {
        expr_t k;
        and_search_any(and, &k, NULL);
        and = and_insert(and_init(), k, false);
        *n = true;
    }
    else
        *n = false;
    return and;
}

/*
 * Update an and_t.
 */
static and_t and_update(and_t and, expr_t k, bool_t v)
{
    if (and == NULL)
        return and;
    bool_t s;
    if (and_search(and, k, &s))
    {
        if (s != v)
            return NULL;
        return and;
    }
    and = and_insert(and, k, v);
    return and;
}

/*
 * Convert an and_t to an expr_t.
 */
static expr_t and_to_expr(and_t and)
{
    if (and == NULL)
        return expr_makebool(false);
    switch (and_depth(and))
    {
        case 0:
            return expr_makebool(true);
        case 1:
            if (and_size(and) == 1)
            {
                expr_t k;
                bool_t v;
                and_search_any(and, &k, &v);
                if (!v)
                    return k;
            }
    }
    return expr_makeand(and);
}

/****************************************************************************/
/* DISJUNCTION                                                              */
/****************************************************************************/

/*
 * Or two or_t exprs.
 */
static or_t or_orexpr_orexpr(expr_t a, expr_t b)
{
    or_t ora = expr_getor(a);
    or_t orb = expr_getor(b);
    size_t sa = or_depth(ora);
    size_t sb = or_depth(orb);
    if (sb < sa)
    {
        or_t ort = ora;
        ora = orb;
        orb = ort;
    }
    expr_t k;
    bool_t v;
    for (oritr_t i = oritr(ora); or_get(i, &k, &v) && orb != NULL; or_next(i))
        orb = or_update(orb, k, v);
    return orb;
}

/*
 * Or an or_t with a non-or_t.
 */
static or_t or_orexpr_expr(expr_t a, expr_t b)
{
    or_t ora = expr_getor(a);
    ora = or_update(ora, b, false);
    return ora;
}

/*
 * Or to non-or_t exprs.
 */
static or_t or_expr_expr(expr_t a, expr_t b)
{
    or_t or = or_init();
    or = or_update(or, a, false);
    or = or_update(or, b, false);
    return or;
}

/*
 * Update an or_t.
 */
static or_t or_update(or_t or, expr_t k, bool_t v)
{
    bool_t s;
    k = expr_getnotfactor(k, &s);
    if (s)
        v = !v;
    if (or == NULL)
        return or;
    if (or_search(or, k, &s))
    {
        if (s != v)
            return NULL;
        return or;
    }
    or = or_insert(or, k, v);
    return or;
}

/*
 * Convert an or_t to an expr_t.
 */
static expr_t or_to_expr(or_t or)
{
    if (or == NULL)
        return expr_makebool(true);
    switch (or_depth(or))
    {
        case 0:
            return expr_makebool(false);
        case 1:
            if (or_size(or) == 1)
            {
                expr_t k;
                bool_t v;
                or_search_any(or, &k, &v);
                if (!v)
                    return k;
                and_t and = and_init();
                and = and_insert(and, k, v);
                return expr_makeand(and);
            }
    }
    return expr_makeor(or);
}

/****************************************************************************/
/* NEGATION                                                                 */
/****************************************************************************/

/*
 * Negate an and_t.
 */
static or_t not_andexpr(expr_t a)
{
    and_t and = expr_getand(a);
    expr_t k;
    bool_t v;
    or_t or = or_init();
    for (anditr_t i = anditr(and); and_get(i, &k, &v); and_next(i))
    {
        if (!v)
        {
            bool_t s;
            k = expr_not_propagate(k, &s);
            or = or_insert(or, k, s);
        }
        else
            or = or_insert(or, k, !v);
    }
    return or;
}

/*
 * Negate an or_t.
 */
static and_t not_orexpr(expr_t a)
{
    or_t or = expr_getor(a);
    expr_t k;
    bool_t v;
    and_t and = and_init();
    for (oritr_t i = oritr(or); or_get(i, &k, &v); or_next(i))
    {
        if (!v && expr_gettag(k) == EXPRTAG_AND)
        {
            bool_t s;
            k = expr_not_propagate(k, &s);
            and = and_insert(and, k, s);
        }
        else
            and = and_insert(and, k, !v);
    }
    return and;
}

/*
 * Negate an (non-and_t nor non-or_t) expr_t.
 */
static and_t not_expr(expr_t a)
{
    and_t and = and_init();
    and = and_insert(and, a, true);
    return and;
}

/***************************************************************************/
/* COMPARISON                                                              */
/***************************************************************************/

/*
 * Compare two expressions.
 */
static func_t cmp_expr_expr(atom_t cmp, expr_t a, expr_t b, bool_t *s)
{
    // (0) Create difference:
    expr_t d = expr_sub(b, a);
   
    // (1) Remove non-strictness:
    bool sign = false;
    if (cmp == SYM_LEQ)
    {
        cmp = SYM_LT;
        d = expr_neg(d);
        sign = true;
    }

    // (2) Remove any constant factors.
    num_t n;
    d = expr_getnumfactor(d, &n);
    if (n == 0)
        d = expr_makenum(0);
    else if (n < 0)
        d = expr_neg(d);

    // (3) special-case handling:
    if (cmp == SYM_LT && expr_gettag(d) == EXPRTAG_ADD)
    {
        add_t add = expr_getadd(d);
        if (add_depth(add) == 1)
        {
            switch (add_size(add))
            {
                case 2:
                {
                    expr_t k1; num_t v1;
                    add_search_min(add, &k1, &v1);
                    if (v1 == -1 && expr_gettag(k1) == EXPRTAG_VAR)
                    {
                        expr_t k2; num_t v2;
                        add_search_max(add, &k2, &v2);
                        if (expr_gettag(k2) == EXPRTAG_NUM)
                        {
                            d = expr_neg(d);
                            d = expr_add(d, expr_makenum(1));
                            sign = !sign;
                        }
                    }
                    break;
                }
                case 1:
                {
                    expr_t k1; num_t v1;
                    add_search_min(add, &k1, &v1);
                    if (v1 == -1 && expr_gettag(k1) == EXPRTAG_VAR)
                    {
                        d = expr_neg(d);
                        d = expr_add(d, expr_makenum(1));
                        sign = !sign;
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }
    else if (cmp == SYM_EQ)
    {
        if (expr_getsign(d))
            d = expr_neg(d);
    }

    *s = sign;
    return make_func(cmp, expr_makenum(0), d);
}

/*
 * Convert a comparison into an expr_t.
 */
static expr_t cmp_to_expr(func_t f)
{
    if (expr_gettag(f->args[1]) == EXPRTAG_NUM)
    {
        num_t n = expr_getnum(f->args[1]);
        if (f->atom == SYM_EQ)
            return expr_makebool(0 == n);
        else
            return expr_makebool(0 < n);
    }
    return expr_makeop(f);
}

/****************************************************************************/
/* FACTORIZATION                                                            */
/****************************************************************************/

/*
 * List of small primes.
 */
static uint_t primes[] =
{
    2,   3,   5,   7,   11,  13,  17,  19,  23,  29,  31,  37,  41,  43,
    47,  53,  59,  61,  67,  71,  73,  79,  83,  89,  97,  101, 103, 107,
    109, 113, 127, 131, 137, 139, 149, 151, 157, 163, 167, 173, 179, 181,
    191, 193, 197, 199, 211, 223, 227, 229, 233, 239, 241, 251
};

/*
 * Factor a num_t.
 */
static size_t factor(num_t n, num_t *f, num_t *p)
{
    int_t ni = (int_t)n;
    if (ni < 0)
    {
        *f++ = -1;
        *p++ = 1;
        ni = -ni;
        return 1 + factor_uint((uint_t)ni, f, p);
    }
    else if (ni == 0)
    {
        *f = 0;
        *p = 1;
        return 1;
    }
    else
        return factor_uint((uint_t)ni, f, p);
}

/*
 * Factor a uint_t.
 */
static size_t factor_uint(uint_t n, num_t *f, num_t *p)
{
    // Trial division.
    size_t l = 0;
    for (size_t i = 0; i < sizeof(primes)/sizeof(uint_t); i++)
    {
        if (n % primes[i] == 0)
        {
            f[l] = (num_t)primes[i];
            p[l] = 1;
            n /= primes[i];
            while (n % primes[i] == 0)
            {
                n /= primes[i];
                p[l] += 1;
            }
            l++;
            if (n == 1)
                return l;
        }
    }

    // TODO pollard-rho.
    f[l] = (num_t)n;
    p[l] = 1;
    return l+1;
}

/****************************************************************************/
/* VIEWS                                                                    */
/****************************************************************************/

/*
 * x CMP y
 */
extern bool expr_view_x_cmp_y(expr_t e, expr_t *x, exprop_t *cmp, expr_t *y)
{
    if (expr_gettag(e) != EXPRTAG_OP)
        return false;
    exprop_t o = expr_op(e);
    if (o != EXPROP_EQ && o != EXPROP_LT)
        return false;
    *cmp = (o == EXPROP_LT? EXPROP_GT: EXPROP_EQ);
    e = expr_arg(e, 1);
    switch (expr_gettag(e))
    {
        case EXPRTAG_VAR:
            *x = e;
            *y = expr_num(0);
            return true;
        case EXPRTAG_ADD:
            break;
        default:
            return false;
    }
    add_t add = expr_getadd(e);
    additr_t i = additr(add);
    expr_t k1; num_t v1;
    if (!add_get(i, &k1, &v1))
        return false;
    if (expr_gettype(k1) != EXPRTYPE_VAR || (v1 != 1 && v1 != -1))
        return false;
    add_next(i);
    expr_t k2; num_t v2;
    if (!add_get(i, &k2, &v2))
        return false;
    add_next(i);
    if (add_get(i, NULL, NULL))
        return false;
    switch (expr_gettag(k2))
    {
        case EXPRTAG_NUM:
            if (v1 == 1)
            {
                *x = k1;
                *y = expr_num(-v2);
                return true;
            }
            if (v1 == -1 && o == EXPROP_EQ)
            {
                *x = k1;
                *y = expr_num(v2);
                return true;
            }
            return false;
        case EXPRTAG_VAR: 
            break;
        default:
            return false;
    }
    if (v1 == 1 && v2 == -1)
    {
        *x = k1;
        *y = k2;
        return true;
    }
    if (v1 == -1 && v2 == 1)
    {
        *x = k2;
        *y = k1;
        return true;
    }
    return false;
}

/*
 * x CMP y OP z
 */
extern bool expr_view_x_cmp_y_op_z(expr_t e, expr_t *x, exprop_t *cmp,
    expr_t *y, exprop_t *op, expr_t *z)
{
    if (expr_gettag(e) != EXPRTAG_OP)
        return false;
    exprop_t o = expr_op(e);
    if (o != EXPROP_EQ)
        return false;
    e = expr_arg(e, 1);
    if (expr_gettag(e) != EXPRTAG_ADD)
        return false;
    add_t add = expr_getadd(e);
    additr_t i = additr(add);
    expr_t k1; num_t v1;
    if (!add_get(i, &k1, &v1))
        return false;
    if (expr_gettype(k1) != EXPRTYPE_VAR)
        return false;
    add_next(i);
    expr_t k2; num_t v2;
    if (!add_get(i, &k2, &v2))
        return false;
    add_next(i);
    *cmp = EXPROP_EQ;
    expr_t k3; num_t v3;
    if (add_get(i, &k3, &v3))
    {
        if (v1 != 1 && v1 != -1)
            return false;
        if (expr_gettype(k2) != EXPRTYPE_VAR || (v2 != 1 && v2 != -1))
            return false;
        add_next(i);
        if (add_get(i, NULL, NULL))
            return false;
        switch (expr_gettag(k3))
        {
            case EXPRTAG_VAR:
                if (v3 != 1 && v3 != -1)
                    return false;
                if (v2 == -v1)
                {
                    if (v3 == -v1)
                    {
                        *x = k1;
                        *y = k2;
                        *op = EXPROP_ADD;
                        *z = k3;
                    }
                    else
                    {
                        *x = k2;
                        *y = k1;
                        *op = EXPROP_ADD;
                        *z = k3;
                    }
                }
                else
                {
                    if (v3 == -v1)
                    {
                        *x = k3;
                        *y = k1;
                        *op = EXPROP_ADD;
                        *z = k2;
                    }
                    else
                        return false;
                }
                return true;
            case EXPRTAG_NUM:
                *x = k1;
                if (v2 == -v1)
                {
                    *y = k2;
                    *op = EXPROP_ADD;
                    *z = expr_num((v1 == 1? -v3: v3));
                }
                else
                {
                    /*
                    *y = expr_num(-v3);
                    *op = EXPROP_SUB;
                    *z = k2;
                    */
                    return false;
                }
                return true;
            default:
                return false;
        }
    }
    if (v1 != 1 && v1 != -1)
    {
        if (expr_gettag(k2) != EXPRTAG_VAR)
            return false;
        *x = k2;
        *op = EXPROP_MUL;
        *z = k1;
        if (v2 == 1)
            *y = expr_num(-v1);
        else if (v2 == -1)
            *y = expr_num(v1);
        else
            return false;
        return true;
    }
    *x = k1;
    switch (expr_gettag(k2))
    {
        case EXPRTAG_VAR:
            *y = expr_num(-v2*v1);
            *op = EXPROP_MUL;
            *z = k2;
            return true;
        case EXPRTAG_OP:
        {
            if (v2 != 1)
                return false;
            func_t f = expr_getop(k2);
            if (atom_arity(f->atom) != 2)
                return false;
            expr_t a1 = f->args[0], a2 = f->args[1];
            if (expr_gettype(a1) != EXPRTYPE_VAR &&
                expr_gettype(a1) != EXPRTYPE_NUM)
                return false;
            if (expr_gettype(a2) != EXPRTYPE_VAR &&
                expr_gettype(a2) != EXPRTYPE_NUM)
                return false;
            *y = a1;
            *op = expr_op(k2);
            *z = a2;
            return true;
        }
        case EXPRTAG_MUL:
        {
            if (v2 != -v1)
                return false;
            mul_t mul = expr_getmul(k2);
            mulitr_t j = mulitr(mul);
            expr_t k4; num_t v4;
            if (!mul_get(j, &k4, &v4))
                return false;
            if (expr_gettype(k4) != EXPRTYPE_VAR &&
                expr_gettype(k4) != EXPRTYPE_NUM)
                return false;
            mul_next(j);
            expr_t k5; num_t v5;
            if (mul_get(j, &k5, &v5))
            {
                mul_next(j);
                if (mul_get(j, NULL, NULL))
                    return false;
                if (v4 != 1 || v5 != 1)
                    return false;
                if (expr_gettype(k5) != EXPRTYPE_VAR)
                    return false;
                *y = k4;
                *op = EXPROP_MUL;
                *z = k5;
                return true;
            }
            if (expr_gettype(k4) != EXPRTYPE_VAR || v4 <= 1)
                return false;
            *y = k4;
            *op = EXPROP_POW;
            *z = expr_num(v4);
            return true;
        }
        default:
            return false;
    }
}

/*
 * x = f(...)
 */
extern bool expr_view_x_eq_func(expr_t e, expr_t *x, expr_t *f)
{
    if (expr_gettag(e) != EXPRTAG_OP)
        return false;
    exprop_t op = expr_op(e);
    if (op != EXPROP_EQ)
        return false;
    e = expr_arg(e, 1);
    if (expr_gettag(e) != EXPRTAG_ADD)
        return false;
    add_t add = expr_getadd(e);
    additr_t i = additr(add);
    expr_t k1; num_t v1;
    if (!add_get(i, &k1, &v1))
        return false;
    if (expr_gettype(k1) != EXPRTYPE_VAR || v1 != 1)
        return false;
    add_next(i);
    expr_t k2; num_t v2;
    if (!add_get(i, &k2, &v2))
        return false;
    if (expr_gettype(k2) != EXPRTYPE_OP || v2 != -1)
        return false;
    add_next(i);
    if (add_get(i, NULL, NULL))
        return false;
    op = expr_op(k2);
    if (op < EXPROP_MAX)        // Is built-in op?
        return false;
    *x = k1;
    *f = k2;
    return true;
}

/*
 * x + y sign partition.
 */
extern bool expr_view_plus_sign_partition(expr_t e, expr_t *x, expr_t *y)
{
    if (expr_gettag(e) != EXPRTAG_ADD)
        return false;
    add_t add = expr_getadd(e);
    add_t lhs = add_init();
    add_t rhs = add_init();
    additr_t i = additr(add);
    expr_t k; num_t v;
    while (add_get(i, &k, &v))
    {
        if (v < 0)
            lhs = add_insert(lhs, k, -v);
        else
            rhs = add_insert(rhs, k, v);
        add_next(i);
    }
    *x = add_to_expr(lhs);
    *y = add_to_expr(rhs);
    return true;
}

/*
 * x + y first-term partition.
 */
extern bool expr_view_plus_first_partition(expr_t e, expr_t *x, expr_t *y)
{
    if (expr_gettag(e) != EXPRTAG_ADD)
        return false;
    add_t add = expr_getadd(e);
    expr_t k; num_t v;
    for (additr_t i = additr(add); add_get(i, &k, &v); add_next(i))
    {
        if (v == 1)
        {
            add = add_delete(add, k, NULL);
            *x = k;
            *y = add_to_expr(add);
            return true;
        }
    }
    return false;
}

