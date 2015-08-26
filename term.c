/*
 * term.c
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

#include "log.h"
#include "map.h"
#include "misc.h"
#include "term.h"

/****************************************************************************/
/* NIL                                                                      */
/****************************************************************************/

/*
 * Compare nils.
 */
extern int_t term_compare_nil(nil_t na, nil_t nb)
{
    return 0;
}

/****************************************************************************/
/* BOOLEANS                                                                 */
/****************************************************************************/

/*
 * Compare bools.
 */
extern int_t term_compare_bool(bool_t ba, bool_t bb)
{
    return (int_t)ba - (int_t)bb;
}

/****************************************************************************/
/* NUMBERS                                                                  */
/****************************************************************************/

/*
 * Make a number.
 */
extern term_t term_make_num(num_t n)
{
    word_t w = word_makedouble(n);
    switch (w & NUM_EXPONENT_MASK)
    {
        case 0:                     // 0.0, -0.0, subnormals
            return (term_t)word_settag(0, TAG_NUM);
        case NUM_EXPONENT_MASK:     // inf, -inf, NaNs
            if ((w & NUM_FRACTION_MASK) != 0)
                return (term_t)word_settag(NUM_UNDEFINED, TAG_NUM);
            return (term_t)word_settag(w, TAG_NUM);
        default:                    // Normal numbers
            w += WORD_TAG_MASK/2;
            if ((w & NUM_EXPONENT_MASK) == NUM_EXPONENT_MASK)
                w &= ~NUM_FRACTION_MASK;
            return (term_t)word_settag(word_striptag(w), TAG_NUM);
    }
}

/*
 * Compare numbers.
 */
extern int_t term_compare_num(num_t na, num_t nb)
{
    if (na < nb)
        return -1;
    if (na > nb)
        return 1;
    return 0;
}

/****************************************************************************/
/* STRINGS                                                                  */
/****************************************************************************/

/*
 * Build a str_t.
 */
extern str_t term_build_string(const char *s)
{
    size_t len = strlen(s);
    str_t str = (str_t)gc_malloc(sizeof(struct str_s) + len + 1);
    str->len = len;
    strcpy(str->chars, s);
    return str;
}

/*
 * Compare strings.
 */
extern int_t term_compare_string(str_t sa, str_t sb)
{
    size_t len = (sa->len < sb->len? sa->len: sb->len);
    return (int_t)memcmp(sa->chars, sb->chars, len+1);
}

/****************************************************************************/
/* ATOMS                                                                    */
/****************************************************************************/

/*
 * Built-in atoms.
 */
atom_t ATOM_NOT;
atom_t ATOM_AND;
atom_t ATOM_OR;
atom_t ATOM_IMPLIES;
atom_t ATOM_IFF;
atom_t ATOM_XOR;
atom_t ATOM_EQ;
atom_t ATOM_NEQ;
atom_t ATOM_LT;
atom_t ATOM_LEQ;
atom_t ATOM_GT;
atom_t ATOM_GEQ;
atom_t ATOM_NEG;
atom_t ATOM_ADD;
atom_t ATOM_SUB;
atom_t ATOM_MUL;
atom_t ATOM_DIV;
atom_t ATOM_INT_EQ;
atom_t ATOM_NIL_EQ;
atom_t ATOM_STR_EQ;
atom_t ATOM_ATOM_EQ;
atom_t ATOM_INT_EQ_C;
atom_t ATOM_NIL_EQ_C;
atom_t ATOM_STR_EQ_C;
atom_t ATOM_ATOM_EQ_C;
atom_t ATOM_INT_EQ_PLUS;
atom_t ATOM_INT_EQ_PLUS_C;
atom_t ATOM_INT_EQ_MUL;
atom_t ATOM_INT_EQ_MUL_C;
atom_t ATOM_INT_EQ_POW_C;
atom_t ATOM_INT_GT;
atom_t ATOM_INT_GT_C;

/*
 * Atom data-structures.
 */
MAP_DECL(namemap, char *, size_t, strcmp_compare);
static namemap_t atom_namemap;
static size_t atom_next_id = 0;
char **atom_idmap;

/*
 * Build an atom_t.
 */
