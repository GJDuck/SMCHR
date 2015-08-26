/*
 * typecheck.h
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
#ifndef __TYPECHECK_H
#define __TYPECHECK_H

#include <stdarg.h>

#include "map.h"
#include "term.h"

/*
 * Built-in expression types.
 */
enum typeinst_e
{
    TYPEINST_ANY = 0,
    TYPEINST_NIL,
    TYPEINST_BOOL,
    TYPEINST_NUM,
    TYPEINST_STRING,
    TYPEINST_ATOM,
    TYPEINST_VAR_ANY,
    TYPEINST_VAR_NIL,
    TYPEINST_VAR_BOOL,
    TYPEINST_VAR_NUM,
    TYPEINST_VAR_STRING,
    TYPEINST_VAR_ATOM
};
typedef word_t typeinst_t;

/*
 * Function (inc. constraint/predicate) type signatures.
 */
struct typesig_s
{
    typeinst_t type;        // Function "return" type.
    typeinst_t args[];      // Function arg types.
};
typedef struct typesig_s *typesig_t;
#define TYPESIG_DEFAULT     ((typesig_t)NULL)

/*
 * Type information.
 */
typedef struct typeinfo_s *typeinfo_t;

/*
 * Typecheck pass.
 */
extern void typecheck_init(void);
extern bool typecheck(const char *file, size_t line, term_t t,
    typeinfo_t *info);

/*
 * Misc.
 */
extern typeinst_t typecheck_typeof(typeinfo_t info, term_t t);
extern const char *typeinst_show(typeinst_t type);
extern typesig_t typeinst_make_typesig(size_t aty, typeinst_t type,
    typeinst_t *args);
#define make_typesig(type, ...)                                             \
    typeinst_make_typesig(VA_ARGS_LENGTH(typeinst_t, ##__VA_ARGS__), (type),\
        (typeinst_t []){__VA_ARGS__})
extern bool typesig_eq(size_t arity, typesig_t sig1, typesig_t sig2);
extern typesig_t typeinst_lookup_typesig(atom_t atom);
extern bool typeinst_declare(atom_t atom, typesig_t sig);
extern typesig_t typeinst_get_decl(atom_t atom);
extern typeinst_t typeinst_make(const char *name);
#define make_typeinst(name)     typeinst_make(name)
extern typeinst_t typeinst_make_var(typeinst_t type);
#define make_var_typeinst(type) typeinst_make_var(type)
extern typeinst_t typeinst_make_ground(typeinst_t type);
static inline typeinst_t typeinst_decl_type(typesig_t sig)
{
    if (sig == TYPESIG_DEFAULT)
        return TYPEINST_BOOL;
    return sig->type;
}
static inline typeinst_t typeinst_decl_arg(typesig_t sig, size_t idx)
{
    if (sig == TYPESIG_DEFAULT)
        return TYPEINST_VAR_ANY;
    return sig->args[idx];
}

#endif      /* __TYPECHECK_H */

