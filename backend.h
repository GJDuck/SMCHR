/*
 * backend.h
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
#ifndef __BACKEND_H
#define __BACKEND_H

#include <stdbool.h>

#include "expr.h"
#include "term.h"

/*
 * Solver backend.
 */
extern bool backend(const char *filename, size_t lineno, expr_t s, expr_t e);

#endif      /* __BACKEND_H */
