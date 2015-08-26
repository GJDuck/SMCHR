/*
 * sat.c
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
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "gc.h"
#include "log.h"
#include "misc.h"
#include "options.h"
#include "sat.h"
#include "show.h"
#include "solver.h"
#include "term.h"

/*
 * Tuning.
 */
#define SAT_DECAY               128
#define SAT_RESTART             256
#define SAT_RANDOM              1

/*
 * Types.
 */
typedef literal_t index_t;
typedef int level_t;

typedef struct variable_s *variable_t;
typedef struct clause_s *clause_t;
typedef struct watch_s *watch_t;

/*
 * Watches.
 */
struct watch_s
{
    uint32_t size;              // Watch list size.
    uint32_t length;            // Watch list length.
    clause_t clauses[];         // Watch list clauses.
};

/*
 * A clause.
 */
struct clause_s
{
    uint32_t length;            // Clause length, always >= 2
    literal_t lits[];           // Literals.  The two watch literals are
                                // always lits[0] and lits[1].
};

/*
 * SAT state.
 */

// All variables.
variable_t sat_vars;
static size_t sat_vars_length;

// State
static bool sat_solving;        // Are we solving?
static bool sat_empty;          // Was the empty clause asserted?
static clause_t sat_reason;
static literal_t sat_reason_0;

// Trail.
static level_t sat_tlevel;      // Trail level.
level_t sat_dlevel;             // Decision level.
static literal_t *sat_trail;    // Trail.

// Choice points (theory)
static choicepoint_t *sat_choices;
                                // All (theory) choicepoints.

// Clauses.
static clause_t *sat_clauses;   // All clauses.
static size_t sat_clauses_len;  // Length of 'sat_clauses'.
static ssize_t sat_next_clause; // Next clause.

// Order
static index_t *sat_order;      // Variable order.
static index_t sat_next_var;    // Next variable.
static index_t sat_last_var;    // Last variable selected.

// Random number generator.
static uint32_t sat_z;
static uint32_t sat_w;

/*
 * Prototypes.
 */
static watch_t sat_watch_new(void);
static void literal_addwatch(literal_t lit, clause_t clause);
static void sat_watch_delete(watch_t watch, uint32_t idx);
static literal_t sat_backtrack(clause_t reason, clause_t *learnt);
static void sat_unwind(level_t tlevel, level_t blevel);
static void sat_bump_literal(literal_t lit);
static void sat_bump_clause(clause_t clause);
static void sat_decay(void);
static literal_t sat_select_literal(literal_t *lits);
static clause_t sat_init_clause(literal_t *lits, size_t litslen, bool learnt);
static clause_t sat_new_clause(literal_t *lits, size_t litslen);
static void sat_lazy_clause(literal_t *lits, size_t len, bool keep,
    const char *solver, size_t lineno);
static clause_t sat_eager_clause(literal_t *lits, size_t len);
static bool sat_clause_istrue(literal_t *lits, size_t len);
static action_t sat_action(action_t action) __attribute__ ((noinline))
    __attribute__ ((optimize(2)));
static size_t sat_luby(size_t i);

static char *sat_show_buf_literal(char *start, char *end, literal_t lit);
static char *sat_show_literal(literal_t lit);
static char *sat_show_buf_lits(char *start, char *end, literal_t *lits,
    size_t len);
static char *sat_show_lits(literal_t *lits, size_t litslen);
static char *sat_show_clause(clause_t clause);

/*
 * Simple functions.
 */
static inline uint32_t sat_rand(void)
{
    sat_z = 36969 * (sat_z & 0xFFFF) + (sat_z >> 16);
    sat_w = 18000 * (sat_w & 0xFFFF) + (sat_w >> 16);
    return (sat_z << 16) + sat_w;
}
static inline literal_t literal_makeindex(index_t idx)
{
    return (literal_t)(idx+1);
}
static inline literal_t literal_negate(literal_t lit)
{
    return -lit;
}
static inline bool literal_isnil(literal_t lit)
{
    return (lit == LITERAL_NIL);
}
static inline void literal_setmark(literal_t lit, bool mark)
{
    variable_t var = literal_getvar(lit);
    var->mark = mark;
}
static inline bool literal_getmark(literal_t lit)
{
    variable_t var = literal_getvar(lit);
    return var->mark;
}
static inline void literal_setunit(literal_t lit)
{
    variable_t var = literal_getvar(lit);
    var->unit = true;
    var->unit_sign = literal_getsign(lit);
}
static inline bool literal_isunit(literal_t lit)
{
    variable_t var = literal_getvar(lit);
    return var->unit && var->unit_sign == literal_getsign(lit);
}
static inline watch_t literal_getwatch(literal_t lit)
{
    variable_t var = literal_getvar(lit);
    return var->watches[literal_getsign(lit)];
}
static inline void literal_setwatch(literal_t lit, watch_t watch)
{
    variable_t var = literal_getvar(lit);
    var->watches[literal_getsign(lit)] = watch;
}
static inline clause_t literal_getreason(literal_t lit)
{
    variable_t var = literal_getvar(lit);
    return var->reason;
}
static inline void literal_setreason(literal_t lit, clause_t reason)
{
    variable_t var = literal_getvar(lit);
    var->reason = reason;
}
static inline uint32_t literal_getactivity(literal_t lit)
{
    variable_t var = literal_getvar(lit);
    return var->activity;
}
static inline uint32_t literal_getorder(literal_t lit)
{
    variable_t var = literal_getvar(lit);
    return var->order;
}
static inline void sat_setchoice(void)
{
    debug("!rCHOICE!d [dlevel=%zu, choice=%zu]", sat_dlevel, choicepoint());
    sat_choices[sat_dlevel] = choicepoint();
}
static inline choicepoint_t sat_getchoice(void)
{
    return sat_choices[sat_dlevel+1];       // XXX
}

/****************************************************************************/
/* ENGINE                                                                   */
/****************************************************************************/

