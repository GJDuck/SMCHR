/*
 * solver.h
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
#ifndef __SOLVER_H
#define __SOLVER_H

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "log.h"
#include "misc.h"
#include "names.h"
#include "options.h"
#include "pass_rewrite.h"
#include "sat.h"
#include "stats.h"
#include "term.h"
#include "typecheck.h"
#include "word.h"

/****************************************************************************/
/* TRAIL                                                                    */
/****************************************************************************/

/*
 * A trail entry.
 */
struct trailentry_s
{
    word_t *ptr;
    word_t val;
};
typedef struct trailentry_s *trailentry_t;

/*
 * Trail function.
 */
typedef void (*trailfunc_t)(word_t arg);

/*
 * Choice-points.
 */
typedef size_t choicepoint_t;

/*
 * The trail (private).
 */
extern trailentry_t __solver_trail;
extern size_t __solver_trail_len;

/*
 * Get the current choicepoint.
 */
static inline choicepoint_t ALWAYS_INLINE solver_get_choicepoint(void)
{
    return (choicepoint_t)__solver_trail_len;
}
#define choicepoint()       solver_get_choicepoint()

/*
 * Trail a address.
 *
 * trail(ptr)
 *      Save the (word-sized) value pointed to by 'ptr' so that it is
 *      automatically restored on backtracking.
 */
static inline void ALWAYS_INLINE solver_trail(word_t *ptr)
{
    __solver_trail[__solver_trail_len].ptr = ptr;
    __solver_trail[__solver_trail_len].val = *ptr;
    __solver_trail_len++;
}
#define trail(ptr)                                                          \
    do {                                                                    \
        _Static_assert(sizeof(*(ptr)) == sizeof(word_t),                    \
            "trailed value must have size equal to sizeof(word_t)");        \
        solver_trail((word_t *)(ptr));                                      \
    } while(false)

/*
 * Trail a function.
 *
 * trail_func(f, arg)
 *      Save a function 'f' and argument 'arg' that will be invoked on 
 *      backtracking.
 */
extern void solver_trail_func(trailfunc_t f, word_t arg);
#define trail_func(f, arg)  solver_trail_func((f), (arg))

/*
 * Initialize/reset the trail (private)
 */
extern void solver_init_trail(void);
extern void solver_reset_trail(void);

/*
 * Backtrack to a choice-point (private)
 */
extern void solver_backtrack(choicepoint_t cp);

/****************************************************************************/
/* PROPAGATORS                                                              */
/****************************************************************************/

/*
 * A propagator.
 */
typedef struct prop_s *prop_t;
struct prop_s
{
    prop_t next;        // Next entry if queued.a
    word_t state;       // Solver-specific state.
    uint64_t info;      // Propagator info.
};

/*
 * Propagator notification/handler routines.
 */
typedef struct cons_s *cons_t;
typedef uint32_t event_t;
typedef void (*handler_t)(prop_t prop);

/*
 * Propagator information.
 */
struct propinfo_s
{
    uint_t priority;    // Propagator's priority.
    event_t events;     // Propagator's events.
    handler_t handler;  // Propagator's handler.
};
typedef struct propinfo_s *propinfo_t;

/*
 * Propagator list.
 */
typedef struct proplist_s *proplist_t;
struct proplist_s
{
    prop_t prop;        // The propagator.
    proplist_t next;    // Tail.
};

/*
 * Propagator flags.
 */
#define INFO_EVENT_MASK     0x000000000000007Full   // Event mask.
#define INFO_EVENT_SHIFT    0                       // Event shift.
#define FLAG_KILLED         0x0000000000000080ull   // Propagator killed?
#define FLAG_USER_1         0x0000000000000100ull   // User flags:
#define FLAG_USER_2         0x0000000000000200ull
#define FLAG_USER_3         0x0000000000000400ull
#define FLAG_USER_4         0x0000000000000800ull
#define INFO_IDX_MASK       0x00000000000FF000ull   // Info index.
#define INFO_IDX_SHIFT      12
#define INFO_OFFSET_MASK    0x00000000FFF00000ull   // Cons offset.
#define INFO_OFFSET_SHIFT   20

/*
 * Types of events.
 */
#define EVENT_TRUE          0x00000001
#define EVENT_FALSE         0x00000002
#define EVENT_BIND          0x00000004
#define EVENT_CREATE        0x00000008
#define EVENT_NONE          0
#define EVENT_DECIDE        (EVENT_TRUE | EVENT_FALSE)
#define EVENT_ALL           (EVENT_TRUE | EVENT_FALSE | EVENT_BIND)

/*
 * Kill a propagator.
 *
 * kill(prop)
 *      Kill a propagator, undone on backtracking.
 * annihilate(prop)
 *      Kill a propagator, never undone.
 */
