/*
 * op.h
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
#ifndef __OP_H
#define __OP_H

#include <stdbool.h>

#include "map.h"
#include "misc.h"

/*
 * Operator representation.
 */
enum assoc_e
{
    XFY,
    YFX,
    XFX
};
typedef enum assoc_e assoc_t;

/*
 * User-define operators.
 */
typedef struct opspec_s *opspec_t;
MAP_DECL(opinfo, char *, opspec_t, strcmp_compare);

extern opinfo_t binop_register(opinfo_t opinfo, char *binop, assoc_t assoc,
    unsigned priority, bool ac, bool space);
extern opinfo_t unop_register(opinfo_t opinfo, char *unop, unsigned priority,
    bool space);

extern bool binop_lookup(opinfo_t opinfo, const char *binop, assoc_t *assoc,
    unsigned *priority, bool *ac, bool *space);
extern bool unop_lookup(opinfo_t opinfo, const char *unop, unsigned *priority,
    bool *space);

#endif      /* __OP_H */