/*
 * Assert the literal.
 */
static void literal_set(literal_t lit, clause_t reason)
{
    debug("!ySET!d %s !yLEVEL!d %u !yREASON!d !g%s!d",
        sat_show_literal(lit), sat_dlevel,
        (reason == NULL? "(decision)": sat_show_clause(reason)));

    variable_t var = literal_getvar(lit);
    var->sign = literal_getsign(lit);
    var->set = true;
    var->dlevel = sat_dlevel;
    var->reason = reason;
    sat_trail[sat_tlevel] = lit;
    sat_tlevel++;
    if (var->cons != NULL)
    {
        solver_event_decision(var->cons);
        solver_store_insert(var->cons);
    }
}

/*
 * Undo an assertion.
 */
static inline void literal_unset(literal_t lit)
{
    debug("!yUNSET!d %s !yLEVEL!d %u !yREASON!d !g%s!d",
        sat_show_literal(lit), literal_getdlevel(lit),
        (literal_getreason(lit) == NULL? "(decision)":
            sat_show_clause(literal_getreason(lit))));
    
    variable_t var = literal_getvar(lit);
    var->set = false;
    index_t order = var->order;
    if (order < sat_next_var)
        sat_next_var = order;
}

/*
 * Assign lit to be true; and propagate.
 */
static bool sat_propagate(literal_t lit, clause_t reason)
{
    level_t curr, next;

    sat_setchoice();

sat_propagate_restart:

    curr = sat_tlevel;
    next = curr + 1;

    literal_set(lit, reason);

sat_propagate_loop:
    while (curr < next)
    {
        lit = sat_trail[curr];
        curr++;
        debug("!rPROPAGATE!d %s", sat_show_literal(lit));
        lit = literal_negate(lit);
        watch_t watch = literal_getwatch(lit);
        for (int i = 0; i < watch->length; i++)
        {
            clause_t clause = watch->clauses[i];
            bool watch_lit_idx = (clause->lits[0] == lit);
            literal_t watch_lit = clause->lits[watch_lit_idx];
            
            debug("!rWAKE!g [%s] !y%s!d", sat_show_literal(lit),
                sat_show_clause(clause));
            if (literal_istrue(watch_lit))
            {
                debug("!rTRUE!d %s", sat_show_clause(clause));
                // Clause is TRUE.
                continue;
            }

            // Find a non-false literal.
            uint32_t j;
            for (j = 2; j < clause->length &&
                    literal_isfalse(clause->lits[j]); j++)
                ;

            // All other literals are false; use other watch.
            if (j >= clause->length)
            {
                if (literal_isfree(watch_lit))
                {
                    debug("!rIMPLIED!d %s", sat_show_literal(watch_lit));
                    
                    // Implied set:
                    if (watch_lit_idx != 0)
                    {
                        check(lit == clause->lits[0]);
                        clause->lits[0] = watch_lit;
                        clause->lits[1] = lit;
                    }
                    debug_step(DEBUG_PROPAGATE, false, clause->lits,
                        clause->length, NULL, 0);
                    literal_set(watch_lit, clause);
                    next++;
                    continue;
                }

                // All literals in this clause are false; fail.
                debug("!rCONFLICT!d %s", sat_show_clause(clause));
                debug_step(DEBUG_FAIL, false, clause->lits, clause->length,
                    NULL, 0);
                solver_flush_queue();
                lit = sat_backtrack(clause, &reason);
                if (lit == LITERAL_NIL)
                    return false;
                goto sat_propagate_restart;
            }

            // Update to watch the new literal.
            literal_t new_watch_lit = clause->lits[j];
            clause->lits[!watch_lit_idx] = new_watch_lit;
            clause->lits[j]              = lit;
            check(!literal_isfalse(new_watch_lit));
            literal_addwatch(new_watch_lit, clause);
            index_t order = literal_getorder(new_watch_lit);
            sat_watch_delete(watch, i);
            if (order < sat_next_var)
                sat_next_var = order;
            i--;
        }
    }

    // Run the theory solver:
    check(curr == sat_tlevel);
    do
    {
        switch (sat_action(SAT_ACTION_PROPAGATE))
        {
            case SAT_ACTION_PROPAGATE:
                break;
            case SAT_ACTION_FAIL:
                // Theory solver failed.
                debug("!rCONFLICT!d (THEORY)");
                if (sat_empty)
                    return false;
                lit = sat_backtrack(sat_reason, &reason);
                if (lit == LITERAL_NIL)
                    return false;
                goto sat_propagate_restart;
            case SAT_ACTION_RESTART:
                // Theory solver assert singleton clause:

                lit = sat_reason_0;
                goto sat_propagate_restart;
        }
        if (sat_tlevel != curr)
        {
            next = sat_tlevel;
            goto sat_propagate_loop;
        }
    }
    while (!solver_is_queue_empty());

    return true;
}

/*
 * SAT/Theory solver interface.
 */
extern action_t sat_action(action_t action)
{
    /*
     * Note: this function does not work at optimization level 0 (-O0)
     */
    static void *sp = NULL;

    switch (action)
    {
        case SAT_ACTION_PROPAGATE:
            /*
             * A SAT variable has been set, do solver propagation.
             */

            // Save the stack-pointer.
            asm ("movq %%rsp, %0": "=m"(sp));

            // Propagate:
            solver_wake_prop();

            // Success:
            return SAT_ACTION_PROPAGATE;

        case SAT_ACTION_FAIL:
            /*
             * A solver has reported failure.
             */

            // Reset all propagators
            solver_flush_queue();
 
            // Restore the stack-pointer.
            asm ("movq %0, %%rsp": : "m"(sp));

            // We are now "inside" the last call to
            // sat_action(SAT_ACTION_PROPAGATE)

            return SAT_ACTION_FAIL;

        case SAT_ACTION_RESTART:
            /*
             * A solver has asserted a late clause.
             */

            // Reset all propagators
            solver_flush_queue();
            
            // Restore the stack-pointer.
            asm ("movq %0, %%rsp": : "m"(sp));

            return SAT_ACTION_RESTART;
    }

    return SAT_ACTION_PROPAGATE;
}

