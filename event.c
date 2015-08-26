/*
 * event.c
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

#include "solver.h"

/*
 * Set event for a constraint.
 */
static void solver_event(cons_t c, event_t e)
{
    sym_t sym = c->sym;
    prop_t props = propagator(c);
    for (size_t i = 0; i < sym->propinfo_len; i++)
    {
        prop_t prop = props+i;
        if (iskilled(prop))
            continue;
        if (isscheduled(prop))
            continue;
        if (shouldwake(prop, e))
            schedule(prop);
    }
}

/*
 * Decision event.
 */
extern void solver_event_decision(cons_t c)
{
    if (ispurged(c))
        return;
    event_t e = (decision(c->b) == TRUE? EVENT_TRUE: EVENT_FALSE);
    solver_event(c, e);
}

/*
 * Binding event.
 */
extern void solver_event_bind(var_t x, var_t y)
{
    conslist_t cs = solver_var_search(x);
    while (cs != NULL)
    {
        cons_t c = cs->cons;
        cs = cs->next;
        if (ispurged(c))
            continue;
        if (decision(c->b) == UNKNOWN)
            continue;
        solver_event(c, EVENT_BIND);
    }

    cs = solver_var_search(y);
    while (cs != NULL)
    {
        cons_t c = cs->cons;
        cs = cs->next;
        if (ispurged(c))
            continue;
        if (decision(c->b) == UNKNOWN)
            continue;
        solver_event(c, EVENT_BIND);
    }
}

/*
 * Delay on a user event.
 */
extern proplist_t solver_delay_user(prop_t prop, proplist_t ps)
{
    proplist_t head = (proplist_t)gc_malloc(sizeof(struct proplist_s));
    head->prop = prop;
    head->next = ps;
    return head;
}

/*
 * Signal a user event.
 */
extern void solver_event_user(proplist_t ps)
{
    while (ps != NULL)
    {
        prop_t prop = ps->prop;
        ps = ps->next;
        if (iskilled(prop))
            continue;
        if (isscheduled(prop))
            continue;
        schedule(prop);
    }
}

