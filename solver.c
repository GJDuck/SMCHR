/*
 * solver.c
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

#include <setjmp.h>

#include "map.h"
#include "misc.h"
#include "solver.h"

/*
 * Built-in symbols.
 */
sym_t EQ;
sym_t EQ_C;
sym_t EQ_PLUS;
sym_t EQ_PLUS_C;
sym_t EQ_MUL;
sym_t EQ_MUL_C;
sym_t EQ_POW_C;
sym_t GT;
sym_t GT_C;
sym_t LB;
sym_t EQ_NIL;
sym_t EQ_C_NIL;
sym_t EQ_ATOM;
sym_t EQ_C_ATOM;
sym_t EQ_STR;
sym_t EQ_C_STR;

/*
 * Built-in constraints.
 */
struct cons_s true_cons;
struct cons_s false_cons;

/*
 * All symbols.
 */
static int_t compare_sym(sym_t syma, sym_t symb)
{
    int_t cmp = (int_t)strcmp(syma->name, symb->name);
    if (cmp != 0)
        return cmp;
    cmp = (int_t)syma->arity - (int_t)symb->arity;
    return cmp;
}
MAP_DECL(syms, sym_t, sym_t, compare_sym);
syms_t syms;

/*
 * Test if two lookups are equal.
 */
static inline bool lookup_iseq(lookup_t lx, lookup_t ly)
{
    while (true)
    {
        if (*lx != *ly)
            return false;
        if (*lx == -1)
            return true;
        lx++;
        ly++;
    }
}

/*
 * Make a lookup.
 */
extern lookup_t solver_make_lookup(term_t *args, size_t len)
{
    size_t max_lookup = INT8_MAX;
    if (len > max_lookup)
        panic("lookup is too long; maximum is %zu, got %zu", max_lookup, len);
    lookup_t lookup = (lookup_t)gc_malloc(len*sizeof(int8_t)+1);
    size_t j = 0;
    for (size_t i = 0; i < len; i++)
    {
        term_t arg = args[i];
        if (arg != (term_t)NULL)
            lookup[j++] = (int8_t)i;
    }
    lookup[j] = -1;
    return lookup;
}

/*
 * Make a new symbol.
 */
extern sym_t solver_make_sym(const char *name, size_t arity, bool deflt)
{
    sym_t sym = solver_lookup_sym(name, arity);
    if (sym != NULL)
        return sym;
    sym = (sym_t)gc_malloc(sizeof(struct sym_s));
    if (!gc_isptr(name))
        name = gc_strdup(name);
    sym->name   = name;
    sym->arity  = arity;
    sym->constr = NULL;
    sym->occs   = NULL;
    sym->type   = DEFAULT;
    sym->flags  = 0;
    sym->sig    = TYPESIG_DEFAULT;
    sym->hash   = hash_new();
    sym->propinfo_len = 0;
    sym->lookups_len = 0;
    syms = syms_destructive_insert(syms, sym, sym);
    if (deflt)
        default_solver(sym);
    return sym;
}

/*
 * Lookup a symbol.
 */
extern sym_t solver_lookup_sym(const char *name, size_t arity)
{
    struct sym_s key_0;
    sym_t key = &key_0;
    key->name = name;
    key->arity = arity;
    sym_t sym;
    if (syms_search(syms, key, &sym))
        return sym;
    else
        return NULL;
}

/*
 * Register a solver with a symbol.
 */
extern void solver_register_solver(sym_t sym, uint_t priority, event_t e,
    handler_t handler, ...)
{
    size_t idx;
    for (idx = 0; idx < sym->propinfo_len; idx++)
    {
        if (sym->propinfo[idx].handler == handler)
            break;
    }

    if (idx == sym->propinfo_len)
    {
        if (idx >= MAX_PROPINFO)
            fatal("too many solvers for symbol %s/%zu", sym->name, sym->arity);
        sym->propinfo[idx].priority = priority;
        sym->propinfo[idx].events   = e;
        sym->propinfo[idx].handler  = handler;
        sym->propinfo_len++;
    }
    else
    {
        if (sym->propinfo[idx].events != e)
            fatal("conflicting event declarations for symbol `%s/%zu",
                sym->name, sym->arity);
    }

    va_list ap;
    va_start(ap, handler);
    while (true)
    {
        lookup_t lookup = va_arg(ap, lookup_t);
        if (lookup == NULL)
            break;
        solver_register_lookup(sym, lookup);
    }
    va_end(ap);
}