/*
 * Backtrack+learning after failure.
 */
static literal_t sat_backtrack(clause_t reason, clause_t *nogood_ptr)
{
    stat_backtracks++;

    sat_bump_clause(reason);

    literal_t conflicts[sat_tlevel];
    uint32_t conflicts_len = 0;
    literal_t lit;

    check(reason != NULL);

    // We have failed at the top-level; no work to do.
    if (sat_dlevel == 0)
        return LITERAL_NIL;

    // Mark literals in 'reason':
    uint32_t count = 0;
    for (uint32_t i = 0; i < reason->length; i++)
    {
        lit = reason->lits[i];
        level_t dlevel = literal_getdlevel(lit);
    
        // Ignore unit literals
        if (dlevel == 0)
        {
            debug("!bUNIT*!d %s", sat_show_literal(lit));
            continue;
        }
        literal_setmark(lit, true);
        if (dlevel < sat_dlevel)
        {
            // Previous decision-level literal:
            debug("!bCONFLICT*!d %s !bLEVEL!d %u", sat_show_literal(lit),
                dlevel);
            conflicts[conflicts_len++] = lit;
        }
        else
        {
            check(dlevel == sat_dlevel);
            // Current decision-level literal:
            debug("!bSKIP*!d %s", sat_show_literal(lit));
            count++;
        }
    }

    check(count != 0);

    // Find the UIP and collect the conflicts.
    level_t tlevel = sat_tlevel-1;
    do
    {
        if (tlevel < 0)
            return LITERAL_NIL;
        lit = sat_trail[tlevel--];
        literal_unset(lit);
        if (!literal_getmark(lit))
        {
            debug("!bNOT MARKED!d %s", sat_show_literal(lit));
            continue;
        }
        literal_setmark(lit, false);
        count--;
        if (count <= 0)
            break;
        reason = literal_getreason(lit);
        check(reason != NULL);
        sat_bump_clause(reason);
        for (uint32_t i = 1; i < reason->length; i++)
        {
            lit = reason->lits[i];
            if (literal_getmark(lit))
            {
                debug("!bMARKED!d %s", sat_show_literal(lit));
                continue;
            }
            level_t dlevel = literal_getdlevel(lit);
            if (dlevel == 0)
            {
                debug("!bUNIT!d %s", sat_show_literal(lit));
                continue;
            }
            if (dlevel < sat_dlevel)
            {
                debug("!bCONFLICT!d %s", sat_show_literal(lit));
                conflicts[conflicts_len++] = lit;
            }
            else
            {
                debug("!bSKIP!d %s", sat_show_literal(lit));
                count++;
            }
            literal_setmark(lit, true);
        }
    }
    while (true);

    // Simplify the conflicts & construct the learnt clause:
    literal_t nogood[conflicts_len+1];
    uint32_t nogood_len = 0;
    level_t blevel = 0;
    nogood[nogood_len++] = literal_negate(lit);
    for (uint32_t i = 0; i < conflicts_len; i++)
    {
        lit = conflicts[i];
        if (literal_getreason(lit) != NULL)   // !Decision
        {
            reason = literal_getreason(lit);
            uint32_t k;
            for (k = 1; k < reason->length &&
                    literal_getmark(reason->lits[k]); k++)
                ;
            if (k >= reason->length)
                continue;
        }
        nogood[nogood_len++] = lit;
        level_t dlevel = literal_getdlevel(lit);
        if (blevel < dlevel)
        {
            blevel = dlevel;
            nogood[nogood_len-1] = nogood[1];
            nogood[1] = lit;
        }
    }

    // Unwind the trail:
    while (tlevel >= 0)
    {
        lit = sat_trail[tlevel];
        if (literal_getdlevel(sat_trail[tlevel]) <= blevel)
            break;
        literal_unset(lit);
        tlevel--;
    }
    sat_tlevel = tlevel+1;

    // Clear marks.
    for (uint32_t i = 0; i < conflicts_len; i++)
        literal_setmark(conflicts[i], false);

    debug_step(DEBUG_LEARN, false, nogood, nogood_len, NULL, 0);
    clause_t nogood_clause = sat_init_clause(nogood, nogood_len, true);
    if (nogood_clause != NULL)
    {
        sat_next_clause = sat_clauses_len;
        sat_clauses[sat_clauses_len++] = nogood_clause;
    }
    *nogood_ptr = nogood_clause;
    sat_dlevel = blevel;

    if (sat_empty)
        return LITERAL_NIL;

    solver_backtrack(sat_getchoice());
    debug("!yBACKTRACK!d [dlevel=%zu, tlevel=%zu, choice=%zu] !ySET!d %s",
        sat_dlevel, sat_tlevel, sat_getchoice(), sat_show_literal(nogood[0]));
 
    return nogood[0];
}

/*
 * Unwind the state to the given level.
 * TODO: use?
 */
static void sat_unwind(level_t tlevel, level_t blevel)
{
    // Unwind the trail:
    while (tlevel >= 0)
    {
        literal_t lit = sat_trail[tlevel];
        if (literal_getdlevel(lit) <= blevel)
            break;
        literal_unset(lit);
        tlevel--;
    }
    sat_tlevel = tlevel+1;
    sat_dlevel = blevel;
    solver_backtrack(sat_getchoice());
}

/*
 * Luby sequence.
 */
static size_t sat_luby(size_t i)
{
    if (i <= 1)
        return 1;

    size_t j = (i+1), k;
    for (k = 0; (j & 1) == 0; k++)
        j = j >> 1;
    if (j == 1)
    {
        // k = log2(i+1)
        k--;
        return (1 << k);
    }
    else
    {
        for (; j != 1; k++)
            j = j >> 1;
        // k = floor(log2(i+1))
        return sat_luby(i - (1 << k) + 1);
    }
}