static inline bool ALWAYS_INLINE solver_iskilled(prop_t prop)
{
    return ((prop->info & FLAG_KILLED) != 0);
}
static inline void ALWAYS_INLINE solver_kill(prop_t prop)
{
    if (solver_iskilled(prop))
        return;
    trail(&prop->info);
    prop->info |= FLAG_KILLED;
}
static inline void ALWAYS_INLINE solver_annihilate(prop_t prop)
{
    prop->info |= FLAG_KILLED;
}
#define iskilled(prop)      solver_iskilled(prop)
#define kill(prop)          solver_kill(prop)
#define annihilate(prop)    solver_annihilate(prop)

/*
 * Get the propagator's constraint.
 */
static inline cons_t ALWAYS_INLINE solver_constraint(prop_t prop)
{
    uint64_t offset = (prop->info & INFO_OFFSET_MASK) >> INFO_OFFSET_SHIFT;
    word_t *ptr = (word_t *)prop;
    ptr -= offset;
    return (cons_t)ptr;
}
#define constraint(prop)                                                    \
    solver_constraint(prop)

/*
 * Tests if a propagator is already scheduled or not.
 */
static inline bool ALWAYS_INLINE solver_is_prop_scheduled(prop_t prop)
{
    return (prop->next != NULL);
}
#define isscheduled(prop)   solver_is_prop_scheduled(prop)

/*
 * Test which decision events should wake the propagator.
 */
static inline bool ALWAYS_INLINE solver_should_wake(prop_t prop, event_t e)
{
    return ((((prop->info & INFO_EVENT_MASK) >> INFO_EVENT_SHIFT) & e) != 0);
}
#define shouldwake(prop, e) solver_should_wake((prop), (e))

/*
 * Get the propagator's propinfo index (private).
 */
static inline size_t ALWAYS_INLINE solver_propinfo_index(prop_t prop)
{
    return (prop->info & INFO_IDX_MASK) >> INFO_IDX_SHIFT;
}

/*
 * Explicitly schedule a propagator.
 */
extern void solver_schedule_prop(prop_t prop);
#define schedule(prop)      solver_schedule_prop(prop)

/*
 * Wake a propagator (private).
 */
extern bool solver_wake_prop(void);

/*
 * Check if the propagation queue is empty (private).
 */
extern bool solver_is_queue_empty(void);

/*
 * Flush the propagator queue (private).
 */
extern void solver_flush_queue(void);

/*
 * Reset the propagator queue (private).
 */
extern void solver_reset_prop_queue(void);

/*
 * Initialize a propagator (private).
 */
static inline void ALWAYS_INLINE solver_init_prop(prop_t prop, size_t idx,
    event_t e, cons_t c)
{
    prop->next = NULL;
    prop->state = 0;
    size_t offset = (word_t *)prop - (word_t *)c;
    prop->info =
        ((e << INFO_EVENT_SHIFT) & INFO_EVENT_MASK) |
        ((offset << INFO_OFFSET_SHIFT) & INFO_OFFSET_MASK) |
        ((idx << INFO_IDX_SHIFT) & INFO_IDX_MASK);
    if ((e & EVENT_CREATE) != 0)
        solver_schedule_prop(prop);
}

/****************************************************************************/
/* VARIABLES                                                                */
/****************************************************************************/

/*
 * Variable representation (private).
 */
typedef struct svar_s *svar_t;
typedef struct conslist_s *conslist_t;
typedef long long int hash_t __attribute__ ((vector_size (16)));
struct svar_s
{
    struct var_s var;       // Term variable.
    svar_t next;            // Next unified variable.
    int_t lit;              // Justification literal.
    hash_t hash;            // Variable's hash value.
 
    conslist_t cs;          // Constraints that use this variable.
    conslist_t tail;        // Tail of 'cs'.
    size_t cs_len;          // Length of 'cs'.
    bool mark;              // Mark for matching.
    word_t extra[];         // Solver-specific data.
};

/*
 * Make a variable.  Note that this overrides term's make_var.
 */
extern var_t solver_make_var(const char *name);
#ifdef make_var
#undef make_var
#endif
#define make_var(name)          solver_make_var(name)

/*
 * Dereference a variable.
 */
static inline var_t ALWAYS_INLINE solver_deref_var(var_t x0)
{
    svar_t x = (svar_t)x0;
    while (x->next != x)
        x = x->next;
    return (var_t)x;
}
#define deref(x)                solver_deref_var(x)

/*
 * Attach a constraint to a variable (private).
 */
extern void solver_attach_var(var_t x0, cons_t c);

/*
 * Bind two variables (private).
 */
extern void solver_bind_vars(literal_t lit, var_t x, var_t y);

/*
 * Match two variables.
 */