/*
 * Register a lookup with a symbol.
 */
extern void solver_register_lookup(sym_t sym, lookup_t lookup)
{
    // Do not accept "all-T" lookup (already handled by default). 
    for (size_t i = 0; true; i++)
    {
        if (lookup[i] == -1)
        {
            if (i >= sym->arity)
                return;
            break;
        }
        if (lookup[i] != i)
            break;
    }

    // Check for existing lookup:
    for (size_t i = 0; i < sym->lookups_len; i++)
    {
        if (lookup_iseq(lookup, sym->lookups[i]))
            return;
    }

    // Otherwise add lookup:
    size_t len = 0;
    while (lookup[len] != (-1))
        len++;
    if (len == sym->arity)
        return;
    size_t idx = sym->lookups_len;
    if (idx >= MAX_LOOKUPS)
        fatal("too many lookups for symbol %s/%zu", sym->name, sym->arity);
    sym->lookups[idx] = lookup;
    sym->lookups_len++;
}

/*
 * Register a typesig with a symbol.
 */
extern void solver_register_typesig(sym_t sym, typesig_t sig)
{
    if ((sym->flags & FLAG_SOLVER_TYPESIG) != 0)
    {
        if (!typesig_eq(sym->arity, sig, sym->sig))
        {
solver_register_typesig_error:
            fatal("conflicting typeinst declarations for symbol `%s/%zu'",
                sym->name, sym->arity);
        }
        return;
    }
    
    atom_t atom = make_atom(sym->name, sym->arity);
    if (!typeinst_declare(atom, sig))
        goto solver_register_typesig_error;
    sym->sig = sig;
    sym->flags |= FLAG_SOLVER_TYPESIG;
}

/*
 * Make a constraint.
 */