/*
 * Restart the search.
 */
static void sat_restart(void)
{
    debug("!cRESTART!d");
    if (sat_dlevel == 1)
        return;
    level_t tlevel = sat_tlevel-1;
    while (tlevel >= 0)
    {
        literal_t lit = sat_trail[tlevel];
        if (literal_getdlevel(lit) == 0)
            break;
        literal_unset(lit);
        tlevel--;
    }
    assert(tlevel >= 0);
    sat_tlevel = tlevel+1;
    sat_dlevel = 1;
    solver_backtrack(sat_choices[1]);
}

/*
 * Perform a search.
 */
extern bool sat_solve(literal_t *choices)
{
    sat_solving = true;

    // Empty clause?
    if (sat_empty)
        return false;

    // Find and propagate unit clauses (dlevel == 0):
    for (size_t i = 0; i < sat_vars_length; i++)
    {
        for (size_t s = LITERAL_POS; s <= LITERAL_NEG; s++)
        {
            literal_t lit = literal_makeindex((index_t)i);
            lit = (s? -lit: lit);
            if (literal_isunit(lit))
            {
                debug_step(DEBUG_SELECT, false, &lit, 1, NULL, 0);
                if (!sat_propagate(lit, NULL))
                    return false;
            }
        }
    }

    // Solving:
    size_t next_decay = SAT_DECAY;
    size_t next_restart = SAT_RESTART;
    size_t restart_seq = 1;
    for (sat_dlevel = 1; true; sat_dlevel++)
    {
        if (stat_backtracks >= next_restart)
        {
            sat_restart();
            restart_seq++;
            next_restart += (SAT_RESTART * sat_luby(restart_seq));
        }
        literal_t lit = sat_select_literal(choices);
        if (lit == LITERAL_NIL)
        {
            // All variables have been set; and no conflict; SAT
            return true;
        }
        debug_step(DEBUG_SELECT, false, &lit, 1, NULL, 0);
        if (stat_backtracks >= next_decay)
        {
            next_decay += SAT_DECAY;
            sat_decay();
        }
        if (!sat_propagate(lit, NULL))
        {
            // UNSAT
            return false;
        }
    }
}

/*
 * Initialize the SAT solver.
 */
extern void sat_init(void)
{
    check(sizeof(struct variable_s) % sizeof(void *) == 0);

    // SAT solver memory:
    size_t size = 0x3FFFFFFF;
    sat_vars = (variable_t)buffer_alloc(size);
    if (!gc_dynamic_root((void **)&sat_vars, &sat_vars_length,
            sizeof(struct variable_s)))
        panic("failed to set GC dynamic root for SAT variables: %s",
            strerror(errno));
    size = 0x3FFFFFFF;
    sat_trail = buffer_alloc(size);
    size = 0x3FFFFFFF;
    sat_choices = buffer_alloc(size);
    size = 0x3FFFFFFF;
    sat_order = buffer_alloc(size);
    size = 0x3FFFFFFF;
    sat_clauses = buffer_alloc(size);
    sat_reset();
    return;
}

/*
 * Reset the SAT state.
 */
extern void sat_reset(void)
{
    sat_solving     = false;
    sat_vars_length = 0;
    sat_dlevel      = 0;
    sat_tlevel      = 0;
    sat_empty       = false;
    sat_reason      = NULL;
    sat_z           = 0xDEADBEEF;
    sat_w           = 0x12345678;
    sat_next_var    = 0;
    sat_last_var    = -1;
    sat_clauses_len = 0;
    sat_next_clause = -1;

    // Variable 0 is always set.
    bvar_t b0 = sat_make_var(make_var("__TRUE__"), NULL);
    check(b0 == LITERAL_TRUE);
    sat_init_clause(&b0, 1, false);
}

/****************************************************************************/
/* VARIABLES                                                                */
/****************************************************************************/

/*
 * Create a new SAT variable.
 */
extern bvar_t sat_make_var(var_t v, cons_t c)
{
    index_t idx = sat_vars_length;
    sat_vars_length++;
    if (v == NULL)
    {
        size_t size = 16;
        char *name = gc_malloc(size);
        snprintf(name, size, "_SAT_%zu", sat_vars_length);
        v = make_var(name);
    }
    variable_t var = (variable_t)(sat_vars + idx);
    var->set       = false;
    var->sign      = false;
    var->mark      = false;
    var->unit      = false;
    var->unit_sign = false;
    var->lazy      = sat_solving;
    var->dlevel = 0;
    var->reason = NULL;
    var->activity = 0;
    var->order = idx;
    var->watches[0] = sat_watch_new();
    var->watches[1] = sat_watch_new();
    var->var        = v;
    var->cons = c;
    sat_order[idx] = idx;
    return literal_makeindex(idx);
}

/*
 * Get the term var of a SAT variable.
 */
extern var_t sat_get_var(bvar_t b)
{
    index_t idx = literal_getindex(b);
    variable_t var = sat_vars + idx;
    return var->var;
}

/*
 * Get the constraint of a SAT variable.
 */
extern cons_t sat_get_constraint(bvar_t b)
{
    index_t idx = literal_getindex(b);
    variable_t var = sat_vars + idx;
    return var->cons;
}

/****************************************************************************/
/* ORDER                                                                    */
/****************************************************************************/

/*
 * Bump the activity of a literal.
 */
static void sat_bump_literal(literal_t lit)
{
    index_t idx = literal_getindex(lit);
    variable_t var = sat_vars + idx;
    var->activity++;
    index_t order = var->order;
    if (order == 0)
        return;
    variable_t var0 = sat_vars + sat_order[order-1];
    if (var0->activity >= var->activity)
        return;
    var0 = sat_vars + sat_order[0];
    index_t hi;
    if (var0->activity + 1 == var->activity)
    {
        hi = 0;
        goto sat_bump_literal_swap;
    }
    index_t lo = 0;
    hi = order-1;
    while (hi - lo > 1)
    {
        index_t mid = (lo + hi) / 2;
        var0 = sat_vars + sat_order[mid];
        if (var0->activity < var->activity)
            hi = mid;
        else
            lo = mid;
    }

sat_bump_literal_swap:
    var0 = sat_vars + sat_order[hi];
    index_t tmp = sat_order[hi];
    sat_order[hi] = sat_order[order];
    sat_order[order] = tmp;
    var0->order = order;
    var->order  = hi;
    if (hi < sat_next_var)
        sat_next_var = hi;
}

