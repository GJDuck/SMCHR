/*
 * typecheck.c
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

#include "log.h"
#include "map.h"
#include "show.h"
#include "typecheck.h"

/*
 * Instance encoding.
 */
#define TYPEINST_GROUND_ARITY   0
#define TYPEINST_VAR_ARITY      1

/*
 * Function type information.
 */
MAP_DECL(tsiginfo, atom_t, typesig_t, compare_atom);

/*
 * Variable type information.
 */
struct vtype_s
{
    typeinst_t type;
};
typedef struct vtype_s *vtype_t;
MAP_DECL(typeinfo, var_t, vtype_t, compare_var);

/*
 * Typechecking context.
 */
struct context_s
{
    const char *file;
    size_t line;
    tsiginfo_t tinfo;
    typeinfo_t vinfo;
    bool error;
};
typedef struct context_s *context_t;

/*
 * Globals.
 */
static tsiginfo_t tinfo;

/*
 * Prototypes.
 */
static term_t typecheck_disambiguate(context_t cxt, term_t t);
static bool typecheck_term(context_t cxt, term_t t, typeinst_t type);
static bool typecheck_var(context_t cxt, var_t x, typeinst_t type);
static void typecheck_set_var(context_t cxt, var_t x, typeinst_t type);
static void typecheck_unify(context_t cxt, var_t x, var_t y);
static bool typecheck_expect(context_t cxt, term_t t, typeinst_t expected,
    typeinst_t actual);
static bool typecheck_unexpected(context_t cxt, term_t t);

/*
 * Typecheck pass.
 */
extern bool typecheck(const char *file, size_t line, term_t t,
    typeinfo_t *info)
{
    struct context_s cxt_0;
    context_t cxt = &cxt_0;
    cxt->file = file;
    cxt->line = line;
    cxt->tinfo = tinfo;
    cxt->vinfo = typeinfo_init();
    cxt->error = false;

    if (!typecheck_term(cxt, t, TYPEINST_BOOL) || cxt->error)
        return false;

    *info = cxt->vinfo;
    return true;
}

/*
 * Get the type of a term.
 */
extern typeinst_t typecheck_typeof(typeinfo_t info, term_t t)
{
    switch (type(t))
    {
        case VAR:
        {
            vtype_t vtype;
            if (typeinfo_search(info, var(t), &vtype))
                return typeinst_make_ground(vtype->type);
            return TYPEINST_ANY;
        }
        case NIL:
            return TYPEINST_NIL;
        case BOOL:
            return TYPEINST_BOOL;
        case NUM:
            return TYPEINST_NUM;
        case STR:
            return TYPEINST_STRING;
        case ATOM:
            return TYPEINST_ATOM;
        case FOREIGN:
            return TYPEINST_ANY;
        case FUNC:
        {
            func_t f = func(t);
            typesig_t sig;
            if (tsiginfo_search(tinfo, f->atom, &sig))
                return typeinst_make_ground(sig->type);
            return TYPEINST_ANY;
        }
        default:
            return TYPEINST_ANY;
    }
}

/*
 * Typecheck a term.
 */
