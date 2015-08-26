/*
 * trail.c
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

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "misc.h"
#include "solver.h"

/*
 * The trail.
 */
trailentry_t __solver_trail;
size_t __solver_trail_len;

static void *func_min = (void *)UINT64_MAX;
static void *func_max = 0;

/*
 * Initialize the trail.
 */
extern void solver_init_trail(void)
{
    __solver_trail_len = 0;
    size_t size = sizeof(struct trailentry_s)*0x7FFFFFFF;
    __solver_trail = (trailentry_t)buffer_alloc(size);
    if (!gc_dynamic_root((void **)&__solver_trail, &__solver_trail_len, 
            sizeof(struct trailentry_s)))
        panic("failed to set GC dynamic root for trail: %s",
            strerror(errno));
}

/*
 * Reset the trail,
 */
extern void solver_reset_trail(void)
{
    __solver_trail_len = 0;
}

/*
 * Trail a function.
 */
extern void solver_trail_func(trailfunc_t f, word_t arg)
{
    void *fptr = (void *)f;
    if (fptr < func_min)
        func_min = fptr;
    if (fptr > func_max)
        func_max = fptr;
    __solver_trail[__solver_trail_len].ptr = fptr;
    __solver_trail[__solver_trail_len].val = arg;
    __solver_trail_len++;
}

/*
 * Test if 'ptr' is a function.
 */
static inline bool trail_is_func(void *ptr)
{
    return (ptr <= func_max && ptr >= func_min);
}

/*
 * Unwind the trail.
 */
extern void solver_backtrack(choicepoint_t cp)
{
    debug("!cBACKTRACK!d cp=%zu", cp);
    for (ssize_t i = (ssize_t)__solver_trail_len-1; i >= (ssize_t)cp; i--)
    {
        word_t *ptr = __solver_trail[i].ptr;
        word_t val  = __solver_trail[i].val;
        if (trail_is_func(ptr))
        {
            trailfunc_t f = (trailfunc_t)ptr;
            f(val);
        }
        else
            *ptr = val;
    }
    __solver_trail_len = cp;
}