/*
 * Bump the acitivity of a clause.
 */
static void sat_bump_clause(clause_t clause)
{
    for (size_t i = 0; i < clause->length; i++)
    {
        literal_t lit = clause->lits[i];
        sat_bump_literal(lit);
    }

/*
    for (index_t i = 1; i < sat_vars_length; i++)
    {
        literal_t lit_i = literal_makeindex(sat_order[i]);
        literal_t lit_0 = literal_makeindex(sat_order[i-1]);
        variable_t var_i = literal_getvar(lit_i);
        variable_t var_0 = literal_getvar(lit_0);
        check(var_i->activity <= var_0->activity);
        if (literal_isfree(lit_i) && var_i->order < sat_next_var)
        {
            for (size_t s = 0; s < 2; s++)
            {
                watch_t watch = var_i->watches[s];
                for (size_t i = 0; i < watch->length; i++)
                {
                    clause_t clause = watch->clauses[i];
                    bool found = false;
                    for (size_t j = 0; j < clause->length; j++)
                    {
                        literal_t lit = clause->lits[j];
                        if (literal_istrue(lit))
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                        panic("sat_next_var invariant broken; "
                            "sat_next_var=%u, and the order of %s is %u",
                            sat_next_var, sat_show_literal(lit_i),
                            var_i->order);
                }
            }
        }
    }
*/
}

/*
 * Decay all activities.
 */
static void sat_decay(void)
{
    for (index_t i = 0; i < sat_vars_length; i++)
    {
        variable_t var = sat_vars + i;
        var->activity >>= 1;
    }
}

/*
 * Choose the literal?
 */
static literal_t sat_should_select_literal(literal_t lit)
{
    if (lit == LITERAL_NIL)
        return LITERAL_NIL;
    variable_t var = literal_getvar(lit);
    if (var->set)
        return LITERAL_NIL;

    int best_sign = -1, best_score = -1;
    for (int sign = 0; sign <= 1; sign++)
    {
        watch_t watch = var->watches[sign];
        int score = 0;
        for (size_t i = 0; i < watch->length; i++)
        {
            // if (score > 8)
            //     break;
            clause_t clause = watch->clauses[i];
            bool found = false;
            for (size_t j = 0; j < clause->length; j++)
            {
                if (literal_istrue(clause->lits[j]))
                {
                    found = true;
                    break;
                }
            }
            if (!found)
                score++;
        }
        if (score > best_score ||
            (score == best_score && (sat_rand() & 0x1) == 0))
        {
            best_score = score;
            best_sign = sign;
        }
    }
    if (best_score == 0)
    {
        debug("!cSKIP!d %s", sat_show_literal(lit));
        return LITERAL_NIL;
    }
    if (best_sign != 0)
        lit = -lit;
    return lit;
}

/*
 * Select a literal.
 */
static literal_t sat_select_literal(literal_t *lits)
{
    literal_t lit = LITERAL_NIL;

#if 0
    // Random:
    if ((sat_rand() & 0xFF) <= SAT_RANDOM)
    {
        index_t i = sat_rand() % sat_vars_length;
        literal_t lit_i = literal_makeindex(i);
        lit_i = sat_should_select_literal(lit_i);
        if (lit_i != LITERAL_NIL)
        {
            lit = lit_i;
            goto found_literal;
        }
    }
#endif

#if 0 
    // Clause-list:
    for (size_t ttl = 256; ttl != 0 && sat_next_clause >= 0; ttl--)
    {
        clause_t clause = sat_clauses[sat_next_clause];
        sat_next_clause--;
        bool found = true;
        int best_score = 0;
        literal_t lit_1 = LITERAL_NIL;
        for (size_t i = 0; i < clause->length; i++)
        {
            literal_t lit_i = clause->lits[i];
            if (literal_istrue(lit_i))
            {
                found = false;
                break;
            }
            if (literal_isfalse(lit_i))
                continue;
            int score = literal_getactivity(lit_i);
            if (score > best_score)
            {
                lit_1 = lit_i;
                best_score = score;
            }
        }
        if (found)
        {
            lit_1 = sat_should_select_literal(lit_1);
            if (lit_1 != LITERAL_NIL)
            {
                lit = lit_1;
                goto found_literal;
            }
        }
    }
#endif
 
    // VSIDS:
    for (index_t i = sat_next_var; i < sat_vars_length; i++)
    {
        literal_t lit_i = literal_makeindex(sat_order[i]);
        lit_i = sat_should_select_literal(lit_i);
        if (lit_i != LITERAL_NIL)
        {
            lit = lit_i;
            sat_next_var = i+1;
            break;
        }
    }

    // Fall-back (activity == 0)
    if (lit != LITERAL_NIL && literal_getactivity(lit) == 0 &&
        sat_last_var != -1)
    {
        variable_t var = literal_getvar(literal_makeindex(sat_last_var));
        cons_t c = var->cons;
        if (c == NULL)
            goto found_literal;
        sym_t sym = c->sym;
        size_t aty = sym->arity;
        for (size_t i = 0; i < aty; i++)
        {
            term_t arg = c->args[i];
            if (type(arg) != VAR)
                continue;
            var_t x = var(arg);
            conslist_t cs = solver_var_search(x);
            while (cs != NULL)
            {
                cons_t d = cs->cons;
                cs = cs->next;
                if (ispurged(d))
                    continue;
                literal_t lit_d = sat_should_select_literal((literal_t)d->b);
                if (lit_d != LITERAL_NIL)
                {
                    lit = lit_d;
                    sat_next_var--;
                    goto found_literal;
                }
            }
        }
    }

found_literal:

    if (lit != LITERAL_NIL)
    {
        debug("!gSELECT!d %s [activity=%u]", sat_show_literal(lit),
            literal_getactivity(lit));
        stat_decisions++;
        sat_last_var = literal_getindex(lit);
    }

    return lit;
}