extern atom_t term_build_atom(const char *name, size_t arity)
{
    word_t atom_name =
        ((word_t)name[0] << 24) | (name[0] == '\0'? 0:
        ((word_t)name[1] << 16) | (name[1] == '\0'? 0:
        ((word_t)name[2] << 8)  | (name[2] == '\0'? 0:
        ((word_t)name[3]))));
    word_t atom_id;
    if (!namemap_search(atom_namemap, (char *)name, &atom_id))
    {
        if (!gc_isptr(name))
        {
            char *name_1 = (char *)gc_malloc(strlen(name)+1);
            strcpy(name_1, name);
            name = (const char *)name_1;
        }
        atom_id = atom_next_id++;
        atom_idmap[atom_id] = (char *)name;
        atom_namemap = namemap_destructive_insert(atom_namemap, (char *)name,
            atom_id);
    }
    word_t atom_arity = (word_t)arity;
    atom_t atom =
        ((atom_name << ATOM_NAME_SHIFT) & ATOM_NAME_MASK) |
        ((atom_id << ATOM_ID_SHIFT) & ATOM_ID_MASK) |
        ((atom_arity << ATOM_ARITY_SHIFT) & ATOM_ARITY_MASK);
    return atom;
}

/*
 * Compare atoms.
 */
extern int_t term_compare_atom(atom_t aa, atom_t ab)
{
    int_t atom_name_a = (int_t)(aa >> ATOM_NAME_SHIFT);
    int_t atom_name_b = (int_t)(ab >> ATOM_NAME_SHIFT);
    int_t diff = atom_name_a - atom_name_b;
    if (diff != 0)
        return diff;
    size_t atom_id_a = (aa & ATOM_ID_MASK) >> ATOM_ID_SHIFT;
    size_t atom_id_b = (ab & ATOM_ID_MASK) >> ATOM_ID_SHIFT;
    if (atom_id_a != atom_id_b)
    {
        const char *name_a = atom_idmap[atom_id_a];
        const char *name_b = atom_idmap[atom_id_b];
        return (int_t)strcmp(name_a, name_b);
    }
    int_t atom_arity_a = (int_t)((aa & ATOM_ARITY_MASK) >> ATOM_ARITY_SHIFT);
    int_t atom_arity_b = (int_t)((ab & ATOM_ARITY_MASK) >> ATOM_ARITY_SHIFT);
    return atom_arity_a - atom_arity_b;
}

/*
 * Get atom name.
 */
extern const char *term_get_atom_name(atom_t atom)
{
    size_t id = (atom & ATOM_ID_MASK) >> ATOM_ID_SHIFT;
    return atom_idmap[id];
}

/****************************************************************************/
/* FUNCTORS                                                                 */
/****************************************************************************/

/*
 * Build a functor.
 */
extern func_t term_build_func(atom_t atom, ...)
{
    va_list ap;
    va_start(ap, atom);
    size_t arity = term_get_atom_arity(atom);
    func_t f = (func_t)gc_malloc(sizeof(struct func_s) + arity*sizeof(term_t));
    f->atom = atom;
    for (size_t i = 0; i < arity; i++)
    {
        term_t arg = va_arg(ap, term_t);
        f->args[i] = arg;
    }
    va_end(ap);
    return f;
}

/*
 * Build a functor.
 */
extern func_t term_build_func_a(atom_t atom, term_t *args)
{
    size_t arity = term_get_atom_arity(atom);
    func_t f = (func_t)gc_malloc(sizeof(struct func_s) + arity*sizeof(term_t));
    f->atom = atom;
    for (size_t i = 0; i < arity; i++)
        f->args[i] = args[i];
    return f;
}

/*
 * Compare functors.
 */
extern int_t term_compare_func(func_t fa, func_t fb)
{
    atom_t aa = fa->atom;
    atom_t ab = fb->atom;
    int_t cmp = term_compare_atom(aa, ab);
    if (cmp != 0)
        return cmp;
    size_t aty = term_get_atom_arity(aa);
    for (size_t i = 0; i < aty; i++)
    {
        term_t arga = fa->args[i];
        term_t argb = fb->args[i];
        cmp = term_compare(arga, argb);
        if (cmp != 0)
            return cmp;
    }
    return 0;
}

/****************************************************************************/
/* FOREIGN                                                                  */
/****************************************************************************/

/*
 * Compare foreigns.
 */
extern int_t term_compare_foreign(foreign_t fa, foreign_t fb)
{
    return (int_t)fa - (int_t)fb;
}

/****************************************************************************/
/* VARS                                                                     */
/****************************************************************************/

/*
 * Build a variable.
 */
extern var_t term_build_var(const char *name)
{
    var_t v = (var_t)gc_malloc(sizeof(struct var_s));
    if (name != NULL && !gc_isptr(name))
    {
        char *name_1 = (char *)gc_malloc(strlen(name) + 1);
        strcpy(name_1, name);
        name = (const char *)name_1;
    }
    v->name = name;
    return v;
}

/*
 * Compare variables.
 */