static bool typecheck_term(context_t cxt, term_t t, typeinst_t type)
{
    switch (type(t))
    {
        case VAR:
            return typecheck_var(cxt, var(t), type);
        case NIL:
            return typecheck_expect(cxt, t, type, TYPEINST_NIL);
        case BOOL:
            return typecheck_expect(cxt, t, type, TYPEINST_BOOL);
        case NUM:
            return typecheck_expect(cxt, t, type, TYPEINST_NUM);
        case STR:
            return typecheck_expect(cxt, t, type, TYPEINST_STRING);
        case ATOM:
            return typecheck_expect(cxt, t, type, TYPEINST_ATOM);
        case FOREIGN:
            return typecheck_unexpected(cxt, t);
        case FUNC:
        {
            func_t f = func(t);
            typesig_t sig;
            bool ok = true;
            if (f->atom == ATOM_EQ || f->atom == ATOM_NEQ)
            {
                // Special case for equality.
                term_t x = f->args[0], y = f->args[1];
                if (!typecheck_term(cxt, x, TYPEINST_ANY) ||
                        !typecheck_term(cxt, y, TYPEINST_ANY))
                    return false;
                typeinst_t tx = typecheck_typeof(cxt->vinfo, x);
                typeinst_t ty = typecheck_typeof(cxt->vinfo, y);
                if (tx != TYPEINST_ANY)
                {
                    if (ty == TYPEINST_ANY)
                    {
                        if (type(y) == VAR)
                            typecheck_set_var(cxt, var(y), tx);
                        return true;
                    }
                    if (tx == ty)
                        return true;
                }
                else
                {
                    if (ty == TYPEINST_ANY)
                    {
                        if (type(x) == VAR && type(y) == VAR)
                        {
                            typecheck_unify(cxt, var(x), var(y));
                            return true;
                        }
                        else
                        {
                            // Ambigious equality, handle later.
                            /*
                                error("(%s: %zu) type error with "
                                    "expression `!y%s!d': equality is "
                                    "ambiguous, both arguments have "
                                    "type (!g%s!d)", cxt->file, cxt->line,
                                    show(t),
                                    typeinst_show(TYPEINST_ANY));
                            */
                            return true;
                        }
                    }
                    else if (type(x) == VAR)
                    {
                        typecheck_set_var(cxt, var(x), ty);
                        return true;
                    }
                }
                error("(%s: %zu) type error with expression `!y%s!d'; "
                    "equality arguments have different types (!g%s!d) "
                    "vs (!g%s!d)", cxt->file, cxt->line, show(t),
                    typeinst_show(tx), typeinst_show(ty));
                return false;
            }
            if (tsiginfo_search(cxt->tinfo, f->atom, &sig))
            {
                if (!typecheck_expect(cxt, t, type, typeinst_decl_type(sig)))
                    ok = false;
                for (size_t i = 0; ok && i < atom_arity(f->atom); i++)
                {
                    if (!typecheck_term(cxt, f->args[i],
                            typeinst_decl_arg(sig, i)))
                        ok = false;
                }
            }
            else
            {
                for (size_t i = 0; ok && i < atom_arity(f->atom); i++)
                    ok = typecheck_term(cxt, f->args[i], TYPEINST_ANY);
            }
            return ok;
        }
        default:
            panic("invalid term type (%d)", type(t));
    }
}

/*
 * Typecheck a variable.
 */
static bool typecheck_var(context_t cxt, var_t x, typeinst_t type)
{
    type = typeinst_make_ground(type);
    if (type == TYPEINST_ANY)
        return true;
    vtype_t vtype;
    if (!typeinfo_search(cxt->vinfo, x, &vtype))
    {
        vtype = (vtype_t)gc_malloc(sizeof(struct vtype_s));
        vtype->type = type;
        cxt->vinfo = typeinfo_destructive_insert(cxt->vinfo, x, vtype);
        return true;
    }
    if (vtype->type == TYPEINST_ANY)
    {
        typecheck_set_var(cxt, x, type);
        return true;
    }
    return typecheck_expect(cxt, term_var(x), vtype->type, type);
}

/*
 * Set the type of a variable.
 */
static void typecheck_set_var(context_t cxt, var_t x, typeinst_t type)
{
    type = typeinst_make_ground(type);
    vtype_t vtype;
    if (!typeinfo_search(cxt->vinfo, x, &vtype))
    {
        vtype = (vtype_t)gc_malloc(sizeof(struct vtype_s));
        vtype->type = type;
        cxt->vinfo = typeinfo_destructive_insert(cxt->vinfo, x, vtype);
        return;
    }
    vtype->type = type;
}

/*
 * Unify two type variables.
 */
static void typecheck_unify(context_t cxt, var_t x, var_t y)
{
    vtype_t vtype;
    if (typeinfo_search(cxt->vinfo, x, &vtype))
    {
        cxt->vinfo = typeinfo_destructive_insert(cxt->vinfo, y, vtype);
        return;
    }
    if (typeinfo_search(cxt->vinfo, y, &vtype))
    {
        cxt->vinfo = typeinfo_destructive_insert(cxt->vinfo, x, vtype);
        return;
    }
    vtype = (vtype_t)gc_malloc(sizeof(struct vtype_s));
    vtype->type = TYPEINST_ANY;
    cxt->vinfo = typeinfo_destructive_insert(cxt->vinfo, x, vtype);
    cxt->vinfo = typeinfo_destructive_insert(cxt->vinfo, y, vtype);
}

/*
 * Get the type of a term.
 */
extern typesig_t typeinst_lookup_typesig(atom_t atom)
{
    typesig_t sig = TYPESIG_DEFAULT;
    tsiginfo_search(tinfo, atom, &sig);
    return sig;
}

/*
 * Report error if types do not match.
 */