/****************************************************************************/
/* CLAUSES                                                                  */
/****************************************************************************/

/*
 * Add a new clause to the clause data-base.
 */
extern void sat_add_clause(literal_t *lits, size_t len, bool keep,
    const char *solver, size_t lineno)
{
    if (sat_solving)
        sat_lazy_clause(lits, len, keep, solver, lineno);
    else
        sat_eager_clause(lits, len);
}

/*
 * Create an eager (initial) clause.
 */
static clause_t sat_eager_clause(literal_t *lits, size_t litslen)
{
    return sat_init_clause(lits, litslen, false);
}

/*
 * Create a new SAT clause (generated during setup).
 */
static clause_t sat_init_clause(literal_t *lits, size_t litslen, bool learnt)
{
    debug("!r%s!d %s", (learnt? "LEARNT": "EAGER"),
        sat_show_lits(lits, litslen));

    switch (litslen)
    {
        case 0:
            sat_empty = true;
            return NULL;
        case 1:
            if (literal_isunit(lits[0]))
                return NULL;
            if (literal_isunit(literal_negate(lits[0])))
            {
                sat_empty = true;
                return NULL;
            }
            literal_setunit(lits[0]);
            return NULL;
        default:
        {
            clause_t clause = sat_new_clause(lits, litslen);
            return clause;
        }
    }
}

/*
 * Create a new SAT clause.
 */
static clause_t sat_new_clause(literal_t *lits, size_t litslen)
{
    clause_t clause = (clause_t)gc_malloc(sizeof(struct clause_s) +
        litslen*sizeof(literal_t));
    clause->length = (uint32_t)litslen;
    for (uint32_t i = 0; i < litslen; i++)
        clause->lits[i] = lits[i];
    literal_addwatch(lits[0], clause);
    literal_addwatch(lits[1], clause);
    return clause;
}

/*
 * Insert a literal into a clause conforming to the watch invariant.
 * The order is: TRUE < UNKNOWN < FALSE
 */
static void sat_clause_insert_literal(literal_t *lits, literal_t lit,
    size_t idx)
{
    switch (idx)
    {
        case 0:
            lits[0] = lit;
            return;
        
        case 1:
        {
            literal_t lit_0 = lits[0];
            if (literal_istrue(lit))
            {
                if (literal_istrue(lit_0) &&
                        literal_getdlevel(lit_0) < literal_getdlevel(lit))
                    lits[1] = lit;
                else
                {
                    lits[1] = lit_0;
                    lits[0] = lit;
                }
            }
            else if (literal_isfree(lit))
            {
                if (literal_istrue(lit_0))
                    lits[1] = lit;
                else
                {
                    lits[1] = lit_0;
                    lits[0] = lit;
                }
            }
            else if (literal_isfalse(lit_0) &&
                    literal_getdlevel(lit) > literal_getdlevel(lit_0))
            {
                lits[1] = lit_0;
                lits[0] = lit;
            }
            else
                lits[1] = lit;
            return;
        }

        default:
        {
            literal_t lit_0 = lits[0], lit_1 = lits[1];
            if (literal_istrue(lit))
            {
                lits[idx] = lit_1;
                if (!literal_istrue(lit_0) ||
                    literal_getdlevel(lit) < literal_getdlevel(lit_0))
                {
                    lits[1] = lit_0;
                    lits[0] = lit;
                }
                else
                    lits[1] = lit;
            }
            else if (literal_isfree(lit))
            {
                lits[idx] = lit_1;
                if (!literal_istrue(lit_0))
                {
                    lits[1] = lit_0;
                    lits[0] = lit;
                }
                else
                    lits[1] = lit;
            }
            else
            {
                if (literal_isfalse(lit_0))
                {
                    if (literal_getdlevel(lit) > literal_getdlevel(lit_0))
                    {
                        lits[idx] = lit_1;
                        lits[1] = lit_0;
                        lits[0] = lit;
                    }
                    else if (literal_isfalse(lit_1) &&
                        literal_getdlevel(lit) > literal_getdlevel(lit_1))
                    {
                        lits[idx] = lit_1;
                        lits[1] = lit;
                    }
                    else
                        lits[idx] = lit;
                }
                else if (literal_isfalse(lit_1) &&
                    literal_getdlevel(lit) > literal_getdlevel(lit_1))
                {
                    lits[idx] = lit_1;
                    lits[1] = lit;
                }
                else
                    lits[idx] = lit;
            }
            return;
        }
    }
}

/*
 * Add a lazily generated clause.
 */