extern cons_t solver_make_cons(reason_t reason, sym_t sym, term_t *args_0)
{
    // (0) Handle commutative constraints here:
    if ((sym->flags & FLAG_COMMUTATIVE) != 0)
    {
        if (term_compare(args_0[0], args_0[1]) > 0)
        {
            term_t tmp = args_0[0];
            args_0[0] = args_0[1];
            args_0[1] = tmp;
        }
    }

    // (0) Apply a constructor if need be.
    if (sym->constr != NULL)
    {
        switch (sym->constr(&sym, args_0))
        {
            case UNKNOWN:
                break;
            case TRUE:
                return &true_cons;
            case FALSE:
                return &false_cons;
        }
    }

    // (1) Normalize the arguments and compute the hash value:
    size_t aty = sym->arity;
    term_t args[aty];
    hash_t key = hash_sym(sym);
    typesig_t sig = sym->sig;

    for (size_t i = 0; i < aty; i++)
    {
        term_t arg = args_0[i];

        // Run-time type-inst checking:
        type_t tt = type(arg);
        typeinst_t ti = typeinst_decl_arg(sig, i);
        bool type_ok = false;
        if (tt == VAR)
            type_ok = (ti == typeinst_make_var(ti));
        else if (ti == TYPEINST_ANY)
            type_ok = true;
        else
        {
            switch (tt)
            {
                case NIL:
                    type_ok = (ti == TYPEINST_NIL);
                    break;
                case BOOL:
                    type_ok = (ti == TYPEINST_BOOL);
                    break;
                case ATOM:
                    type_ok = (ti == TYPEINST_ATOM);
                    break;
                case NUM:
                    type_ok = (ti == TYPEINST_NUM);
                    break;
                case STR:
                    type_ok = (ti == TYPEINST_STRING);
                    break;
                default:
                    break;
            }
        }
        if (!type_ok)
            fatal("type-inst error for `%s/%zu' constraint; expected a term "
                "of type `%s', found `%s'", sym->name, sym->arity,
                typeinst_show(ti), show(arg));

        if (tt == VAR)
        {
            var_t x = var(arg);
            x = deref(x);
            arg = term_var(x);
        }
        key = hash_join(i, key, hash_term(arg));
        args[i] = arg;
    }

    // (2) Find existing constraint:
    conslist_t cs = solver_store_search(key);
    if (cs != NULL)
    {
        // Found an existing constraint; compute the reason:
        cons_t c = cs->cons;
        for (size_t i = 0; i < aty; i++)
        {
            term_t arg = args_0[i];
            if (type(arg) == VAR)
            {
                check(type(c->args[i]) == VAR);
                var_t x = var(arg), y = var(c->args[i]);
                match_vars(reason, x, y);
            }
        }
        debug("FOUND EXISTING %s", show_cons(c));
        return c;
    }
    else if ((sym->flags & FLAG_COMMUTATIVE) != 0)
    {
        hash_t key_1 = hash_sym(sym);
        key_1 = hash_join(0, key_1, hash_term(args[1]));
        key_1 = hash_join(1, key_1, hash_term(args[0]));
        cs = solver_store_search(key_1);
        if (cs != NULL)
        {
            cons_t c = cs->cons;
            var_t x = var(args_0[1]), y = var(c->args[0]);
            match_vars(reason, x, y);
            x = var(args_0[0]); y = var(c->args[1]);
            match_vars(reason, x, y);
            debug("FOUND EXISTING %s", show_cons(c));
            return c;
        }
    }

    // (3) Otherwise create a new constraint:
    size_t numprops = sym->propinfo_len;
    cons_t c = (cons_t)gc_malloc(sizeof(struct cons_s) +
        aty*sizeof(term_t) + numprops*sizeof(struct prop_s));
    c->sym = sym;
    c->b     = sat_make_var(NULL, c);
    c->flags = 0;
    for (size_t i = 0; i < aty; i++)
    {
        term_t arg = args[i];
        c->args[i] = arg;
        if (type(arg) == VAR)
        {
            check(type(args_0[i]) == VAR);
            var_t x = var(arg), y = var(args_0[i]);
            match_vars(reason, x, y);
            solver_attach_var(x, c);
        }
    }
    prop_t props = (prop_t)(c->args + aty);
    for (size_t i = 0; i < sym->propinfo_len; i++)
        solver_init_prop(props + i, i, sym->propinfo[i].events, c);
    solver_store_insert_primary(key, c);
    debug("!rCONSTRAINT!d %s", show_cons(c));
    stat_constraints++;
    return c;
}

/*
 * x = y constructor.
 */
static decision_t solver_make_eq(sym_t *sym, term_t *args)
{
    term_t x = args[X], y = args[Y];
    if (x == y)
        return TRUE;
    if (term_compare(x, y) > 0)
    {
        term_t t = x;
        x = y;
        y = t;
    }
    return UNKNOWN;
}

/*
 * x > y constructor.
 */
static decision_t solver_make_gt(sym_t *sym, term_t *args)
{
    if (args[X] == args[Y])
        return FALSE;
    return UNKNOWN;
}

/*
 * x = y + c constructor.
 */
static decision_t solver_make_eq_plus_c(sym_t *sym, term_t *args)
{
    term_t x = args[X], y = args[Y];
    num_t c = num(args[Z]);
    if (c == 0)
    {
        *sym = EQ;
        return solver_make_eq(sym, args);
    }
    if (x == y)
        return FALSE;
    if (c < 0)
    {
        term_t tmp = args[X];
        args[X] = args[Y];
        args[Y] = tmp;
        args[Z] = term_num(-c);
    }
    return UNKNOWN;
}

/*
 * x = c * y constructor.
 */
static decision_t solver_make_eq_mul_c(sym_t *sym, term_t *args)
{
    term_t x = args[X], y = args[Y];
    num_t c = num(args[Z]);
    if (c == 0)
    {
        *sym = EQ_C;
        args[Y] = args[Z];
        return UNKNOWN;
    }
    if (c == 1)
    {
        *sym = EQ;
        return solver_make_eq(sym, args);
    }
    if (x == y)
    {
        *sym = EQ_C;
        args[Y] = term_int(0);
        return UNKNOWN;
    }
    return UNKNOWN;
}