typedef struct reason_s *reason_t;
extern bool solver_match_vars(reason_t reason, var_t x, var_t y);
static inline bool ALWAYS_INLINE solver_match_vars_0(var_t x, var_t y)
{
    x = deref(x);
    y = deref(y);
    return (x == y);
}
#define match_vars(reason, x, y)                                            \
    (option_eq? solver_match_vars((reason), (x), (y)): (x) == (y))
#define match_vars_0(x, y)                                                  \
    solver_match_vars_0((x), (y))

/*
 * Get all constraints that mention the variable (private).
 */
extern conslist_t solver_var_search(var_t x);

/*
 * Allocate/get solver-specific "extra" memory attached to each variable.
 */
extern size_t solver_alloc_extra(size_t size, word_t *_template);
extern word_t *solver_get_extra(var_t x, size_t offset);
#define alloc_extra(size, temp) solver_alloc_extra((size), (temp))
#define extra(x, offset)        solver_get_extra((x), (offset))

/*
 * Debugging (private).
 */
extern void solver_var_verify(var_t x0);

/*
 * Initialize/reset var module (private)
 */
extern void solver_init_var(void);
extern void solver_reset_var(void);

/****************************************************************************/
/* BOOLEAN (SAT) VARIABLES                                                  */
/****************************************************************************/

/*
 * Decisions.
 *
 * Constraints can be true, false, or unknown.
 */
#define FALSE                   SAT_NEG
#define TRUE                    SAT_POS
#define UNKNOWN                 SAT_UNSET

/*
 * Get the decision of a bvar.
 */
#define decision(b)             sat_get_decision(b)

/*
 * Get the level of a bvar.
 */
#define level(b)                literal_getdlevel(b)

/*
 * Get the literal of a bvar.
 */
static inline literal_t ALWAYS_INLINE solver_literal(bvar_t b)
{
    return (decision(b) == FALSE? -b: b);
}
#define literal(b)              solver_literal(b)

/****************************************************************************/
/* LOOKUPS                                                                  */
/****************************************************************************/

/*
 * A lookup is a (-1) terminated array of argument indexes.
 */
typedef int8_t *lookup_t;

/*
 * Make a lookup, e.g. make_lookup(T, T, _, _)
 */