static bool typecheck_expect(context_t cxt, term_t t, typeinst_t expected,
    typeinst_t actual)
{
    if (expected == TYPEINST_ANY || expected == TYPEINST_VAR_ANY)
        return true;
    if (typeinst_make_ground(expected) != typeinst_make_ground(actual))
    {
        error("(%s: %zu) type error with expression `!y%s!d'; expected type "
            "(!g%s!d), found (!g%s!d)", cxt->file, cxt->line, show(t),
            typeinst_show(expected), typeinst_show(actual));
        return false;
    }
    return true;
}

/*
 * Report an error.
 */
static bool typecheck_unexpected(context_t cxt, term_t t)
{
    error("(%s: %zu) type error with expression `!y%s!d'; unexpected term "
        "type", cxt->file, cxt->line, show(t));
    return false;
}

/*
 * Convert a typeinst into a string.
 */
extern const char *typeinst_show(typeinst_t type)
{
    switch (type)
    {
        case TYPEINST_ANY: case TYPEINST_VAR_ANY:
            return "any";
        case TYPEINST_NIL: case TYPEINST_VAR_NIL:
            return "nil";
        case TYPEINST_BOOL: case TYPEINST_VAR_BOOL:
            return "bool";
        case TYPEINST_NUM: case TYPEINST_VAR_NUM:
            return "int";
        case TYPEINST_STRING: case TYPEINST_VAR_STRING:
            return "str";
        case TYPEINST_ATOM: case TYPEINST_VAR_ATOM:
            return "atom";
        default:
        {
            atom_t atom = (atom_t)type;
            return atom_name(atom);
        }
    }
}

/*
 * Make an typesig_t.
 */
extern typesig_t typeinst_make_typesig(size_t aty, typeinst_t type,
    typeinst_t *args)
{
    typesig_t sig = (typesig_t)gc_malloc(sizeof(struct typesig_s) +
        aty*sizeof(typeinst_t));
    sig->type = type;
    for (size_t i = 0; i < aty; i++)
        sig->args[i] = args[i];
    return sig;
}

/*
 * Check if two typesigs are equal.
 */
extern bool typesig_eq(size_t arity, typesig_t sig1, typesig_t sig2)
{
    if (sig1 == TYPESIG_DEFAULT)
    {
        if (sig2 == TYPESIG_DEFAULT)
            return true;
typesig_eq_default_check:
        if (sig2->type != TYPEINST_BOOL)
            return false;
        for (size_t i = 0; i < arity; i++)
        {
            if (sig2->args[i] != TYPEINST_VAR_BOOL)
                return false;
        }
        return true;
    }
    else if (sig2 == TYPESIG_DEFAULT)
    {
        sig2 = sig1;
        sig1 = TYPESIG_DEFAULT;
        goto typesig_eq_default_check;
    }
    if (sig1->type != sig2->type)
        return false;
    for (size_t i = 0; i < arity; i++)
    {
        if (sig1->args[i] != sig2->args[i])
            return false;
    }
    return true;
}

/*
 * Initialize this module.
 */
extern void typecheck_init(void)
{
    tsiginfo_t info = tsiginfo_init();

    typesig_t sig_bb  = make_typesig(TYPEINST_BOOL, TYPEINST_BOOL);
    typesig_t sig_bbb = make_typesig(TYPEINST_BOOL, TYPEINST_BOOL,
        TYPEINST_BOOL);
    typesig_t sig_bnn = make_typesig(TYPEINST_BOOL, TYPEINST_NUM,
        TYPEINST_NUM);
    typesig_t sig_nn = make_typesig(TYPEINST_NUM, TYPEINST_NUM);
    typesig_t sig_nnn = make_typesig(TYPEINST_NUM, TYPEINST_NUM, TYPEINST_NUM);

    info = tsiginfo_destructive_insert(info, ATOM_NOT, sig_bb);
    info = tsiginfo_destructive_insert(info, ATOM_AND, sig_bbb);
    info = tsiginfo_destructive_insert(info, ATOM_OR, sig_bbb);
    info = tsiginfo_destructive_insert(info, ATOM_IMPLIES, sig_bbb);
    info = tsiginfo_destructive_insert(info, ATOM_IFF, sig_bbb);
    info = tsiginfo_destructive_insert(info, ATOM_XOR, sig_bbb);
    info = tsiginfo_destructive_insert(info, ATOM_LT, sig_bnn);
    info = tsiginfo_destructive_insert(info, ATOM_LEQ, sig_bnn);
    info = tsiginfo_destructive_insert(info, ATOM_GT, sig_bnn);
    info = tsiginfo_destructive_insert(info, ATOM_GEQ, sig_bnn);
    info = tsiginfo_destructive_insert(info, ATOM_NEG, sig_nn);
    info = tsiginfo_destructive_insert(info, ATOM_ADD, sig_nnn);
    info = tsiginfo_destructive_insert(info, ATOM_SUB, sig_nnn);
    info = tsiginfo_destructive_insert(info, ATOM_MUL, sig_nnn);
    info = tsiginfo_destructive_insert(info, ATOM_DIV, sig_nnn);

    if (!gc_root(&tinfo, sizeof(tinfo)))
        panic("failed to register GC root for tsiginfo map: %s",
            strerror(errno));

    tinfo = info;
}

