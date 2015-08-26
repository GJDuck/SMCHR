/*
 * debug.h
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

#ifndef __DEBUG_H
#define __DEBUG_H

#include "options.h"
#include "sat.h"

enum port_e
{
    DEBUG_FAIL,
    DEBUG_PROPAGATE,
    DEBUG_LEARN,
    DEBUG_SELECT
};
typedef enum port_e port_t;

extern size_t debug_state_jump;
extern size_t debug_state_skip;
extern size_t debug_state_step;

extern void debug_init(void);
extern void debug_step_0(port_t port, bool lazy, literal_t *lits, size_t len,
    const char *solver, size_t lineno);

static inline void debug_step(port_t port, bool lazy, literal_t *lits,
    size_t len, const char *solver, size_t lineno)
{
    if (option_debug_on)
        debug_step_0(port, lazy, lits, len, solver, lineno);
    debug_state_step++;
}

static inline void debug_enable(void)
{
    option_debug_on = true;
    debug_state_jump = debug_state_skip = 0;
}

#endif      /* __DEBUG_H */
