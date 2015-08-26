/*
 * smchr.h
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
#ifndef __SMCHR_H
#define __SMCHR_H

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "gc.h"
#include "log.h"
#include "map.h"
#include "misc.h"
#include "parse.h"
#include "show.h"
#include "term.h"
#include "word.h"

/****************************************************************************/
/* MAIN API                                                                 */
/****************************************************************************/

/*
 * Initialize the SMCHR system.
 */
extern void smchr_init(void);

/*
 * Load a solver by name.
 * Returns `true' on success, or `false' on failure.
 */
extern bool smchr_load(const char *name);

/*
 * Execute a goal.  Returns one of two values:
 * - `false' (TERM_FALSE) if the goal is unsatisfiable.
 * - `nil' (TERM_NIL) if execution of the goal was aborted.
 * - Otherwise returns a conjunction of constraints for which the theory
 *   solver(s) cannot prove unsatisfiability.  These constraints may still be
 *   unsatisfiable if the theory solver(s) are incomplete.
 */
extern term_t smchr_execute(const char *filename, size_t lineno, term_t goal);

#ifdef __cplusplus
}
#endif

#endif      /* __SMCHR_H */
