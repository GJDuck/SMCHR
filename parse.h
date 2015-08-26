/*
 * parse.h
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
#ifndef __PARSE_H
#define __PARSE_H

#include <stdbool.h>

#include "term.h"
#include "misc.h"
#include "map.h"
#include "op.h"

MAP_DECL(varset, char *, term_t, strcmp_compare);

extern void parse_init(void);
extern term_t parse_term(const char *filename, size_t *line, opinfo_t opinfo,
    const char *str, const char **end, varset_t *vars);

#endif      /* __PARSE_H */
