/*
 * solver_chr.h
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
#ifndef __SOLVER_CHR_H
#define __SOLVER_CHR_H

#include "solver.h"

extern solver_t solver_chr;

/*
 * Compile a CHR file.
 */
extern bool chr_compile(const char *filename);

#endif      /* __SOLVER_CHR_H */
