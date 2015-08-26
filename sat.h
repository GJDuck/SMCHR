/*
 * sat.h
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
#ifndef __SAT_H
#define __SAT_H

#include <stdbool.h>
#include <stdint.h>

#include "term.h"

/****************************************************************************/
/* TYPES                                                                    */
/****************************************************************************/

typedef struct cons_s *cons_t;

enum action_e
{
    SAT_ACTION_PROPAGATE,
    SAT_ACTION_FAIL,
    SAT_ACTION_RESTART
};
typedef enum action_e action_t;

enum decision_e
{
    SAT_UNSET = 1,
    SAT_POS   = 2,
    SAT_NEG   = 3
};
typedef enum decision_e decision_t;

/*
 * SAT variable (private).
 */
typedef struct clause_s *clause_t;
typedef struct watch_s *watch_t;
typedef int level_t;
struct variable_s
{
    bool set:1;                 // Is variable set?
    bool sign:1;                // If set, what sign?
    bool mark:1;                // Is variable marked?
    bool unit:1;                // Is variable a unit?
    bool unit_sign:1;           // If unit, what sign?
    bool lazy:1;                // Is variable lazily generated?
    level_t dlevel;             // Decision level.
    clause_t reason;            // Reason for non-decisions.
    watch_t watches[2];         // Watch lists.
    uint32_t activity;          // Activity.
    uint32_t order;             // Order index.
    cons_t cons;                // Constraint (NULL if none).
    const char *name;           // Name (NULL if none).
    var_t var;                  // Var (for reflection).
};
typedef struct variable_s *variable_t;

/*
 * All SAT variables (private).
 */
extern variable_t sat_vars;

/****************************************************************************/
/* LITERALS                                                                 */
/****************************************************************************/

typedef int32_t literal_t;
typedef literal_t index_t;

#define LITERAL_POS             0
#define LITERAL_NEG             1
#define LITERAL_NIL             0
#define LITERAL_TRUE            1
#define LITERAL_FALSE           (-1)

static inline index_t ALWAYS_INLINE literal_getindex(literal_t lit)
{
    return (index_t)((lit < 0? -lit: lit)-1);
}
static inline variable_t literal_getvar(literal_t lit)
{
    index_t idx = literal_getindex(lit);
    return sat_vars + idx;
}
static inline level_t literal_getdlevel(literal_t lit)
{
    variable_t var = literal_getvar(lit);
    return var->dlevel;
}
static inline bool literal_getsign(literal_t lit)
{
    return (lit < 0);
}
static inline bool literal_istrue(literal_t lit)
{
    variable_t var = literal_getvar(lit);
    return (var->set && var->sign == literal_getsign(lit));
}
static inline bool literal_isfalse(literal_t lit)
{
    variable_t var = literal_getvar(lit);
    return (var->set && var->sign == !literal_getsign(lit));
}
static inline bool literal_isfree(literal_t lit)
{
    variable_t var = literal_getvar(lit);
    return !var->set;
}

/****************************************************************************/
/* VARIABLES                                                                */
/****************************************************************************/

typedef literal_t bvar_t;

extern bvar_t sat_make_var(var_t v, cons_t c);
extern var_t sat_get_var(bvar_t b);

static inline decision_t ALWAYS_INLINE sat_get_decision(bvar_t b)
{
    index_t idx = literal_getindex(b);
    variable_t var = (variable_t)(sat_vars + idx);
    if (!var->set)
        return SAT_UNSET;
    return (var->sign? SAT_NEG: SAT_POS);
}
extern cons_t sat_get_constraint(bvar_t b);

/****************************************************************************/
/* CLAUSES                                                                  */
/****************************************************************************/

extern void sat_add_clause(literal_t *lits, size_t size, bool keep,
    const char *solver, size_t lineno);

/****************************************************************************/
/* LEVELS                                                                   */
/****************************************************************************/

extern level_t sat_dlevel;

static inline level_t ALWAYS_INLINE sat_level(void)
{
    return sat_dlevel;
}

/****************************************************************************/
/* MISC                                                                     */
/****************************************************************************/

extern void sat_init(void);
extern void sat_reset(void);

extern bool sat_solve(literal_t *choices);

extern term_t sat_result(void);
extern void sat_dump(void);

#endif      /* __SAT_H */