/*
 * x = y + z constructor.
 */
static decision_t solver_make_eq_plus(sym_t *sym, term_t *args)
{
    term_t x = args[X], y = args[Y], z = args[Z];
    if (x == y)
    {
        *sym = EQ_C;
        args[Y] = term_int(0);
        if (y == z)
            return UNKNOWN;
        args[X] = z;
        return UNKNOWN;
    }
    if (x == z)
    {
        *sym = EQ_C;
        args[Y] = term_int(0);
        args[X] = y;
        return UNKNOWN;
    }
    if (y == z)
    {
        *sym = EQ_MUL_C;
        args[Z] = term_int(2);
        return UNKNOWN;
    }
    return UNKNOWN;
}

/*
 * Invoke the solver.
 */
static bool solver_on = false;
static jmp_buf solver_env;
extern result_t solver_solve(literal_t *choices)
{
    literal_t nil = LITERAL_NIL;
    if (choices == NULL)
        choices = &nil;

    if (setjmp(solver_env) != 0)
    {
        solver_on = false;
        return RESULT_ERROR;
    }

    solver_on = true;
    result_t result;
    if (sat_solve(choices))
        result = RESULT_UNKNOWN;
    else
        result = RESULT_UNSAT;
    solver_on = false;

    return result;
}

/*
 * Abort the solver.
 */
extern void solver_abort(void)
{
    if (!solver_on)
        return;
    longjmp(solver_env, 1);
}

/*
 * Solver comparison.
 */
extern int solver_compare(const void *a, const void *b)
{
    solver_t sa = *(solver_t *)a;
    solver_t sb = *(solver_t *)b;
    return strcmp(sa->name, sb->name);
}

/*
 * Re-size a reason_t.
 */
extern void solver_grow_reason(reason_t reason)
{
    reason->size = 2*reason->size + REASON_INIT_SIZE;
    literal_t *lits = (literal_t *)gc_malloc(reason->size*sizeof(literal_t));
    memcpy(lits, reason->lits, reason->len*sizeof(literal_t));
    if (gc_isptr(reason->lits))
        gc_free(reason->lits);
    reason->lits = lits;
}

/*
 * Solver init/reset.
 */
