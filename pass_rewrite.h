/*
 * pass_rewrite.h
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
#ifndef __PASS_REWRITE_H
#define __PASS_REWRITE_H

#include <stdbool.h>

#include "expr.h"

extern atom_t ATOM_REWRITE;

/*
 * Register a rewrite rule:
 */
extern bool register_rewrite_rule(term_t rule, const char *filename,
    size_t lineno);

/*
 * Rewrite pass.
 */
extern void rewrite_init(void);
extern expr_t pass_rewrite_expr(const char *filename, size_t lineno, expr_t e);

#endif      /* __PASS_REWRITE_H */
