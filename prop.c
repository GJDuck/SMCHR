/*
 * prop.c
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

#include "solver.h"

#define MAX_PRIORITY        8

/*
 * The propagator queue.
 */
struct propqueue_s
{
    prop_t head;
    prop_t tail;
};
static struct propqueue_s propqueue[MAX_PRIORITY] = {{0}};
static uint_t priority = MAX_PRIORITY;
static prop_t current = NULL;

/*
 * Reset the propagator queue.
 */
extern void solver_reset_prop_queue(void)
{
    for (size_t i = 0; i < MAX_PRIORITY; i++)
        propqueue[i].head = propqueue[i].tail = NULL;
    current = NULL;
    priority = MAX_PRIORITY;
}

/*
 * Schedule a propagator.
 */
extern void solver_schedule_prop(prop_t prop)
{
    if (solver_is_prop_scheduled(prop))
    {
        if (prop != current)
            return;
        current = NULL;
    }

    cons_t c = constraint(prop);
    if (ispurged(c))
        return;
    sym_t sym = c->sym;
    propinfo_t info = sym->propinfo;

    prop->next = prop;
    if (propqueue[info->priority].head == NULL)
    {
        propqueue[info->priority].head = prop;
        propqueue[info->priority].tail = prop;
    }
    else
    {
        propqueue[info->priority].tail->next = prop;
        propqueue[info->priority].tail = prop;
    }
    priority = (priority <= info->priority? priority: info->priority);
    debug("!rSCHEDULE!d (%u) %s [%zu]",
        (prop->info & INFO_IDX_MASK) >> INFO_IDX_SHIFT, show_cons(c),
            priority);
}

/*
 * Wake a propagator.
 */
extern bool solver_wake_prop(void)
{
    prop_t prop;
    cons_t c;
    while (true)
    {
        if (priority == MAX_PRIORITY)
            return false;

        prop = propqueue[priority].head;
        debug("!rWAKE!d (%u) %s [%zu]",
            (prop->info & INFO_IDX_MASK) >> INFO_IDX_SHIFT,
            show_cons(constraint(prop)), priority);
        if (prop->next == prop)
        {
            propqueue[priority].head = NULL;
            propqueue[priority].tail = NULL;
            while (priority < MAX_PRIORITY && propqueue[priority].head == NULL)
                priority++;
        }
        else
            propqueue[priority].head = prop->next;

        if (iskilled(prop))
        {
            prop->next = NULL;
            continue;
        }
        c = constraint(prop);
        if (!ispurged(c))
            break;
        prop->next = NULL;
    }

    sym_t sym = c->sym;
    propinfo_t info = sym->propinfo + solver_propinfo_index(prop);
    current = prop;
    info->handler(prop);
    if (current != NULL)
    {
        current = NULL;
        prop->next = NULL;
    }

    while (priority < MAX_PRIORITY && propqueue[priority].head == NULL)
        priority++;

    return (priority != MAX_PRIORITY);
}

/*
 * Check if the propagation queue is empty.
 */
extern bool solver_is_queue_empty(void)
{
    return (priority == MAX_PRIORITY);
}

/*
 * Flush the propagator queue.
 */
extern void solver_flush_queue(void)
{
    while (priority < MAX_PRIORITY)
    {
        prop_t prop = propqueue[priority].head;
        if (prop == NULL)
        {
            priority++;
            continue;
        }
        while (prop->next != prop)
        {
            debug("!rFLUSH!d %s [%zu]", show_cons(constraint(prop)),
                priority);
            prop_t old_prop = prop;
            prop = prop->next;
            old_prop->next = NULL;
        }
        prop->next = NULL;
        debug("!rFLUSH!d %s [%zu]", show_cons(constraint(prop)), priority);
        propqueue[priority].head = propqueue[priority].tail = NULL;
        priority++;
    }

    if (current != NULL)
    {
        debug("!rFLUSH!d %s [%zu]", show_cons(constraint(current)), priority);
        current->next = NULL;
        current = NULL;
    }
}