extern void solver_init(void)
{
    syms = syms_init();
    if (!gc_root(&syms, sizeof(syms)))
        panic("failed to set GC root for symbols map");

    sym_t BOOL_TRUE = make_sym("true", 0, false);
    sym_t BOOL_FALSE = make_sym("false", 0, false);

    EQ        = make_sym("int_eq", 2, false);
    EQ_C      = make_sym("int_eq_c", 2, true);
    EQ_PLUS   = make_sym("int_eq_plus", 3, true);
    EQ_PLUS_C = make_sym("int_eq_plus_c", 3, true);
    EQ_MUL    = make_sym("int_eq_mul", 3, true);
    EQ_MUL_C  = make_sym("int_eq_mul_c", 3, true);
    EQ_POW_C  = make_sym("int_eq_pow_c", 3, true);
    GT        = make_sym("int_gt", 2, true);
    GT_C      = make_sym("int_gt_c", 2, true);
    LB        = make_sym("int_lb", 2, false);
    EQ_NIL    = make_sym("nil_eq", 2, false);
    EQ_C_NIL  = make_sym("nil_eq_c", 2, true);
    EQ_ATOM   = make_sym("atom_eq", 2, false);
    EQ_C_ATOM = make_sym("atom_eq_c", 2, true);
    EQ_STR    = make_sym("str_eq", 2, false);
    EQ_C_STR  = make_sym("str_eq_c", 2, true);

    EQ->type = X_CMP_Y;
    GT->type = X_CMP_Y;
    EQ_C->type = X_CMP_C;
    GT_C->type = X_CMP_C;
    EQ_PLUS->type = X_EQ_Y_OP_Z;
    EQ_MUL->type = X_EQ_Y_OP_Z;
    EQ_PLUS_C->type = X_EQ_Y_OP_C;
    EQ_MUL_C->type = X_EQ_Y_OP_C;

    EQ->flags |= FLAG_COMMUTATIVE;
    EQ_NIL->flags |= FLAG_COMMUTATIVE;
    EQ_ATOM->flags |= FLAG_COMMUTATIVE;
    EQ_STR->flags |= FLAG_COMMUTATIVE;

    EQ->constr = solver_make_eq;
    GT->constr = solver_make_gt;
    EQ_PLUS_C->constr = solver_make_eq_plus_c;
    EQ_MUL_C->constr = solver_make_eq_mul_c;
    EQ_PLUS->constr = solver_make_eq_plus;

    typesig_t sig_b_vn_vn = make_typesig(TYPEINST_BOOL, TYPEINST_VAR_NUM,
        TYPEINST_VAR_NUM);
    typesig_t sig_b_vn_n = make_typesig(TYPEINST_BOOL, TYPEINST_VAR_NUM,
        TYPEINST_NUM);
    typesig_t sig_b_vn_vn_vn = make_typesig(TYPEINST_BOOL, TYPEINST_VAR_NUM,
        TYPEINST_VAR_NUM, TYPEINST_VAR_NUM);
    typesig_t sig_b_vn_vn_n = make_typesig(TYPEINST_BOOL, TYPEINST_VAR_NUM,
        TYPEINST_VAR_NUM, TYPEINST_NUM);
    typesig_t sig_b_v0_v0 = make_typesig(TYPEINST_BOOL, TYPEINST_VAR_NIL,
        TYPEINST_VAR_NIL);
    typesig_t sig_b_v0_0 = make_typesig(TYPEINST_BOOL, TYPEINST_VAR_NIL,
        TYPEINST_NIL);
    typesig_t sig_b_va_va = make_typesig(TYPEINST_BOOL, TYPEINST_VAR_ATOM,
        TYPEINST_VAR_ATOM);
    typesig_t sig_b_va_a = make_typesig(TYPEINST_BOOL, TYPEINST_VAR_ATOM,
        TYPEINST_ATOM);
    typesig_t sig_b_vs_vs = make_typesig(TYPEINST_BOOL, TYPEINST_VAR_STRING,
        TYPEINST_VAR_STRING);
    typesig_t sig_b_vs_s = make_typesig(TYPEINST_BOOL, TYPEINST_VAR_STRING,
        TYPEINST_STRING);

    register_typesig(EQ, sig_b_vn_vn);
    register_typesig(GT, sig_b_vn_vn);
    register_typesig(EQ_C, sig_b_vn_n);
    register_typesig(GT_C, sig_b_vn_n);
    register_typesig(EQ_PLUS, sig_b_vn_vn_vn);
    register_typesig(EQ_MUL, sig_b_vn_vn_vn);
    register_typesig(EQ_PLUS_C, sig_b_vn_vn_n);
    register_typesig(EQ_MUL_C, sig_b_vn_vn_n);
    register_typesig(EQ_POW_C, sig_b_vn_vn_n);
    register_typesig(LB, sig_b_vn_n);
    register_typesig(EQ_NIL, sig_b_v0_v0);
    register_typesig(EQ_C_NIL, sig_b_v0_0);
    register_typesig(EQ_ATOM, sig_b_va_va);
    register_typesig(EQ_C_ATOM, sig_b_va_a);
    register_typesig(EQ_STR, sig_b_vs_vs);
    register_typesig(EQ_C_STR, sig_b_vs_s);

    solver_init_var();
    solver_init_trail();
    solver_init_store();

    true_cons.sym    = BOOL_TRUE;
    true_cons.b      = LITERAL_TRUE;
    true_cons.flags  = 0;
    false_cons.sym   = BOOL_FALSE;
    false_cons.b     = LITERAL_FALSE;
    false_cons.flags = 0;
}
extern void solver_reset(void)
{
    solver_reset_var();
    solver_reset_trail();
    solver_reset_store();
    solver_reset_prop_queue();
    hash_reset();
    names_reset();
}