#define DEBUG(...)      debug(__VA_ARGS__)
static void sat_lazy_clause(literal_t *lits, size_t len, bool keep,
    const char *solver, size_t lineno)
{
    debug("NEW %s", sat_show_lits(lits, len));

    check(len != 0);

    literal_t new_lits[len+1];
    size_t j = 0;

    for (size_t i = 0; i < len; i++)
    {
        literal_t lit = lits[i];
        if (literal_isnil(lit))
            continue;
        if (lit == LITERAL_FALSE)
            continue;
        if (literal_getmark(lit))
        {
            bool conflict = false;
            for (size_t k = 0; k < j; k++)
            {
                literal_t lit_k = new_lits[k];
                if (lit == lit_k)
                    break;
                if (lit == literal_negate(lit_k))
                {
                    conflict = true;
                    break;
                }
            }
            if (!conflict)
                // Redundant literal x \/ x -> x
                continue;

            // Redundant x \/ -x -> true
            DEBUG("!rLAZY!d %s (!yTRUE!d [useless])",
                sat_show_lits(lits, len));
            goto unmark_and_return;
        }
        sat_clause_insert_literal(new_lits, lit, j);
        j++;
        literal_setmark(lit, true);
    }

    // Check for unit clauses:
    literal_t lit_0 = new_lits[0];
    if (j == 1)
    {
        if (literal_istrue(lit_0))
            goto unmark_and_return;
        sat_unwind(sat_tlevel-1, 0);
        if (literal_isfalse(lit_0))
        {
            sat_empty = true;
            debug_step(DEBUG_FAIL, true, new_lits, 1, solver, lineno);
            sat_action(SAT_ACTION_FAIL);
        }
        literal_setmark(lit_0, false);
        sat_reason_0 = lit_0;
        debug_step(DEBUG_PROPAGATE, true, new_lits, 1, solver, lineno);
        sat_action(SAT_ACTION_RESTART);
        panic("SAT restart failed");
    }

    // Redundant clause (case 1):
    literal_t lit_1 = new_lits[1];
    if (!keep && literal_istrue(lit_0))
    {
        DEBUG("!rLAZY!d %s (!yREDUNDANT!d [true])",
            sat_show_lits(new_lits, j));
        goto unmark_and_return;
    }

    // Redundant clause (case 2):
    if (!keep && literal_isfree(lit_0) && literal_isfree(lit_1) &&
            sat_clause_istrue(new_lits, j))
    {
        DEBUG("!rLAZY!d %s (!yREDUNDANT DISJUNCTION!d)",
            sat_show_lits(new_lits, j));
        goto unmark_and_return;
    }

    // Check for late clauses:
    if (literal_isfalse(lit_0) && literal_getdlevel(lit_0) != sat_dlevel)
    {
        panic("at (%s:%zu) late fail clause (!y%s!d); expected level %d, got "
            "level %d", solver, lineno, sat_show_lits(new_lits, j), sat_dlevel,
            literal_getdlevel(lit_0));
        goto unmark_and_return;
    }
    if (literal_isfree(lit_0) && literal_isfalse(lit_1) &&
        literal_getdlevel(lit_1) != sat_dlevel)
    {
        panic("at (%s:%zu) late propagation clause (!y%s!d); expected level "
            "%d, got level %d", solver, lineno, sat_show_lits(new_lits, j),
            sat_dlevel, literal_getdlevel(lit_1));
        goto unmark_and_return;
    }

    // Create the clause:
    stat_clauses++;
    clause_t clause = (clause_t)gc_malloc(sizeof(struct clause_s) +
        j*sizeof(literal_t));
    clause->length = j;
    check(clause->length > 1);

    // Clear the marks:
    for (size_t i = 0; i < j; i++)
    {
        literal_t lit = new_lits[i];
        clause->lits[i] = lit;
        literal_setmark(lit, false);
        if (!literal_isfree(lit))
        {
            index_t order = literal_getorder(lit);
            if (order < sat_next_var)
                sat_next_var = order;
        }
    }

    // Create watch literals:
    literal_addwatch(lit_0, clause);
    literal_addwatch(lit_1, clause);

    // Do any required propagation:
    if (literal_isfree(lit_0) && literal_isfalse(lit_1))
    {
        DEBUG("!rLAZY!d %s (!yPROPAGATE!d %s)", sat_show_clause(clause),
            sat_show_literal(lit_0));
        debug_step(DEBUG_PROPAGATE, true, clause->lits, clause->length,
            solver, lineno);
        literal_set(lit_0, clause);
        return;
    }
    if (literal_isfalse(lit_0))
    {
        DEBUG("!rLAZY!d %s (!yFAIL!d)", sat_show_clause(clause));
        debug_step(DEBUG_FAIL, true, clause->lits, clause->length, solver,
            lineno);
        sat_reason = clause;
        sat_action(SAT_ACTION_FAIL);
        panic("SAT failure failed");
    }
    debug_step(DEBUG_PROPAGATE, true, clause->lits, clause->length, solver,
        lineno);
    DEBUG("!rLAZY!d %s (!ySLEEP!d)", sat_show_clause(clause));
    return;

unmark_and_return:
    for (size_t i = 0; i < j; i++)
        literal_setmark(new_lits[i], false);
    return;
}
#undef DEBUG

/*
 * Test if a clause is redundant.
 */
static bool sat_clause_istrue(literal_t *lits, size_t len)
{
    // Search for matching clause:
    for (size_t i = 0; i < len && literal_isfree(lits[i]); i++)
    {
        watch_t watch = literal_getwatch(lits[i]);
        for (size_t j = 0; j < watch->length; j++)
        {
            clause_t clause = watch->clauses[j];

            // Check if every free literal in clause is free in lits:
            size_t k;
            for (k = 0; k < clause->length; k++)
            {
                literal_t lit = clause->lits[k];
                if (lit == lits[i])
                    continue;
                if (literal_isfalse(lit))
                    continue;
                if (literal_istrue(lit))
                    break;
                bool found = false;
                for (size_t l = 0; !found && l < len &&
                        literal_isfree(lits[l]); l++)
                    found = (lit == lits[l]);
                if (!found)
                    break;
            }
            if (k >= clause->length)
            {
                debug("SUBSUMED %s BY %s", sat_show_lits(lits, len),
                    sat_show_clause(clause));
                return true;
            }
        }
    }
    return false;
}

/****************************************************************************/
/* WATCHES                                                                  */
/****************************************************************************/

/*
 * Create a new SAT watch.
 */
static watch_t sat_watch_new(void)
{
    size_t watch_size = 4;
    watch_t watch = (watch_t)gc_malloc(sizeof(struct watch_s) +
        watch_size*sizeof(clause_t));
    watch->size   = watch_size;
    watch->length = 0;
    return watch;
}

/*
 * Make literal watch clause.
 */