extern lookup_t solver_make_lookup(term_t *args, size_t len);
#define _                       ((term_t)NULL)
#define T                       ((term_t)1)
#define make_lookup(...)                                                    \
    solver_make_lookup((term_t []){__VA_ARGS__},                            \
        VA_ARGS_LENGTH(term_t, ##__VA_ARGS__))
#define make_lookup_a(args, len)                                            \
    solver_make_lookup((args), (len))

/****************************************************************************/
/* SYMBOLS                                                                  */
/****************************************************************************/

/*
 * Symbol types (private).
 */
enum symtype_e
{
    X_CMP_Y,                    // x CMP y
    X_CMP_C,                    // x CMP c
    X_EQ_Y_OP_Z,                // x = y OP z
    X_EQ_Y_OP_C,                // x = y OP c
    DEFAULT                     // User symbol.
};
typedef enum symtype_e symtype_t;

/*
 * Symbol flags.
 */
#define FLAG_COMMUTATIVE        1       // Symbol is commutative.
#define FLAG_SOLVER_TYPESIG     8       // Solver set typesig.

/*
 * Symbols.
 */
#define MAX_PROPINFO            16
#define MAX_LOOKUPS             16
typedef struct sym_s *sym_t;
typedef decision_t (*constructor_t)(sym_t *, term_t *);
typedef struct occ_s *occ_t;
struct sym_s
{
    const char *name;                               // Symbol's name.
    size_t arity;                                   // Symbol's arity.
    constructor_t constr;                           // Symbol's constructor.
    occ_t occs;                                     // Symbol's CHR occs.
    symtype_t type:8;                               // Symbol's type.
    uint32_t flags;                                 // Symbol's flags.
    hash_t hash;                                    // Symbol's hash.
    typesig_t sig;                                  // Symbol's type sig.
    size_t propinfo_len;                            // Length of propinfo.
    struct propinfo_s propinfo[MAX_PROPINFO];       // Propagator info.
    size_t lookups_len;                             // Length of lookups.
    lookup_t lookups[MAX_LOOKUPS];                  // Lookups.
};

/*
 * Make a new symbol.
 */
extern sym_t solver_make_sym(const char *name, size_t arity, bool deflt);
#define make_sym(name, arity, deflt)                                        \
    solver_make_sym((name), (arity), (deflt))

/*
 * Lookup a symbol.
 */
extern sym_t solver_lookup_sym(const char *name, size_t arity);
#define lookup_sym(name, arity) solver_lookup_sym((name), (arity))

/*
 * Register a solver with a constraint.
 *
 * register_solver(sym, priority, e, handler, lookup, ...)
 *      Registers handler as a propagator for sym.  The solver should
 *      also declare any lookups that will be used.
 */
extern void solver_register_solver(sym_t sym, uint_t priority, event_t e,
    handler_t handler, ...);
#define register_solver(sym, priority, e, handler, ...)                     \
    solver_register_solver((sym), (priority), (e), (handler),               \
        ##__VA_ARGS__, NULL)

/*
 * Register a lookup with a symbol.
 */
extern void solver_register_lookup(sym_t sym, lookup_t lookup);
#define register_lookup(sym, l) solver_register_lookup((sym), (l))

/*
 * Register a typesig with a symbol.
 */
extern void solver_register_typesig(sym_t sym, typesig_t sig);
#define register_typesig(sym, sig)                                          \
    solver_register_typesig((sym), (sig))

/*
 * Built-in symbols.
 */
extern sym_t EQ;
extern sym_t EQ_C;
extern sym_t EQ_PLUS;
extern sym_t EQ_PLUS_C;
extern sym_t EQ_MUL;
extern sym_t EQ_MUL_C;
extern sym_t GT;
extern sym_t GT_C;
extern sym_t LB;
extern sym_t EQ_NIL;
extern sym_t EQ_C_NIL;
extern sym_t EQ_ATOM;
extern sym_t EQ_C_ATOM;
extern sym_t EQ_STR;
extern sym_t EQ_C_STR;

/****************************************************************************/
/* EVENTS                                                                   */
/****************************************************************************/

/*
 * Events (see `types of events' defined above).
 */
typedef uint32_t event_t;

/*
 * A decision event (private).
 */
extern void solver_event_decision(cons_t c);

/*
 * A binding event (private).
 */
extern void solver_event_bind(var_t x, var_t y);

/*
 * Delay on an internal solver event.
 */
extern proplist_t solver_delay_user(prop_t prop, proplist_t ps);
#define delay(prop, ps) solver_delay_user((prop), (ps))

/*
 * An internal solver event.
 */
extern void solver_event_user(proplist_t ps);
#define event(ps)       solver_event_user(ps)

/****************************************************************************/
/* CONSTRAINTS                                                              */
/****************************************************************************/

/*
 * Constraint argument names.
 */
#define X               0
#define Y               1
#define Z               2

/*
 * Constraints.
 *
 * Given
 *      b <-> f(...)
 * then
 *      - f = sym
 *      - b = b
 *      - ... = args
 */
struct cons_s
{
    sym_t sym;          // Constraint's symbol (functor).
    bvar_t b;           // Constraint's decision variable.
    uint32_t flags;     // Constraint flags.
    term_t args[];      // Constraint's arguments.
};

/*
 * Constraint flags.
 */
#define FLAG_DELETED    0x00000001          // Constraint is dead/deleted.

/*
 * Constraint constructor.
 */
extern cons_t solver_make_cons(reason_t reason, sym_t sym, term_t *args);
#define make_cons(reason, sym, ...)                                         \
    solver_make_cons((reason), (sym), (term_t[]){__VA_ARGS__})
#define make_cons_a(reason, sym, args)                                      \
    solver_make_cons((reason), (sym), (args))

/*
 * Purge/delete a constraint.
 */
static inline bool ALWAYS_INLINE solver_ispurged(cons_t c)
{
    return ((c->flags & FLAG_DELETED) != 0);
}
extern char *solver_show_cons(cons_t c);        // XXX
extern void solver_store_delete(cons_t c);
static inline void ALWAYS_INLINE solver_purge(cons_t c)
{
    if (solver_ispurged(c))
        return;
    void *ptr = (void *)&c->flags;
    trail((word_t *)ptr);
    c->flags |= FLAG_DELETED;
    solver_store_delete(c);
}
#define ispurged(cons)      solver_ispurged(cons)
#define purge(cons)         solver_purge(cons)

/*
 * Get the constraint's propagator.
 */
static inline prop_t ALWAYS_INLINE solver_get_propagator(cons_t c)
{
    return (prop_t)(c->args + c->sym->arity);
}
#define propagator(c)       solver_get_propagator(c)

/****************************************************************************/
/* REASONS                                                                  */
/****************************************************************************/

/*
 * A 'reason' (a.k.a. 'justification') describes why propagation or failure
 * occurs.  A 'reason' can be thought of as the implication:
 *      ante_1 /\ ... /\ ante_n ---> cons_1 \/ ... \/ cons_m
 * where ante_1, ..., ante_n are the antecedents, and cons_1, ..., cons_m and
 * the consequents.  A reason is a slightly higher-level way of representing
 * a clause.
 */
struct reason_s
{
    uint16_t len;       // Reason length.
    uint16_t size;      // Reason size.
    literal_t *lits;    // Reason literals.
};

/*
 * Construnct a reason.
 *
 * make_reason(ante1, ante2, ...)
 *      Makes a 'reason' on the stack.  The reason may optionally be 
 *      initialised with one or more antecedents.
 */
#define REASON_INIT_SIZE        8
#define solver_make_reason(...)                                             \
    (&(struct reason_s){                                                    \
        VA_ARGS_LENGTH(literal_t, ##__VA_ARGS__),                           \
        VA_ARGS_LENGTH(literal_t, ##__VA_ARGS__) + REASON_INIT_SIZE,        \
        solver_negate_lits((literal_t [                                     \
            VA_ARGS_LENGTH(literal_t, ##__VA_ARGS__) + REASON_INIT_SIZE])   \
                {__VA_ARGS__},                                              \
            VA_ARGS_LENGTH(literal_t, __VA_ARGS__) + REASON_INIT_SIZE)      \
    })
#define make_reason(...)        solver_make_reason(__VA_ARGS__)

static inline literal_t * ALWAYS_INLINE solver_negate_lits(literal_t *lits,
    size_t len)
{
    for (size_t i = 0; i < len; i++)
        lits[i] = -lits[i];
    return lits;
}

/*
 * Re-size a reason_t (private).
 */
extern void solver_grow_reason(reason_t reason);

/*
 * Basic operations (private)
 */
static inline uint16_t ALWAYS_INLINE solver_push_reason(reason_t reason,
    literal_t lit)
{
    if (reason->len >= reason->size)
        solver_grow_reason(reason);
    uint16_t len = reason->len;
    reason->lits[reason->len++] = lit;
    return len;
}
static inline void ALWAYS_INLINE solver_pop_reason(reason_t reason,
    uint16_t len)
{
    reason->len -= len;
}
static inline void ALWAYS_INLINE solver_reset_reason(reason_t reason)
{
    reason->len = 0;
}
static inline void ALWAYS_INLINE solver_append_reason(reason_t reason1,
    reason_t reason2)
{
    for (size_t i = 0; i < reason2->len; i++)
        solver_push_reason(reason1, reason2->lits[i]);      
}

/*
 * Push a literal onto a reason.
 *
 * antecedent(reason, lit), consequent(reason, lit)
 *      Pushes a antecendet/consequent onto a reason.  These can be pushed in
 *      any order.
 * append(reason1, reason2)
 *      Push all literals from reason2 onto reason1.
 */
static inline ssize_t ALWAYS_INLINE solver_push_antecedent(reason_t reason,
    literal_t lit)
{
    return (ssize_t)solver_push_reason(reason, -lit);
}
static inline ssize_t ALWAYS_INLINE solver_push_consequent(reason_t reason,
    literal_t lit)
{
    return (ssize_t)solver_push_reason(reason, lit);
}
#define antecedent(reason, lit)                                             \
    solver_push_antecedent((reason), (lit))
#define consequent(reason, lit)                                             \
    solver_push_consequent((reason), (lit))
#define append(reason1, reason2)                                            \
    solver_append_reason((reason1), (reason2))

/*
 * Reason manipulation.
 *
 * save(reason)
 *      Create a reason save point.
 * restore(reason, val)
 *      Restore to a previous save point.
 * undo(reason, val)
 *      Pop val literals off the reason.
 * reset(reason)
 *      Pop everything off the reason.
 *
 */
static inline ssize_t ALWAYS_INLINE solver_save(reason_t reason)
{
    return (ssize_t)reason->len;
}
static inline void ALWAYS_INLINE solver_restore(reason_t reason, size_t point)
{
    reason->len = (uint16_t)point;
}
#define save(reason)            solver_save(reason)
#define restore(reason, sp)     solver_restore((reason), (sp))
#define undo(reason, n)         solver_pop_reason((reason), (n))
#define reset(reason)           solver_reset_reason(reason)

/*
 * Test if the reason is late or not.
 */
static inline bool ALWAYS_INLINE solver_islate(reason_t reason)
{
    if (reason->len == 0)
        return false;
    for (size_t i = 0; i < reason->len; i++)
    {
        if (decision(reason->lits[i]) == UNKNOWN)
            continue;
        level_t level = literal_getdlevel(reason->lits[i]);
        if (level == sat_level())
            return false;
    }
    return true;
}
#define islate(reason)          solver_islate(reason)

/*
 * Add a clause to the database.
 *
 * propagate(reason), fail(reason), redundant(reason)
 *      Adds a clause (represented by a reason_t) to the clause database.
 *      Currently the semantics are:
 *      - Redundant clauses are ignored (unless specified by redundant());
 *        this includes clauses that are temporarily implied by the current
 *        decisions.  The redundancy test is incomplete, but works well in
 *        practice.
 *      - Empty clauses (i.e. all literals are false) are recorded and invoke
 *        failure and backtracking.
 *      - Singleton clauses (i.e. all literals but one are false) will cause
 *        unit propagation.
 *      - Other clauses are simply added to the database.
 *
 *      NOTE: At least one literal must be from the current decision level,
 *      otherwise execution will be aborted.  This restriction may be lifted
 *      in the future.
 */
static inline void ALWAYS_INLINE solver_add_clause(reason_t reason,
    bool keep, const char *solver, size_t lineno)
{
    sat_add_clause(reason->lits, reason->len, keep, solver, lineno);
}
static inline void ALWAYS_INLINE solver_propagate(reason_t reason,
    const char *solver, size_t lineno)
{
    solver_add_clause(reason, false, solver, lineno);
}
static inline void ALWAYS_INLINE solver_redundant(reason_t reason,
    const char *solver, size_t lineno)
{
    solver_add_clause(reason, true, solver, lineno);
}
#define propagate(reason)                                                   \
    do {                                                                    \
        debug("PROPAGATE %s:%u", __FILE__, __LINE__);                       \
        solver_propagate(reason, __FILE__, __LINE__);                       \
    } while (false)
#define propagate_by(reason, solver, lineno)                                \
    do {                                                                    \
        debug("PROPAGATE %s:%u", __FILE__, __LINE__);                       \
        solver_propagate(reason, solver, lineno);                           \
    } while (false)
#define fail(reason)                                                        \
    do {                                                                    \
        debug("FAIL %s:%u", __FILE__, __LINE__);                            \
        solver_propagate(reason, __FILE__, __LINE__);                       \
        panic("fail failure");                                              \
    } while (false)
#define fail_by(reason, solver, lineno)                                     \
    do {                                                                    \
        debug("FAIL %s:%u", __FILE__, __LINE__);                            \
        solver_propagate(reason, solver, lineno);                           \
        panic("fail failure");                                              \
    } while (false)
#define redundant(reason)                                                   \
    do {                                                                    \
        debug("REDUNDANT %s:%u", __FILE__, __LINE__);                       \
        solver_redundant(reason, __FILE__, __LINE__);                       \
    } while (false)
#define redundant_by(reason, solver, lineno)                                \
    do {                                                                    \
        debug("REDUNDANT %s:%u", __FILE__, __LINE__);                       \
        solver_redundant(reason, solver, lineno);                           \
    } while (false)

/****************************************************************************/
/* STORE                                                                    */
/****************************************************************************/

/*
 * The global store is where all constraints are kept.
 */

#include "hash.h"

/*
 * Constraint list.
 */
typedef struct conslist_s *conslist_t;
struct conslist_s
{
    cons_t cons;            // Constraint.
    conslist_t next;        // Next entry in store.
};

/*
 * Initialize/reset the store (private).
 */
extern void solver_init_store(void);
extern void solver_reset_store(void);

/*
 * Search for constraints (private).
 */
extern conslist_t solver_store_search(hash_t key);

/*
 * Insert a constraint (primary key only) (private).
 */
extern void solver_store_insert_primary(hash_t key, cons_t c);

/*
 * Insert a constraint (private).
 */
extern void solver_store_insert(cons_t c);

/*
 * Delete a constraint (private).
 */
extern void solver_store_delete(cons_t c);

/*
 * Move a constraint (private).
 */
extern void solver_store_move(cons_t c, hash_t key_old, hash_t key_new);

/****************************************************************************/
/* LOOKUPS and ITERATORS                                                    */
/****************************************************************************/

/*
 * bvar patterns (private)
 */
enum bpattern_e
{
    BPATTERN_DONT_CARE = 0,
    BPATTERN_TRUE = TRUE,
    BPATTERN_FALSE = FALSE,
    BPATTERN_UNKNOWN = UNKNOWN,
    BPATTERN_SET = 4,
    BPATTERN_NOT_TRUE = 5,
    BPATTERN_NOT_FALSE = 6
};
typedef enum bpattern_e bpattern_t;

#define SET             BPATTERN_SET
#define NOT_TRUE        BPATTERN_NOT_TRUE
#define NOT_FALSE       BPATTERN_NOT_FALSE

/*
 * USAGE: The general pattern for iterators is the following:
 *
 *  cons_t c;
 *  for (itr_t i = findall(reason, d, f, x, _, y); get(i, &c); next(i))
 *      ...
 *
 *  Here
 *      - the findall() macro constructs an iterator for all constraints
 *        matching the given lookup, in this case f(x, _, y).
 *      - get() sets c to be the next constraint
 *      - next() moves the iterator forward.
 *      - get() has the side affect of appending the equality justification
 *        to 'reason'.
 *
 * Note: Iterators have a nice interface, but a messy implementation.  The
 *       intention is that the itr_s structure will be mostly compiled away,
 *       as will be many of the if-branches.  The resulting code will be
 *       similar to the low-level code a human would write.
 */

struct itr_s
{
    conslist_t cs;              // The constraint list.
    bpattern_t d;               // The pattern for matching c->b
    reason_t reason;            // The reason.
    size_t len0;                // Initial reason length.
    
    // Lookup arguments:
    term_t t0;
    term_t t1;
    term_t t2;
    term_t t3;
    term_t t4;
    term_t t5;
    term_t t6;
    term_t t7;
};
typedef struct itr_s *itr_t;

/*
 * Get constraint/advance iterator.
 */
static inline void ALWAYS_INLINE solver_next(itr_t i)
{
    i->cs = i->cs->next;
}
static inline void ALWAYS_INLINE solver_match_arg(reason_t reason,
    term_t arg0, term_t arg1)
{
    if (type(arg0) != VAR || type(arg1) != VAR)
        return;
    if (!solver_match_vars(reason, var(arg0), var(arg1)))
#ifndef NODEBUG
        panic("index is broken; non-matching arguments %s vs. %s", show(arg0),
            show(arg1));
#else
        ;
#endif
}
static inline bool ALWAYS_INLINE solver_match_bvar(bpattern_t p, bvar_t b)
{
    switch (p)
    {
        case BPATTERN_DONT_CARE:
            return true;
        case BPATTERN_TRUE:
            return (decision(b) == TRUE);
        case BPATTERN_FALSE:
            return (decision(b) == FALSE);
        case BPATTERN_UNKNOWN:
            return (decision(b) == UNKNOWN);
        case BPATTERN_SET:
            return (decision(b) != UNKNOWN);
        case BPATTERN_NOT_TRUE:
            return (decision(b) != TRUE);
        case BPATTERN_NOT_FALSE:
            return (decision(b) != FALSE);
        default:
            return false;
    }
}
static inline bool ALWAYS_INLINE solver_get(itr_t i, cons_t *cptr)
{
    i->reason->len = i->len0;
    while (i->cs != NULL)
    {
        cons_t c = i->cs->cons;
        if (!ispurged(c) && solver_match_bvar(i->d, c->b))
        {
            *cptr = c;
            if (i->t0 != _) solver_match_arg(i->reason, i->t0, c->args[0]);
            if (i->t1 != _) solver_match_arg(i->reason, i->t1, c->args[1]);
            if (i->t2 != _) solver_match_arg(i->reason, i->t2, c->args[2]);
            if (i->t3 != _) solver_match_arg(i->reason, i->t3, c->args[3]);
            if (i->t4 != _) solver_match_arg(i->reason, i->t4, c->args[4]);
            if (i->t5 != _) solver_match_arg(i->reason, i->t5, c->args[5]);
            if (i->t6 != _) solver_match_arg(i->reason, i->t6, c->args[6]);
            if (i->t7 != _) solver_match_arg(i->reason, i->t7, c->args[7]);
            return true;
        }
        i->cs = i->cs->next;
    }
    return false;
}
#define next(itr)               solver_next(itr)
#define get(itr, cptr)          solver_get((itr), (cptr))

/*
 * Find all constraint matching a lookup.
 *
 * findall(reason, decision, sym, arg0, ...)
 *      Returns an iterator matching the given lookup.  The iterator includes
 *      any constraint that matches the lookup through equalities.  These
 *      equality constraints will be reset & appended by get().
 */
#define findall(reason, d, sym, ...)                                        \
    FINDALL_HELPER((reason), (d), (sym), ##__VA_ARGS__, _, _, _, _, _, _,   \
        _, _)
#define FINDALL_HELPER(reason, d, sym, t0, t1, t2, t3, t4, t5, t6, t7, ...) \
    solver_init_itr(&((struct itr_s){NULL, (bpattern_t)(d), (reason),       \
        (reason)->len, (t0), (t1), (t2), (t3), (t4), (t5), (t6), (t7)}),    \
        (sym))
static inline itr_t ALWAYS_INLINE solver_init_itr(itr_t i, sym_t sym)
{
    hash_t key = hash_sym(sym);
    if (i->t0 != _) key = hash_join(0, key, hash_term(i->t0));
    if (i->t1 != _) key = hash_join(1, key, hash_term(i->t1));
    if (i->t2 != _) key = hash_join(2, key, hash_term(i->t2));
    if (i->t3 != _) key = hash_join(3, key, hash_term(i->t3));
    if (i->t4 != _) key = hash_join(4, key, hash_term(i->t4));
    if (i->t5 != _) key = hash_join(5, key, hash_term(i->t5));
    if (i->t6 != _) key = hash_join(6, key, hash_term(i->t6));
    if (i->t7 != _) key = hash_join(7, key, hash_term(i->t7));
    i->cs = solver_store_search(key);
    return i;
}

/*
 * Find a single matching constraint.
 *
 * find(reason, decision, sym, arg0, ...)
 *      Like findall() but returns the first matching constraint.
 */
#define find(reason, d, sym, ...)                                           \
    FIND_HELPER((reason), (d), (sym), ##__VA_ARGS__, _, _, _, _, _, _, _, _)
#define FIND_HELPER(reason, d, sym, t0, t1, t2, t3, t4, t5, t6, t7, ...)    \
    solver_find_first((reason), (bpattern_t)(d), (sym), (t0), (t1), (t2),   \
        (t3), (t4), (t5), (t6), (t7))
static inline cons_t ALWAYS_INLINE solver_find_first(reason_t reason,
    bpattern_t d, sym_t sym, term_t t0, term_t t1, term_t t2, term_t t3,
    term_t t4, term_t t5, term_t t6, term_t t7)
{
    hash_t key = hash_sym(sym);
    if (t0 != _) key = hash_join(0, key, hash_term(t0));
    if (t1 != _) key = hash_join(1, key, hash_term(t1));
    if (t2 != _) key = hash_join(2, key, hash_term(t2));
    if (t3 != _) key = hash_join(3, key, hash_term(t3));
    if (t4 != _) key = hash_join(4, key, hash_term(t4));
    if (t5 != _) key = hash_join(5, key, hash_term(t5));
    if (t6 != _) key = hash_join(6, key, hash_term(t6));
    if (t7 != _) key = hash_join(7, key, hash_term(t7));
    conslist_t cs = solver_store_search(key);
    while (cs != NULL)
    {
        cons_t c = cs->cons;
        if (!solver_ispurged(c) && solver_match_bvar(d, c->b))
        {
            if (t0 != _) solver_match_arg(reason, t0, c->args[0]);
            if (t1 != _) solver_match_arg(reason, t1, c->args[1]);
            if (t2 != _) solver_match_arg(reason, t2, c->args[2]);
            if (t3 != _) solver_match_arg(reason, t3, c->args[3]);
            if (t4 != _) solver_match_arg(reason, t4, c->args[4]);
            if (t5 != _) solver_match_arg(reason, t5, c->args[5]);
            if (t6 != _) solver_match_arg(reason, t6, c->args[6]);
            if (t7 != _) solver_match_arg(reason, t7, c->args[7]);
            return c;
        }
        cs = cs->next;
    }
    return NULL;
}

/****************************************************************************/
/* REFLECTION                                                               */
/****************************************************************************/

/*
 * Show a constraint.
 */
extern char *solver_show_buf_cons(char *start, char *end, cons_t c);
extern char *solver_show_cons(cons_t c);
#define show_buf_cons(start, end, c)                                        \
    solver_show_buf_cons((start), (end), (c))
#define show_cons(c)        solver_show_cons(c)

/*
 * Convert a constraint to a term (private).
 */
extern term_t solver_convert_cons(cons_t c);

/****************************************************************************/
/* REWRITE RULES                                                            */
/****************************************************************************/

/*
 * Register a rewrite rule.
 */
#define rewrite_rule(head, body)                                            \
    do {                                                                    \
        register_rewrite_rule(term_func(make_func(ATOM_REWRITE, (head),     \
            (body))), __FILE__, __LINE__);                                  \
    } while (false)

/****************************************************************************/
/* SOLVERS                                                                  */
/****************************************************************************/

/*
 * Solver init/reset functions.
 */
typedef void (*solver_init_t)(void);
typedef void (*solver_reset_t)(void);

/*
 * Solver.
 */
struct solver_s
{
    solver_init_t init;     // Initialize the solver.
    solver_reset_t reset;   // Reset the solver (optional).
    char *name;             // Solver's name.
};
typedef struct solver_s *solver_t;

/*
 * Solver comparison.
 */
extern int solver_compare(const void *a, const void *b);

/****************************************************************************/
/* DEFAULT SOLVER                                                           */
/****************************************************************************/

/*
 * Attach the default solver to a symbol (private).
 */
extern void solver_default_solver(sym_t sym);
#define default_solver(sym)     solver_default_solver(sym)

/****************************************************************************/
/* GENERAL                                                                  */
/****************************************************************************/

/*
 * Initialize/reset the solver.
 */
extern void solver_init(void);
extern void solver_reset(void);

/*
 * Abort the solver.
 */
extern void solver_abort(void);
#define bail()                                                              \
    do {                                                                    \
        debug("FAIL %s:%u", __FILE__, __LINE__);                            \
        solver_abort();                                                     \
        panic("solver_abort() failed");                                     \
    } while(false)

/*
 * Solver result.
 */
enum result_e
{
    RESULT_UNKNOWN,     // Satisfiability unknown.
    RESULT_UNSAT,       // Unsatisfiable.
    RESULT_ERROR
};
typedef enum result_e result_t;

/*
 * Invoke the solver.
 */
extern result_t solver_solve(literal_t *choices);
static inline term_t solver_result(void)
{
    return sat_result();
}
#define solve(choices)          solver_solve(choices)
#define result()                solver_result()

#endif      /* __SOLVER_H */