extern int_t term_compare_var(var_t va, var_t vb)
{
    if (va == vb)
        return 0;
    const char *name_a = va->name;
    const char *name_b = vb->name;
    if (name_a == NULL)
    {
        if (name_b == NULL)
            return (int_t)va - (int_t)vb;
        return -1;
    }
    if (name_b == NULL)
        return 1;
    return (int_t)strcmp(name_a, name_b);
}

/****************************************************************************/
/* GENERAL                                                                  */
/****************************************************************************/

/*
 * Initialize this module.
 */
extern void term_init(void)
{
    atom_namemap = namemap_init();
    if (!gc_root(&atom_namemap, sizeof(atom_namemap)))
        panic("failed to register GC root for atom name map: %s",
            strerror(errno));

    // Note: there is no need to make atom_idmap a GC root -- since it only
    //       contains pointers that are referenced by atom_namemap.
    size_t size = ATOM_MAX_ID*sizeof(const char *);
    atom_idmap = (char **)buffer_alloc(size);

    // "Built-in" atoms:
    ATOM_NOT     = make_atom("not", 1);
    ATOM_AND     = make_atom("/\\", 2);
    ATOM_OR      = make_atom("\\/", 2);
    ATOM_IMPLIES = make_atom("->", 2);
    ATOM_IFF     = make_atom("<->", 2);
    ATOM_XOR     = make_atom("xor", 2);
    ATOM_EQ      = make_atom("=", 2);
    ATOM_NEQ     = make_atom("!=", 2);
    ATOM_LT      = make_atom("<", 2);
    ATOM_LEQ     = make_atom("<=", 2);
    ATOM_GT      = make_atom(">", 2);
    ATOM_GEQ     = make_atom(">=", 2);
    ATOM_NEG     = make_atom("-", 1);
    ATOM_ADD     = make_atom("+", 2);
    ATOM_SUB     = make_atom("-", 2);
    ATOM_MUL     = make_atom("*", 2);
    ATOM_DIV     = make_atom("/", 2);

    ATOM_INT_EQ        = make_atom("int_eq", 2);
    ATOM_NIL_EQ        = make_atom("nil_eq", 2);
    ATOM_STR_EQ        = make_atom("str_eq", 2);
    ATOM_ATOM_EQ       = make_atom("atom_eq", 2);
    ATOM_INT_EQ_C      = make_atom("int_eq_c", 2);
    ATOM_NIL_EQ_C      = make_atom("nil_eq_c", 2);
    ATOM_STR_EQ_C      = make_atom("str_eq_c", 2);
    ATOM_ATOM_EQ_C     = make_atom("atom_eq_c", 2);
    ATOM_INT_EQ_PLUS   = make_atom("int_eq_plus", 3);
    ATOM_INT_EQ_PLUS_C = make_atom("int_eq_plus_c", 3);
    ATOM_INT_EQ_MUL    = make_atom("int_eq_mul", 3);
    ATOM_INT_EQ_MUL_C  = make_atom("int_eq_mul_c", 3);
    ATOM_INT_EQ_POW_C  = make_atom("int_eq_pow_c", 3);
    ATOM_INT_GT        = make_atom("int_gt", 2);
    ATOM_INT_GT_C      = make_atom("int_gt_c", 2);
}

/*
 * Compare terms.
 */
extern int_t term_compare(term_t a, term_t b)
{
    type_t ta = term_get_type(a);
    type_t tb = term_get_type(b);
    int_t diff = (int_t)ta - (int_t)tb;
    if (diff != 0)
        return diff;
    switch (ta)
    {
        case NIL:
            return term_compare_nil(term_get_nil(a), term_get_nil(b));
        case BOOL:
            return term_compare_bool(term_get_bool(a), term_get_bool(b));
        case NUM:
            return term_compare_num(term_get_num(a), term_get_num(b));
        case ATOM:
            return term_compare_atom(term_get_atom(a), term_get_atom(b));
        case STR:
            return term_compare_string(term_get_string(a), term_get_string(b));
        case FOREIGN:
            return term_compare_foreign(term_get_foreign(a),
                term_get_foreign(b));
        case VAR:
            return term_compare_var(term_get_var(a), term_get_var(b));
        case FUNC:
            return term_compare_func(term_get_func(a), term_get_func(b));
        default:
            panic("bad term type %u", ta);
    }
}

/*
 * Get the type name.
 */
extern const char *term_get_type_name(type_t type)
{
    switch (type)
    {
        case VAR:
            return "var";
        case BOOL:
            return "bool";
        case ATOM:
            return "atom";
        case NUM:
            return "num";
        case STR:
            return "str";
        case FUNC:
            return "func";
        case NIL:
            return "nil";
        case FOREIGN:
            return "foreign";
        default:
            return "<unknown>";
    }
}