/*
 * Add a new type declaration.
 */
extern bool typeinst_declare(atom_t atom, typesig_t sig)
{
    if (sig == TYPESIG_DEFAULT)     // XXX: DEFAULT overwritable?
        return true;
    typesig_t sig_0;
    if (tsiginfo_search(tinfo, atom, &sig_0))
    {
        if (sig == sig_0)
            return true;
        if (typeinst_decl_type(sig) != typeinst_decl_type(sig_0))
            return false;
        for (size_t i = 0; i < atom_arity(atom); i++)
        {
            if (typeinst_decl_arg(sig_0, i) != typeinst_decl_arg(sig, i))
                return false;
        }
        return true;
    }
    tinfo = tsiginfo_destructive_insert(tinfo, atom, sig);
    return true;
}

/*
 * Lookup a type declaration.
 */
extern typesig_t typeinst_get_decl(atom_t atom)
{
    typesig_t sig = TYPESIG_DEFAULT;
    tsiginfo_search(tinfo, atom, &sig);
    return sig;
}

/*
 * Add a new type name.
 */
extern typeinst_t typeinst_make(const char *name)
{
    if (strcmp(name, "any") == 0)
        return TYPEINST_ANY;
    if (strcmp(name, "num") == 0)
        return TYPEINST_NUM;
    if (strcmp(name, "int") == 0)
        return TYPEINST_NUM;
    if (strcmp(name, "bool") == 0)
        return TYPEINST_BOOL;
    if (strcmp(name, "atom") == 0)
        return TYPEINST_ATOM;
    if (strcmp(name, "str") == 0)
        return TYPEINST_STRING;
    if (strcmp(name, "nil") == 0)
        return TYPEINST_NIL;
    atom_t atom = make_atom(name, 0);
    return (typeinst_t)atom;
}

/*
 * Make a var typeinst from a typeinst.
 */
extern typeinst_t typeinst_make_var(typeinst_t type)
{
    switch (type)
    {
        case TYPEINST_ANY: case TYPEINST_VAR_ANY:
            return TYPEINST_VAR_ANY;
        case TYPEINST_NIL: case TYPEINST_VAR_NIL:
            return TYPEINST_VAR_NIL;
        case TYPEINST_BOOL: case TYPEINST_VAR_BOOL:
            return TYPEINST_VAR_BOOL;
        case TYPEINST_NUM: case TYPEINST_VAR_NUM:
            return TYPEINST_VAR_NUM;
        case TYPEINST_STRING: case TYPEINST_VAR_STRING:
            return TYPEINST_VAR_STRING;
        case TYPEINST_ATOM: case TYPEINST_VAR_ATOM:
            return TYPEINST_VAR_ATOM;
        default:
        {
            atom_t atom = (atom_t)type;
            atom = atom_set_arity(atom, TYPEINST_VAR_ARITY);
            return (typeinst_t)atom;
        }
    }
}

/*
 * Make a ground typeinst from a typeinst.
 */
extern typeinst_t typeinst_make_ground(typeinst_t type)
{
    switch (type)
    {
        case TYPEINST_ANY: case TYPEINST_VAR_ANY:
            return TYPEINST_ANY;
        case TYPEINST_NIL: case TYPEINST_VAR_NIL:
            return TYPEINST_NIL;
        case TYPEINST_BOOL: case TYPEINST_VAR_BOOL:
            return TYPEINST_BOOL;
        case TYPEINST_NUM: case TYPEINST_VAR_NUM:
            return TYPEINST_NUM;
        case TYPEINST_STRING: case TYPEINST_VAR_STRING:
            return TYPEINST_STRING;
        case TYPEINST_ATOM: case TYPEINST_VAR_ATOM:
            return TYPEINST_ATOM;
        default:
        {
            atom_t atom = (atom_t)type;
            atom = atom_set_arity(atom, TYPEINST_GROUND_ARITY);
            return (typeinst_t)atom;
        }
    }
}

