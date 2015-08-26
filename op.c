/*
 * op.c
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

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "misc.h"
#include "op.h"

/*
 * Operator specification.
 */
struct opspec_s
{
    char *name;                     // Operator's name.

    /* BINOP */
    bool is_binop;                  // Is binop?
    assoc_t binop_assoc;            // Operator's assocativity.
    unsigned binop_priority;        // Operator's priority.
    bool binop_ac;                  // Operator is AC?
    bool binop_space;               // Operator printed with a space?

    /* UNOP */
    bool is_unop;                   // Is unop?
    unsigned unop_priority;         // Operator's priority.
    bool unop_space;                // Operator printed with a space?
};
typedef struct opspec_s *opspec_t;

/*
 * Op-table.
 */
static const struct opspec_s binoptable[] =
{
    {"!=",  true, XFX, 700,  false, true,  false, 0, false},
    {"*",   true, XFY, 400,  true,  false, false, 0, false},
    {"+",   true, YFX, 500,  true,  true,  false, 0, false},
    {"-",   true, YFX, 500,  false, true,  false, 0, false},
    {"->",  true, XFY, 900,  false, true,  false, 0, false},
    {"/",   true, YFX, 400,  false, false, false, 0, false},
    {"/\\", true, XFY, 1000, true,  true,  false, 0, false},
    {"<",   true, XFX, 700,  false, true,  false, 0, false},
    {"<->", true, XFY, 900,  true,  true,  false, 0, false},
    {"<=",  true, XFX, 700,  false, true,  false, 0, false},
    {"=",   true, XFX, 700,  false, true,  false, 0, false},
    {">",   true, XFX, 700,  false, true,  false, 0, false},
    {">=",  true, XFX, 700,  false, true,  false, 0, false},
    {"\\/", true, XFY, 1100, true,  true,  false, 0, false},
    {"^",   true, XFY, 200,  false, false, false, 0, false},
    {"xor", true, YFX, 500,  true,  true,  false, 0, false},
};

/*
 * Operator comparison.
 */
static int binop_compare(const void *a, const void *b)
{
    const opspec_t a1 = (const opspec_t)a;
    const opspec_t b1 = (const opspec_t)b;
    return strcmp(a1->name, b1->name);
}

/*
 * Register a new binary operator.
 */
extern opinfo_t binop_register(opinfo_t opinfo, char *binop, assoc_t assoc,
    unsigned priority, bool ac, bool space)
{
    opspec_t opspec;
    if (!opinfo_search(opinfo, binop, &opspec))
    {
        opspec = (opspec_t)gc_malloc(sizeof(struct opspec_s));
        memset(opspec, 0, sizeof(struct opspec_s));
        if (!gc_isptr(binop))
        {
            size_t len = strlen(binop);
            char *binop_1 = (char *)gc_malloc(len + 1);
            strcpy(binop_1, binop);
            binop = binop_1;
            opspec->name = binop;
        }
        opinfo = opinfo_insert(opinfo, binop, opspec);
    }
    opspec->is_binop = true;
    opspec->binop_assoc = assoc;
    opspec->binop_priority = priority;
    opspec->binop_ac = ac;
    opspec->binop_space = space;
    return opinfo;
}

/*
 * Register a new unary operator.
 */
extern opinfo_t unop_register(opinfo_t opinfo, char *unop, unsigned priority,
    bool space)
{
    opspec_t opspec;
    if (!opinfo_search(opinfo, unop, &opspec))
    {
        opspec = (opspec_t)gc_malloc(sizeof(struct opspec_s));
        memset(opspec, 0, sizeof(struct opspec_s));
        if (!gc_isptr(unop))
        {
            size_t len = strlen(unop);
            char *unop_1 = (char *)gc_malloc(len + 1);
            strcpy(unop_1, unop);
            unop = unop_1;
            opspec->name = unop;
        }
        opinfo = opinfo_insert(opinfo, unop, opspec);
    }
    opspec->is_unop = true;
    opspec->unop_priority = priority;
    opspec->unop_space = space;
    return opinfo;
}

/*
 * Operator lookup.
 */
extern bool binop_lookup(opinfo_t opinfo, const char *binop, assoc_t *assoc,
    unsigned *priority, bool *ac, bool *space)
{
    struct opspec_s key = {(char *)binop, false, 0, 0, false, false, 0, false};
    opspec_t val = bsearch(&key, binoptable,
        sizeof(binoptable)/sizeof(struct opspec_s), sizeof(struct opspec_s),
        binop_compare);
    if (val == NULL)
    {
        if (!opinfo_search(opinfo, (char *)binop, &val))
            return false;
    }
    if (!val->is_binop)
        return false;
    if (assoc != NULL)
        *assoc = val->binop_assoc;
    if (priority != NULL)
        *priority = val->binop_priority;
    if (ac != NULL)
        *ac = val->binop_ac;
    if (space != NULL)
        *space = val->binop_space;
    return true;
}

/*
 * Operator lookup.
 */
extern bool unop_lookup(opinfo_t opinfo, const char *unop, unsigned *priority,
    bool *space)
{
    opspec_t opspec;
    if (strcmp(unop, "-") == 0)
    {
        if (priority != NULL)
            *priority = 200;
        if (space != NULL)
            *space = false;
    }
    else if (strcmp(unop, "not") == 0)
    {
        if (priority != NULL)
            *priority = 850;        // XXX: 900 -> 850
        if (space != NULL)
            *space = true;
    }
    else if (opinfo_search(opinfo, (char *)unop, &opspec))
    {
        if (!opspec->is_unop)
            return false;
        if (priority != NULL)
            *priority = opspec->unop_priority;
        if (space != NULL)
            *space = opspec->unop_space;
    }
    else
        return false;
    return true;
}