static void literal_addwatch(literal_t lit, clause_t clause)
{
    watch_t watch = literal_getwatch(lit);
    if (watch->length+1 >= watch->size)
    {
        watch->size *= 2;
        watch = (watch_t)gc_realloc(watch, sizeof(struct watch_s) +
            watch->size*sizeof(clause_t));
        literal_setwatch(lit, watch);
    }
    watch->clauses[watch->length] = clause;
    watch->length++;
}

/*
 * Remove watch from literal.
 */
static void sat_watch_delete(watch_t watch, uint32_t idx)
{
    watch->length--;
    watch->clauses[idx] = watch->clauses[watch->length];
}

/***************************************************************************/
/* REFLECTION                                                              */
/***************************************************************************/

/*
 * Return the SAT state as a term.
 */
static inline int sat_result_compare(const void *a, const void *b)
{
    term_t ta = *(term_t *)a;
    term_t tb = *(term_t *)b;
    return (int)term_compare(ta, tb);
}
extern term_t sat_result(void)
{
    // Work out the buffer size:
    size_t bufsiz = 0;
    for (index_t i = 0; i < sat_vars_length; i++)
    {
        if (i == 0)
            continue;
        literal_t lit = literal_makeindex(i);
        variable_t var = literal_getvar(lit);
        if (var->lazy && option_verbosity <= 3)
            continue;
        if (!var->set)
            continue;
        cons_t c = var->cons;
        if (c != NULL && ispurged(c))
            continue;
        bufsiz++;
    }

    // Collect all constraints:
    term_t buf[bufsiz];
    size_t k = 0;
    for (size_t i = 0; i < sat_vars_length; i++)
    {
        if (i == 0)
            continue;
        literal_t lit = literal_makeindex(i);
        variable_t var = literal_getvar(lit);
        if (var->lazy && option_verbosity <= 3)
            continue;
        if (!var->set)
            continue;
        cons_t c = var->cons;
        term_t t;
        if (c == NULL)
            t = term_var(var->var);
        else
        {
            if (ispurged(c))
                continue;
            t = solver_convert_cons(c);
        }
        if (var->sign)
            t = term_func(make_func(ATOM_NOT, t));
        buf[k++] = t;
    }
    check(k == bufsiz);

    // Sort the terms:
    qsort(buf, bufsiz, sizeof(term_t), sat_result_compare);

    // Make the final result.
    if (bufsiz == 0)
        return term_boolean(make_boolean(true));
    term_t result = buf[0];
    for (size_t i = 1; i < bufsiz; i++)
        result = term_func(make_func(ATOM_AND, result, buf[i]));

    return result;
}

/*
 * Dump the entire SAT state.
 */
extern void sat_dump(void)
{
    for (size_t i = 0; i < sat_vars_length; i++)
    {
        literal_t lit = literal_makeindex(i);
        variable_t var = literal_getvar(lit);
        cons_t c = var->cons;
        if (var->unit)
        {
            if (var->unit_sign)
                message_0("not ");
            message("!c%s!d /\\ ", var->var->name);
        }
        for (size_t j = 0; j < 2; j++)
        {
            watch_t watch = var->watches[j];
            for (size_t k = 0; k < watch->length; k++)
            {
                clause_t clause = watch->clauses[k];
                if (clause->lits[0] == lit || clause->lits[0] == -lit)
                {
                    message_0("(");
                    for (size_t l = 0; l < clause->length; l++)
                    {
                        literal_t lit = clause->lits[l];
                        variable_t var = literal_getvar(lit);
                        if (literal_getsign(lit))
                            message_0("not ");
                        message_0("!c%s!d", var->var->name);
                        if (l == clause->length-1)
                            message(") /\\");
                        else
                            message_0(" \\/ ");
                    }
                }
            }
        }
        if (c == NULL)
            continue;
        message("!c%s!d <-> !r%s!d /\\", var->var->name, show_cons(c));
    }
}

/*
 * Write a literal.
 */
static char *sat_show_buf_literal(char *start, char *end, literal_t lit)
{
    if (lit == LITERAL_NIL)
    {
        start = show_buf_str(start, end, "nil");
        return start;
    }
    if (literal_getsign(lit))
        start = show_buf_char(start, end, '-');
    variable_t var = literal_getvar(lit);
    start = show_buf_str(start, end, var->var->name);
    cons_t c = var->cons;
    if (c != NULL)
    {
        start = show_buf_str(start, end, " [");
        start = show_buf_cons(start, end, c);
        start = show_buf_char(start, end, ']');
    }
    if (var->set)
    {
        start = show_buf_str(start, end, " <");
        start = show_buf_num(start, end, var->dlevel);
        start = show_buf_char(start, end, '>');
    }
    return start;
}

/*
 * Show a literal.
 */
static char *sat_show_literal(literal_t lit)
{
    char *end = sat_show_buf_literal(NULL, NULL, lit);
    size_t len = end - (char *)NULL;
    char *str = (char *)gc_malloc(len+1);
    sat_show_buf_literal(str, str+len+1, lit);
    return str;
}

/*
 * Write a clause.
 */
static char *sat_show_buf_lits(char *start, char *end, literal_t *lits,
    size_t len)
{
    if (len == 0)
    {
        start = show_buf_str(start, end, "false");
        return start;
    }
    for (size_t i = 0; i < len; i++)
    {
        literal_t lit = lits[i];
        start = sat_show_buf_literal(start, end, lit);
        if (i != len-1)
            start = show_buf_str(start, end, " \\/ ");
    }
    return start;
}

/*
 * Show a clause.
 */
static char *sat_show_lits(literal_t *lits, size_t litslen)
{
    char *end = sat_show_buf_lits(NULL, NULL, lits, litslen);
    size_t len = end - (char *)NULL;
    char *str = (char *)gc_malloc(len+1);
    sat_show_buf_lits(str, str+len+1, lits, litslen);
    return str;
}
static char *sat_show_clause(clause_t clause)
{
    return sat_show_lits(clause->lits, clause->length);
}

