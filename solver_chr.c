/*
 * solver_chr.c
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

#include <stdio.h>

#include "map.h"
#include "parse.h"
#include "solver.h"

#define MAX_ARITY               64
#define MAX_HEADS               16
#define MAX_BODIES              256
#define MAX_GUARDS              256
#define MAX_REGS                256
#define MAX_INSTRS              4096
#define MAX_STACK               1024
#define MAX_CHUNK               8192

#define DEFAULT_PRIORITY        5

/*
 * CHR virtual machine op-codes:
 */
enum opcode_e
{
    OPCODE_GET = 0,
    OPCODE_GET_VAL,
    OPCODE_GET_VAR,
    OPCODE_GET_ID,
    OPCODE_LOOKUP,
    OPCODE_NEXT,
    OPCODE_EQUAL,
    OPCODE_EQUAL_VAL,
    OPCODE_DELETE,
    OPCODE_PROP,
    OPCODE_PROP_EQ,
    OPCODE_DISJUNCT,
    OPCODE_DISJ_EQ,
    OPCODE_PROP_DISJ,
    OPCODE_FAIL,
    OPCODE_RETRY,
    OPCODE_EVAL_PUSH,
    OPCODE_EVAL_PUSH_VAL,
    OPCODE_EVAL_POP,
    OPCODE_EVAL_CMP,
    OPCODE_EVAL_BINOP,
    OPCODE_PRINT,
    OPCODE_INC                  // TODO: Remove this hack!
};
typedef enum opcode_e opcode_t;

static inline size_t ALWAYS_INLINE chr_opcode_len(opcode_t op)
{
    switch (op)
    {
        case OPCODE_FAIL: case OPCODE_PROP_DISJ:
            return 0;
        case OPCODE_GET_VAR: case OPCODE_DELETE: case OPCODE_EVAL_PUSH:
        case OPCODE_EVAL_PUSH_VAL: case OPCODE_EVAL_CMP:
        case OPCODE_EVAL_BINOP: case OPCODE_EVAL_POP: case OPCODE_RETRY:
        case OPCODE_PRINT:
            return 1;
        case OPCODE_EQUAL: case OPCODE_EQUAL_VAL: 
        case OPCODE_GET_VAL: case OPCODE_GET_ID:
            return 2;
        case OPCODE_GET: case OPCODE_LOOKUP: case OPCODE_PROP:
        case OPCODE_PROP_EQ: case OPCODE_DISJUNCT: case OPCODE_DISJ_EQ:
        case OPCODE_INC:
            return 3;
        case OPCODE_NEXT:
            return 4;
        default:
            return 0;
    }
}

enum cmp_e
{
    CMP_EQ,
    CMP_NEQ,
    CMP_LT,
    CMP_GT,
    CMP_LEQ,
    CMP_GEQ,
};
typedef enum cmp_e cmp_t;

enum binop_e
{
    BINOP_ADD,
    BINOP_SUB,
    BINOP_MUL,
    BINOP_DIV,
};
typedef enum binop_e binop_t;

/*
 * An occurrence.
 */
struct occ_s
{
    bool sign;
    word_t *instrs;
    struct occ_s *next;
    const char *file;
    size_t lineno;
};
typedef struct occ_s *occ_t;

/*
 * Context information.
 */
MAP_DECL(reginfo, term_t, size_t, term_compare);
struct context_s
{
    const char *file;
    size_t line;
    size_t reg;
    reginfo_t reginfo;
};
typedef struct context_s *context_t;

/*
 * A constraint.
 */
struct constraint_s
{
    func_t c;       // func_t representation.
    size_t reg;     // Register where constraint is stored.
    bool sign;      // Sign (false = positive).
    bool kill;      // Kill (for simplification).
    bool sched;     // Already scheduled.
    var_t id;       // Constraint's ID (optional).
};
typedef struct constraint_s *constraint_t;

/*
 * Arg specification.
 */
struct spec_s
{
    uint8_t len;
    uint8_t args[];
};
typedef struct spec_s *spec_t;

/*
 * Atoms.
 */
static atom_t ATOM_TRUE;
static atom_t ATOM_FALSE;
static atom_t ATOM_TEST_EQ;
static atom_t ATOM_TEST_NEQ;
static atom_t ATOM_TEST_LT;
static atom_t ATOM_TEST_LEQ;
static atom_t ATOM_TEST_GT;
static atom_t ATOM_TEST_GEQ;
static atom_t ATOM_SET;

static atom_t ATOM_PROP;
static atom_t ATOM_SIMP;
static atom_t ATOM_GUARD;
static atom_t ATOM_KILL;
static atom_t ATOM_ID;
static atom_t ATOM_TYPE;
static atom_t ATOM_OF;
static atom_t ATOM_PRIORITY;
static atom_t ATOM_PRINT;
static atom_t ATOM_INC;

/*
 * Prototypes.
 */
static void chr_init(void);
static void chr_handler(prop_t prop);
static void chr_x_eq_c_handler(prop_t prop);
static void chr_execute(word_t *prog, const char *solver, size_t lineno,
    reason_t reason, cons_t active, word_t *regs);
static hash_t chr_hash(sym_t sym, spec_t spec, word_t *regs);
static void chr_match_args(reason_t reason, spec_t spec, word_t *regs,
    cons_t c);
static cons_t chr_make_cons(reason_t reason, sym_t sym, spec_t spec,
    word_t *regs);
static void chr_print(term_t arg);
static bool chr_ask_eq(reason_t reason, term_t t, term_t u);
static bool chr_ask_x_eq_c(reason_t reason, var_t x, term_t c);
static decision_t chr_tell_eq(reason_t reason, term_t t, term_t u,
    cons_t *c_ptr);
static const char *chr_read_file(FILE *file);
static bool chr_compile_type_inst(context_t cxt, term_t ti,
    typeinst_t *type_ptr);
static bool chr_compile_type_decl(context_t cxt, term_t type);
static bool chr_compile_rule(context_t cxt, term_t remain, term_t kill,
    term_t guard, term_t body);
static bool chr_compile_occ(context_t cxt, size_t idx, constraint_t heads,
    size_t num_heads, constraint_t guards, size_t num_guards,
    constraint_t bodies, size_t num_bodies, bool prop, bool and);
static constraint_t chr_select_partner(constraint_t heads, size_t len);
static bool chr_compile_active(context_t cxt, word_t *instrs, size_t *len_ptr,
    constraint_t active);
static bool chr_compile_partner(context_t cxt, word_t *instrs, size_t *len_ptr,
    constraint_t partner);
static bool chr_compile_guard(context_t cxt, word_t *instrs, size_t *len_ptr,
    constraint_t guard);
static bool chr_compile_expr(context_t cxt, word_t *instrs, size_t *len_ptr,
    term_t expr);
static bool chr_compile_body(context_t cxt, word_t *instrs, size_t *len_ptr,
    bool prop, bool and, constraint_t body);
static spec_t chr_make_spec(size_t len, size_t *regs);
static bool chr_make_reg(context_t cxt, size_t *reg_ptr);
static bool chr_push_instr(context_t cxt, word_t *instrs, size_t *len_ptr,
    opcode_t op, ...);
static bool chr_preprocess(context_t cxt, term_t c, constraint_t cs,
    size_t *len, var_t id, size_t end, bool and, bool sign, bool kill,
    bool guard);
static bool chr_preprocess_cons(context_t cxt, var_t id, bool sign, bool kill,
    bool guard, func_t f, constraint_t c);
static void chr_dump_prog(word_t *prog);

/****************************************************************************/
/* CHR-RUNTIME                                                              */
/****************************************************************************/

/*
 * Solver.
 */
static struct solver_s solver_chr_0 =
{
    chr_init,
    NULL,
    "chr"
};
solver_t solver_chr = &solver_chr_0;

/*
 * Initialize this solver.
 */
static void chr_init(void)
{
    static bool inited = false;
    if (inited)
        return;
    inited = true;

    ATOM_TRUE  = make_atom("true", 0);
    ATOM_FALSE = make_atom("false", 0);
    ATOM_TEST_EQ  = make_atom("$=", 2);
    ATOM_TEST_NEQ = make_atom("$!=", 2);
    ATOM_TEST_GT  = make_atom("$>", 2);
    ATOM_TEST_GEQ = make_atom("$>=", 2);
    ATOM_TEST_LT  = make_atom("$<", 2);
    ATOM_TEST_LEQ = make_atom("$<=", 2);
    ATOM_SET = make_atom(":=", 2);

    ATOM_PROP     = make_atom("==>", 2);
    ATOM_SIMP     = make_atom("<=>", 2);
    ATOM_GUARD    = make_atom("|", 2);
    ATOM_KILL     = make_atom("\\", 2);
    ATOM_ID       = make_atom("#", 2);
    ATOM_TYPE     = make_atom("type", 1);
    ATOM_OF       = make_atom("of", 2);
    ATOM_PRIORITY = make_atom("priority", 2);
    ATOM_PRINT    = make_atom("print", 1);
    ATOM_INC      = make_atom("inc", 3);

    typesig_t sig_bbb = make_typesig(TYPEINST_BOOL, TYPEINST_BOOL,
        TYPEINST_BOOL);
    typesig_t sig_baa = make_typesig(TYPEINST_BOOL, TYPEINST_ANY,
        TYPEINST_ANY);
    typesig_t sig_bnn = make_typesig(TYPEINST_BOOL, TYPEINST_NUM,
        TYPEINST_NUM);
    typesig_t sig_bbn = make_typesig(TYPEINST_BOOL, TYPEINST_BOOL,
        TYPEINST_NUM);
    typeinst_declare(ATOM_TEST_EQ, sig_baa);
    typeinst_declare(ATOM_TEST_NEQ, sig_baa);
    typeinst_declare(ATOM_TEST_GT, sig_baa);
    typeinst_declare(ATOM_TEST_GEQ, sig_baa);
    typeinst_declare(ATOM_TEST_LT, sig_baa);
    typeinst_declare(ATOM_TEST_LEQ, sig_baa);
    typeinst_declare(ATOM_SET, sig_bnn);
    typeinst_declare(ATOM_PROP, sig_bbb);
    typeinst_declare(ATOM_SIMP, sig_bbb);
    typeinst_declare(ATOM_GUARD, sig_bbb);
    typeinst_declare(ATOM_KILL, sig_bbb);
    typeinst_declare(ATOM_ID, sig_bbn);

    register_solver(EQ_C, 1, EVENT_TRUE, chr_x_eq_c_handler);
    register_solver(EQ_C_NIL, 1, EVENT_TRUE, chr_x_eq_c_handler);
    register_solver(EQ_C_ATOM, 1, EVENT_TRUE, chr_x_eq_c_handler);
    register_solver(EQ_C_STR, 1, EVENT_TRUE, chr_x_eq_c_handler);
}

/*
 * (matching) choicepoint.
 */
struct choicepoint_s
{
    uint32_t ip;
    uint32_t sp;
};

static inline size_t ALWAYS_INLINE chr_instr_next(size_t ip, size_t len)
{
    return ip + len + 1;
}
static inline opcode_t ALWAYS_INLINE chr_instr_opcode(word_t *prog, size_t ip)
{
    return (opcode_t)prog[ip];
}
static inline word_t ALWAYS_INLINE chr_instr_arg(word_t *prog, size_t ip,
    size_t idx)
{
    return prog[ip+idx];
}

/*
 * Continuation.
 */
#define CHR_NEXT()                                          \
    do {                                                    \
        ip = ip + chr_opcode_len(op) + 1;                   \
        goto chr_execute_loop;                              \
    } while (false)
#define CHR_RETRY()                                         \
    do {                                                    \
        if (cpp == 0)                                       \
            return;                                         \
        cpp--;                                              \
        ip = choicepoints[cpp].ip;                          \
        restore(reason, choicepoints[cpp].sp);              \
        goto chr_execute_loop;                              \
    } while (false)
#define CHR_RETRY_JUMP(n)                                   \
    do {                                                    \
        if (cpp < (n))                                      \
            return;                                         \
        cpp -= (n);                                         \
        ip = choicepoints[cpp].ip;                          \
        restore(reason, choicepoints[cpp].sp);              \
        goto chr_execute_loop;                              \
    } while (false)

/*
 * Evaluation stack.
 */
#define chr_eval_push(t)                                    \
    do {                                                    \
        stack[sp++] = (t);                                  \
        if (sp > MAX_STACK)                                 \
            panic("CHR solver stack overflow");             \
    } while (false)
#define chr_eval_pop()                                      \
    chr_eval_pop_2(stack, &sp)
static inline term_t ALWAYS_INLINE chr_eval_pop_2(term_t *stack, size_t *sp)
{
    if (*sp == 0)
    {
        error("CHR solver stack underflow");
        bail();
    }
    *sp = *sp - 1;
    return stack[*sp];
}

/*
 * CHR handler.
 */
static void chr_handler(prop_t prop)
{
    cons_t c = constraint(prop);
    decision_t d = decision(c->b);
    if (d == UNKNOWN)
        return;

    debug("!yACTIVE!d %s", show_cons(c));

    occ_t occ = c->sym->occs;
    if (occ == NULL)
        return;

    word_t regs[MAX_REGS];
    regs[0] = (word_t)c;
    for (size_t i = 0; i < c->sym->arity; i++)
        regs[i+1] = (word_t)c->args[i];
    reason_t reason = make_reason((d == TRUE? c->b: -c->b));
    while (occ != NULL)
    {
        if (d != (occ->sign? FALSE: TRUE))
        {
            occ = occ->next;
            continue;
        }
        chr_execute(occ->instrs, occ->file, occ->lineno, reason, c, regs);
        restore(reason, 1);
        if (ispurged(c))
            return;
        occ = occ->next;
    }
}

/*
 * CHR x=c handler.
 */
static void chr_x_eq_c_handler(prop_t prop)
{
    cons_t c = constraint(prop);
    if (decision(c->b) != TRUE)
        return;

    // Wake all CHR constraints attached to `x'.
    var_t x = var(c->args[X]);
    conslist_t cs = solver_var_search(x);
    while (cs != NULL)
    {
        cons_t c = cs->cons;
        cs = cs->next;
        if (ispurged(c))
            continue;
        if (decision(c->b) == UNKNOWN)
            continue;
        sym_t sym = c->sym;
        prop_t props = propagator(c);
        propinfo_t info = sym->propinfo;
        for (size_t i = 0; i < sym->propinfo_len; i++)
        {
            prop_t prop = props+i;
            if (iskilled(prop))
                continue;
            if (isscheduled(prop))
                continue;
            if (info[i].handler == chr_handler)
            {
                schedule(prop);
                break;
            }
        }
    }
}

/*
 * CHR interpreter
 */
static void chr_execute(word_t *prog, const char *solver, size_t lineno,
    reason_t reason, cons_t active, word_t *regs)
{
    debug("CHR EXECUTE");

    struct choicepoint_s choicepoints[MAX_HEADS+1];
    size_t cpp = 0;

    uint32_t ip = 0;
    opcode_t op;

    size_t sp = 0;
    term_t stack[MAX_STACK];

chr_execute_loop:

    op = chr_instr_opcode(prog, ip);

    switch (op)
    {
        case OPCODE_GET:
        {
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 1);
            size_t idx = (size_t)chr_instr_arg(prog, ip, 2);
            size_t r2 = (size_t)chr_instr_arg(prog, ip, 3);
            cons_t c = (cons_t)regs[r1];
            term_t arg = c->args[idx];
            regs[r2] = (word_t)arg;
            CHR_NEXT();
        }
        case OPCODE_GET_VAL:
        {
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 1);
            term_t t1 = (term_t)chr_instr_arg(prog, ip, 2);
            regs[r1] = (word_t)t1;
            CHR_NEXT();
        }
        case OPCODE_GET_VAR:
        {
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 1);
            regs[r1] = term_var(make_var(NULL));
            CHR_NEXT();
        }
        case OPCODE_GET_ID:
        {
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 1);
            size_t r2 = (size_t)chr_instr_arg(prog, ip, 2);
            cons_t c = (cons_t)regs[r1];
            regs[r2] = term_int((int_t)c);
            CHR_NEXT();
        }
        case OPCODE_LOOKUP:
        {
            sym_t sym = (sym_t)chr_instr_arg(prog, ip, 1);
            spec_t spec = (spec_t)chr_instr_arg(prog, ip, 2);
            hash_t key = chr_hash(sym, spec, regs);
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 3);
            regs[r1] = (word_t)solver_store_search(key);
            CHR_NEXT();
        }
        case OPCODE_NEXT:
        {
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 1);
            size_t r2 = (size_t)chr_instr_arg(prog, ip, 2);
            bool sign = (bool)chr_instr_arg(prog, ip, 3);
            spec_t spec = (spec_t)chr_instr_arg(prog, ip, 4);
            conslist_t cs = (conslist_t)regs[r1];
            while (true)
            {
                if (cs == NULL)
                    CHR_RETRY();
                cons_t c = cs->cons;
                cs = cs->next;
                if (ispurged(c))
                    continue;
                if (decision(c->b) != (sign? FALSE: TRUE))
                    continue;
                choicepoints[cpp].ip = ip;
                choicepoints[cpp].sp = save(reason);
                cpp++;
                chr_match_args(reason, spec, regs, c);
                antecedent(reason, (sign? -c->b: c->b));
                debug("!cCHR!d !rMATCH!d %s", show_cons(c));
                regs[r2] = (word_t)c;
                regs[r1] = (word_t)cs;
                CHR_NEXT();
            }
        }
        case OPCODE_EQUAL:
        {
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 1);
            size_t r2 = (size_t)chr_instr_arg(prog, ip, 2);
            term_t t1 = (term_t)regs[r1];
            term_t t2 = (term_t)regs[r2];
            if (!chr_ask_eq(reason, t1, t2))
                CHR_RETRY();
            CHR_NEXT();
        }
        case OPCODE_EQUAL_VAL:
        {
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 1);
            term_t t1 = (term_t)regs[r1];
            term_t t2 = (term_t)chr_instr_arg(prog, ip, 2);
            if (!chr_ask_eq(reason, t1, t2))
                CHR_RETRY();
            CHR_NEXT();
        }
        case OPCODE_DELETE:
        {
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 1);
            cons_t c = (cons_t)regs[r1];
            purge(c);
            CHR_NEXT();
        }
        case OPCODE_PROP:
        {
            bool sign = (bool)chr_instr_arg(prog, ip, 1);
            sym_t sym = (sym_t)chr_instr_arg(prog, ip, 2);
            spec_t spec = (spec_t)chr_instr_arg(prog, ip, 3);
            size_t sp = save(reason);
            cons_t c = chr_make_cons(reason, sym, spec, regs);
            debug("!cCHR!d !gPROPAGATE!d %s", show_cons(c));
            consequent(reason, (sign? -c->b: c->b));
            propagate_by(reason, solver, lineno);
            restore(reason, sp);
            CHR_NEXT();
        }
        case OPCODE_PROP_EQ:
        {
            bool sign = (bool)chr_instr_arg(prog, ip, 1);
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 2);
            size_t r2 = (size_t)chr_instr_arg(prog, ip, 3);
            term_t t1 = (term_t)regs[r1];
            term_t t2 = (term_t)regs[r2];
            cons_t c;
            size_t sp = save(reason);
            decision_t d = chr_tell_eq(reason, t1, t2, &c);
            switch (d)
            {
                case TRUE:
                    if (sign) fail_by(reason, solver, lineno); else CHR_NEXT();
                case FALSE:
                    if (sign) CHR_NEXT(); else fail_by(reason, solver, lineno);
                case UNKNOWN:
                    break;
            }
            consequent(reason, (sign? -c->b: c->b));
            debug("!cCHR!d !gPROPAGATE!d %s", show_cons(c));
            propagate_by(reason, solver, lineno);
            restore(reason, sp);
            CHR_NEXT();
        }
        case OPCODE_DISJUNCT:
        {
            bool sign = (bool)chr_instr_arg(prog, ip, 1);
            sym_t sym = (sym_t)chr_instr_arg(prog, ip, 2);
            spec_t spec = (spec_t)chr_instr_arg(prog, ip, 3);
            cons_t c = chr_make_cons(reason, sym, spec, regs);
            debug("!cCHR!d !gPROPAGATE!d (DISJ) %s", show_cons(c));
            consequent(reason, (sign? -c->b: c->b));
            CHR_NEXT();
        }
        case OPCODE_DISJ_EQ:
        {
            bool sign = (bool)chr_instr_arg(prog, ip, 1);
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 2);
            size_t r2 = (size_t)chr_instr_arg(prog, ip, 3);
            term_t t1 = (term_t)regs[r1];
            term_t t2 = (term_t)regs[r2];
            cons_t c;
            decision_t d = chr_tell_eq(reason, t1, t2, &c);
            switch (d)
            {
                case TRUE:
                    if (sign) CHR_NEXT(); else CHR_RETRY();
                case FALSE:
                    if (sign) CHR_RETRY(); else CHR_NEXT();
                case UNKNOWN:
                    break;
            }
            consequent(reason, (sign? -c->b: c->b));
            debug("!cCHR!d !gPROPAGATE!d (DISJ) %s", show_cons(c));
            CHR_NEXT();
        }
        case OPCODE_PROP_DISJ:
            propagate_by(reason, solver, lineno);
            CHR_NEXT();
        case OPCODE_FAIL:
            fail_by(reason, solver, lineno);
        case OPCODE_RETRY:
        {
            size_t n = (size_t)chr_instr_arg(prog, ip, 1);
            CHR_RETRY_JUMP(n);
        }
        case OPCODE_EVAL_PUSH:
        {
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 1);
            term_t t1 = (term_t)regs[r1];
            chr_eval_push(t1);
            CHR_NEXT();
        }
        case OPCODE_EVAL_PUSH_VAL:
        {
            term_t t1 = (term_t)chr_instr_arg(prog, ip, 1);
            chr_eval_push(t1);
            CHR_NEXT();
        }
        case OPCODE_EVAL_POP:
        {
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 1);
            term_t t1 = chr_eval_pop();
            regs[r1] = (word_t)t1;
            CHR_NEXT();
        }
        case OPCODE_EVAL_CMP:
        {
            cmp_t cop = (cmp_t)chr_instr_arg(prog, ip, 1);
            term_t t2 = chr_eval_pop();
            term_t t1 = chr_eval_pop();
            int_t cmp = term_compare(t1, t2);
            bool result;
            switch (cop)
            {
                case CMP_EQ:
                    result = (cmp == 0);
                    break;
                case CMP_NEQ:
                    result = (cmp != 0);
                    break;
                case CMP_LT:
                    result = (cmp < 0);
                    break;
                case CMP_GT:
                    result = (cmp > 0);
                    break;
                case CMP_LEQ:
                    result = (cmp <= 0);
                    break;
                case CMP_GEQ:
                    result = (cmp >= 0);
                    break;
                default:
                    panic("bad comparison op code (%d)", cop);
            }
            if (result)
                CHR_NEXT();
            else
                CHR_RETRY();
        }
        case OPCODE_EVAL_BINOP:
        {
            binop_t bop = (binop_t)chr_instr_arg(prog, ip, 1);
            term_t t2 = chr_eval_pop();
            term_t t1 = chr_eval_pop();
            if (type(t1) != NUM)
            {
                error("binary op expected integer argument; found `%s'",
                    show(t1));
                bail();
            }
            if (type(t2) != NUM)
            {
                error("binary op expected integer argument; found `%s'",
                    show(t2));
                bail();
            }
            int_t n1 = (int_t)num(t1);
            int_t n2 = (int_t)num(t2);
            int_t n3;
            switch (bop)
            {
                case BINOP_ADD:
                    n3 = n1 + n2;
                    break;
                case BINOP_SUB:
                    n3 = n1 - n2;
                    break;
                case BINOP_MUL:
                    n3 = n1 * n2;
                    break;
                case BINOP_DIV:
                    if (n2 == 0)
                    {
                        error("division by zero");
                        bail();
                    }
                    n3 = n1 / n2;
                    break;
                default:
                    panic("bad binary op code (%d)", bop);
            }
            term_t t3 = term_int(n3);
            chr_eval_push(t3);
            CHR_NEXT();
        }
        case OPCODE_PRINT:
        {
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 1);
            term_t t1 = (term_t)regs[r1];
            chr_print(t1);
            CHR_NEXT();
        }
        case OPCODE_INC:
        {
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 1);
            size_t r2 = (size_t)chr_instr_arg(prog, ip, 2);
            size_t r3 = (size_t)chr_instr_arg(prog, ip, 3);
            term_t t1 = (term_t)regs[r1];
            term_t t2 = (term_t)regs[r2];
            term_t t3 = (term_t)regs[r3];
            if (type(t1) != VAR || type(t2) != NUM)
            {
                error("inc/3 type-inst error; expected var+num, found %s+%s",
                    show(t1), show(t2));
                bail();
            }
            size_t sp = save(reason);
            cons_t c = find(reason, NOT_FALSE, EQ_PLUS_C, _, t1, t2);
            if (c != NULL)
            {
                term_t t4 = c->args[X];
                regs[r3] = (word_t)t4;
            }
            else
            {
                cons_t c = make_cons(reason, EQ_PLUS_C, t3, t1, t2);
                consequent(reason, c->b);
                propagate_by(reason, solver, lineno);
            }
            restore(reason, sp);
            CHR_NEXT();
        }
        default:
            panic("bad CHR op code (%d)", (opcode_t)prog[ip]);
    }
}

/*
 * Calculate lookup key.
 */
static hash_t chr_hash(sym_t sym, spec_t spec, word_t *regs)
{
    hash_t hash = hash_sym(sym);
    for (size_t i = 0; i < spec->len; i++)
    {
        size_t idx = (size_t)spec->args[i];
        if (idx == 0)
            continue;
        term_t arg = (term_t)regs[idx];
        hash = hash_join(i, hash, hash_term(arg));
    }
    return hash;
}

/*
 * Match lookup args.
 */
static void chr_match_args(reason_t reason, spec_t spec, word_t *regs,
    cons_t c)
{
    for (size_t i = 0; i < spec->len; i++)
    {
        size_t idx = (size_t)spec->args[i];
        if (idx == 0)
            continue;
        term_t arg = (term_t)regs[idx];
        solver_match_arg(reason, arg, c->args[i]);
    }
}

/*
 * Construct a constraint.
 */
static cons_t chr_make_cons(reason_t reason, sym_t sym, spec_t spec,
    word_t *regs)
{
    term_t args[sym->arity];
    for (size_t i = 0; i < spec->len; i++)
    {
        size_t idx = (size_t)spec->args[i];
        args[i] = (term_t)regs[idx];
    }
    cons_t c = make_cons_a(reason, sym, args);
    return c;
}

/*
 * Print a message.
 */
static void chr_print(term_t arg)
{
    if (type(arg) == STR)
    {
        str_t str = string(arg);
        message_0(str->chars);
    }
    else
        message_0("%s", show(arg));
}

/*
 * Ask if two terms are equal.
 */
static bool chr_ask_eq(reason_t reason, term_t t, term_t u)
{
    if (t == u)
        return true;
    type_t tt = type(t);
    type_t tu = type(u);

    if (tt == VAR)
    {
        if (tu == VAR)
            return match_vars(reason, var(t), var(u));
        else 
            return chr_ask_x_eq_c(reason, var(t), u);
    }
    else if (tu == VAR)
        return chr_ask_x_eq_c(reason, var(u), t);

    if (tt != tu)
        return false;
    if (tt == STR)
    {
        str_t st = string(t);
        str_t su = string(u);
        if (st->len != su->len)
            return false;
        return (strcmp(st->chars, su->chars) == 0);
    }
    return false;
}

/*
 * Ask if x=c holds.
 */
static bool chr_ask_x_eq_c(reason_t reason, var_t x, term_t c)
{
    cons_t eq_c;
    switch (type(c))
    {
        case NIL:
            eq_c = find(reason, TRUE, EQ_C_NIL, term_var(x), c);
            break;
        case BOOL:
            warning("boolean matching is not-yet-implemented as it requires "
                "special SAT solver support");
            return false;
        case NUM:
            eq_c = find(reason, TRUE, EQ_C, term_var(x), c);
            break;
        case ATOM:
            eq_c = find(reason, TRUE, EQ_C_ATOM, term_var(x), c);
            break;
        case STR:
            eq_c = find(reason, TRUE, EQ_C_STR, term_var(x), c);
            break;
        default:
            return false;
    }

    if (eq_c != NULL)
    {
        antecedent(reason, eq_c->b);
        return true;
    }
    return false;
}

/*
 * Tell an equality constraint.
 */
static decision_t chr_tell_eq(reason_t reason, term_t t, term_t u,
    cons_t *c_ptr)
{
    if (t == u)
        return TRUE;
    type_t tt = type(t);
    type_t tu = type(u);
    switch (tt)
    {
        case VAR:
            switch (tu)
            {
                case VAR:
                {
                    cons_t c = make_cons(reason, EQ, t, u);
                    *c_ptr = c;
                    return UNKNOWN;
                }
                case NUM:
                {
                    cons_t c = make_cons(reason, EQ_C, t, u);
                    *c_ptr = c;
                    return UNKNOWN;
                }
                default:
                    goto chr_tell_eq_nyi;
            }
        case NUM:
            if (tu == VAR)
            {
                cons_t c = make_cons(reason, EQ_C, u, t);
                *c_ptr = c;
                return UNKNOWN;
            }
            return FALSE;
        default:
            if (tu == VAR)
                goto chr_tell_eq_nyi;
            return FALSE;
    }

chr_tell_eq_nyi:
    error("NYI: tell `x = c' with non-num `c'");
    bail();
}

/****************************************************************************/
/* CHR MINI-COMPILER                                                        */
/****************************************************************************/

/*
 * Compile a file.
 */
extern bool chr_compile(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (file == NULL)
    {
        error("failed to open file \"%s\"; %s", filename, strerror(errno));
        return false;
    }
    const char *chunk = chr_read_file(file);
    void *mem = (void *)chunk;
    fclose(file);

    bool result = false;

    struct context_s cxt_0;
    context_t cxt = &cxt_0;
    cxt->file = filename;
    cxt->line = 1;

    opinfo_t opinfo = opinfo_init();
    opinfo = binop_register(opinfo, "==>", XFX, 1200, false, true);
    opinfo = binop_register(opinfo, "<=>", XFX, 1200, false, true);
    opinfo = binop_register(opinfo, "-->", XFX, 1200, false, true);
    opinfo = binop_register(opinfo, "|", XFX, 1150, false, true);
    opinfo = binop_register(opinfo, "\\", XFX, 1150, false, true);
    opinfo = binop_register(opinfo, "$=", XFX, 700,  false, true);
    opinfo = binop_register(opinfo, "$!=", XFX, 700,  false, true);
    opinfo = binop_register(opinfo, "$>", XFX, 700,  false, true);
    opinfo = binop_register(opinfo, "$>=", XFX, 700,  false, true);
    opinfo = binop_register(opinfo, "$<", XFX, 700,  false, true);
    opinfo = binop_register(opinfo, "$<=", XFX, 700,  false, true);
    opinfo = binop_register(opinfo, "$+", YFX, 500,  true,  true);
    opinfo = binop_register(opinfo, "$-", YFX, 500,  false, true);
    opinfo = binop_register(opinfo, "$*", XFY, 400,  true,  false);
    opinfo = binop_register(opinfo, "$/", YFX, 400,  false, false);
    opinfo = binop_register(opinfo, ":=", XFX, 700, false, true);
    opinfo = binop_register(opinfo, "#", XFX, 150, false, true);
    opinfo = unop_register(opinfo, "type", 1300, true);
    opinfo = binop_register(opinfo, "of", XFX, 1300, false, true);
    opinfo = binop_register(opinfo, "priority", XFX, 1200, false, true);

    while (true)
    {
        varset_t vars = varset_init();
        const char *chunk0 = chunk;
        term_t rule = parse_term(cxt->file, &cxt->line, opinfo, chunk,
            &chunk, &vars);
        if (rule == (term_t)NULL)
        {
            if (chunk[0] == '\0')
                break;
            size_t len = 64;
            char pre_err[len+1];
            size_t offset = chunk - chunk0, j = 0;
            for (ssize_t i = (offset > len? offset - len: 0); i < offset; i++)
                pre_err[j++] = (chunk[i] == '\n'? ' ': chunk[i]);
            pre_err[j++] = '\0';
            error("(%s: %zu) failed to parse rule or declaration; error is "
                "\"%s!y%s!d\" <--- here ---> \"!y%.64s!d%s\"", cxt->file,
                cxt->line, (offset > len? "...": ""), pre_err,
                chunk, (strlen(chunk) > len? "...": ""));
            goto chr_compile_exit;
        }
        if (type(rule) != FUNC)
        {
            error("(%s: %zu) expected a rule; found `!y%s!d'", cxt->file,
                cxt->line, show(rule));
            return false;
        }
        func_t f = func(rule);
        if (f->atom != ATOM_SIMP && f->atom != ATOM_PROP &&
            f->atom != ATOM_REWRITE)
        {
            if (!chr_compile_type_decl(cxt, rule))
                return false;
            continue;
        }
        term_t head = f->args[0];
        term_t body = f->args[1];
        if (f->atom == ATOM_REWRITE)
        {
            if (!register_rewrite_rule(rule, cxt->file, cxt->line))
                return false;
            continue;
        }
        term_t guard = (term_t)NULL;
        term_t remain = (term_t)NULL, kill = (term_t)NULL;
        if (type(head) == FUNC && func(head)->atom == ATOM_KILL)
        {
            func_t f = func(head);
            remain = f->args[0];
            kill = f->args[1];
        }
        else if (f->atom == ATOM_PROP)
            remain = head;
        else
            kill = head;
        if (type(body) == FUNC && func(body)->atom == ATOM_GUARD)
        {
            func_t f = func(body);
            guard = f->args[0];
            body = f->args[1];
        }
        if (!chr_compile_rule(cxt, remain, kill, guard, body))
            goto chr_compile_exit;
        typeinfo_t tinfo;
        if (!typecheck(cxt->file, cxt->line, rule, &tinfo))
            goto chr_compile_exit;
    }
    result = true;

chr_compile_exit:
    gc_free(mem);
    return result;
}

/*
 * Read a file as a string.
 */
static const char *chr_read_file(FILE *file)
{
    size_t len = 4096, i = 0;
    char *buf = gc_malloc(len * sizeof(char));
    char c;

    while ((c = getc(file)) != EOF)
    {
        if (i >= len)
        {
            len *= 2;
            buf = gc_realloc(buf, len * sizeof(char));
        }
        buf[i++] = c;
    }

    if (i >= len)
    {
        len++;
        buf = gc_realloc(buf, len * sizeof(char));
    }
    buf[i++] = '\0';
    return buf;
}

/*
 * Compile a type-inst.
 */
static bool chr_compile_type_inst(context_t cxt, term_t ti,
    typeinst_t *type_ptr)
{
    bool is_var = false;
    if (type(ti) == FUNC)
    {
        func_t g = func(ti);
        if (g->atom != ATOM_OF)
            goto typeinst_error;
        ti = g->args[0];
        if (type(ti) != VAR && strcmp(var(ti)->name, "var") != 0)
            goto typeinst_error;
        ti = g->args[1];
        is_var = true;
    }
    const char *name;
    if (type(ti) == NIL)
        name = "nil";
    else if (type(ti) != VAR)
    {
typeinst_error:
        error("(%s: %zu) expected a typeinst-name in type declaration; "
            "found `%s'", cxt->file, cxt->line, show(ti));
        return false;
    }
    else
        name = var(ti)->name;
    typeinst_t type;
    if (strcmp(name, "var") == 0)
    {
        if (is_var)
            goto typeinst_error;
        type = TYPEINST_VAR_ANY;
    }
    else
    {
        type = typeinst_make(name);
        if (is_var)
            type = typeinst_make_var(type);
    }
    *type_ptr = type;
    return true;
}

/*
 * Compile a type declaration.
 */
static bool chr_compile_type_decl(context_t cxt, term_t decl_0)
{
    func_t f = func(decl_0);
    atom_t atom = f->atom;
    if (atom != ATOM_TYPE || type(f->args[0]) != FUNC)
    {
typedecl_error:
        error("(%s: %zu) expected a rule or type declaration; found `!y%s!d'",
            cxt->file, cxt->line, show(decl_0));
        return false;
    }
    term_t decl = f->args[0];
    f = func(decl);
    atom = f->atom;
    uint_t priority = DEFAULT_PRIORITY;
    typeinst_t ret_type = TYPEINST_BOOL;
    if (atom == ATOM_PRIORITY)
    {
        if (type(f->args[0]) != FUNC)
            goto typedecl_error;
        if (type(f->args[1]) != VAR)
        {
priority_error:
            error("(%s: %zu) expected a constraint priority low/medium/high; "
                "found `!y%s!d'", cxt->file, cxt->line, show(f->args[1]));
            return false;
        }
        var_t p = var(f->args[1]);
        if (strcmp(p->name, "low") == 0)
            priority = DEFAULT_PRIORITY+1;
        else if (strcmp(p->name, "medium") == 0)
            priority = DEFAULT_PRIORITY;
        else if (strcmp(p->name, "high") == 0)
            priority = DEFAULT_PRIORITY-1;
        else
            goto priority_error;
        f = func(f->args[0]);
        atom = f->atom;
    }
    else if (atom == ATOM_EQ)
    {
        if (type(f->args[0]) != FUNC)
            goto typedecl_error;
        if (!chr_compile_type_inst(cxt, f->args[1], &ret_type))
            return false;
        f = func(f->args[0]);
        atom = f->atom;
    }
    size_t arity = atom_arity(atom);
    typeinst_t types[arity];
    for (size_t i = 0; i < arity; i++)
        if (!chr_compile_type_inst(cxt, f->args[i], types + i))
            return false;
    sym_t sym = make_sym(atom_name(f->atom), atom_arity(f->atom), true);
    typesig_t sig = typeinst_make_typesig(arity, ret_type, types);
    register_solver(sym, priority, EVENT_ALL, chr_handler); // Set priority.
    register_typesig(sym, sig);
    return true;
}

/*
 * Compile a rule:
 */
static bool chr_compile_rule(context_t cxt, term_t remain, term_t kill,
    term_t guard, term_t body)
{
    struct constraint_s heads[MAX_HEADS];
    size_t num_heads = 0;
    struct constraint_s guards[MAX_GUARDS];
    size_t num_guards = 0;
    struct constraint_s bodies[MAX_BODIES];
    size_t num_bodies = 0;
    bool prop = true;

    if (kill != (term_t)NULL)
    {
        if (!chr_preprocess(cxt, kill, heads, &num_heads, NULL,
                sizeof(heads) / sizeof(struct constraint_s), true, false,
                true, false))
            return false;
        if (num_heads > 0)
            prop = false;
    }
    if (remain != (term_t)NULL)
    {
        if (!chr_preprocess(cxt, remain, heads, &num_heads, NULL,
                sizeof(heads) / sizeof(struct constraint_s), true, false,
                false, false))
            return false;
    }
    if (guard != (term_t)NULL)
    {
        if (!chr_preprocess(cxt, guard, guards, &num_guards, NULL,
                sizeof(guards) / sizeof(struct constraint_s), true, false,
                false, true))
            return false;
    }
    bool and = true;
    if (type(body) == FUNC && func(body)->atom == ATOM_OR)
        and = false;
    if (!chr_preprocess(cxt, body, bodies, &num_bodies, NULL,
            sizeof(bodies) / sizeof(struct constraint_s), and, false, false,
            false))
        return false;

    for (size_t i = 0; i < num_heads; i++)
    {
        if (!chr_compile_occ(cxt, i, heads, num_heads, guards, num_guards,
                bodies, num_bodies, prop, and))
            return false;
    }

    return true;
}

/*
 * Compile an occurrence:
 */
static bool chr_compile_occ(context_t cxt, size_t idx, constraint_t heads,
    size_t num_heads, constraint_t guards, size_t num_guards,
    constraint_t bodies, size_t num_bodies, bool prop, bool and)
{
    word_t instrs[MAX_INSTRS];
    size_t len = 0;

    constraint_t active = heads + idx;
    if (!chr_compile_active(cxt, instrs, &len, active))
        return false;

    constraint_t partner;
    active->sched = true;
    active->reg   = 0;      // Active always in r0.
    ssize_t jump  = -1;
    if (active->id != NULL)
    {
        size_t reg_id;
        if (!chr_make_reg(cxt, &reg_id))
            return false;
        if (!chr_push_instr(cxt, instrs, &len, OPCODE_GET_ID, active->reg,
                reg_id))
            return false;
        cxt->reginfo = reginfo_insert(cxt->reginfo, term_var(active->id),
            reg_id);
    }
    if (active->kill)
        jump = 0;
    for (size_t i = 1;
            (partner = chr_select_partner(heads, num_heads)) != NULL; i++)
    {
        if (partner->kill && jump < 0)
            jump = i;
        if (!chr_compile_partner(cxt, instrs, &len, partner))
            return false;
    }
    for (size_t i = 0; i < num_guards; i++)
    {
        constraint_t guard = guards + i;
        if (!chr_compile_guard(cxt, instrs, &len, guard))
            return false;
    }
    for (size_t i = 0; i < num_heads; i++)
    {
        if (heads[i].kill)
        {
            if (!chr_push_instr(cxt, instrs, &len, OPCODE_DELETE,
                    heads[i].reg))
                return false;
        }
        heads[i].sched = false;
    }
    for (size_t i = 0; i < num_bodies; i++)
    {
        constraint_t body = bodies + i;
        if (!chr_compile_body(cxt, instrs, &len, prop, and, body))
            return false;
    }

    if (!and)
    {
        if (!chr_push_instr(cxt, instrs, &len, OPCODE_PROP_DISJ))
            return false;
    }
    jump = (jump < 0? 1: num_heads - jump);
    if (!chr_push_instr(cxt, instrs, &len, OPCODE_RETRY, jump))
        return false;

    occ_t occ = gc_malloc(sizeof(struct occ_s));
    occ->sign = active->sign;
    occ->instrs = gc_malloc(len * sizeof(word_t));
    occ->next = NULL;
    occ->file = cxt->file;
    occ->lineno = cxt->line;
    memcpy(occ->instrs, instrs, len * sizeof(word_t));
    atom_t atom = active->c->atom;
    const char *name = atom_name(atom);
    size_t arity = atom_arity(atom);
    if (false)
    {
        message("!cOCC!d %s/%zu:", name, arity);
        chr_dump_prog(occ->instrs);
    }

    sym_t sym = make_sym(name, arity, true);
    occ_t occ0 = sym->occs;
    if (occ0 == NULL)
    {
        register_solver(sym, DEFAULT_PRIORITY, EVENT_ALL, chr_handler);
        sym->occs = occ;
    }
    else
    {
        // TODO: better solution?
        occ_t prev = occ0;
        occ0 = occ0->next;
        while (occ0 != NULL)
        {
            prev = occ0;
            occ0 = occ0->next;
        }
        prev->next = occ;
    }
    return true;
}

/*
 * Select a partner.
 */
static constraint_t chr_select_partner(constraint_t heads, size_t len)
{
    // TODO: greedy selection.
    for (size_t i = 0; i < len; i++)
    {
        if (heads[i].sched)
            continue;
        constraint_t head = heads + i;
        heads[i].sched = true;
        return head;
    }
    return NULL;
}

/*
 * Compile the active constraint:
 */
static bool chr_compile_active(context_t cxt, word_t *instrs, size_t *len_ptr,
    constraint_t active)
{
    cxt->reginfo = reginfo_init();
 
    atom_t atom = active->c->atom;
    size_t arity = atom_arity(atom);
    for (size_t i = 0; i < arity; i++)
    {
        size_t reg = i+1, reg0;
        term_t arg = active->c->args[i];
        if (type(arg) == VAR)
        {
            if (reginfo_search(cxt->reginfo, arg, &reg0))
            {
                if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_EQUAL, reg,
                        reg0))
                    return false;
            }
            else
                cxt->reginfo = reginfo_insert(cxt->reginfo, arg, reg);
        }
        else
        {
            if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_EQUAL_VAL,
                    reg, arg))
                return false;
        }
    }
    cxt->reg = arity+1;
    return true;
}

/*
 * Compile a partner constraint:
 */
static bool chr_compile_partner(context_t cxt, word_t *instrs, size_t *len_ptr,
    constraint_t partner)
{
    atom_t atom = partner->c->atom;
    size_t arity = atom_arity(atom);
    size_t regs[arity];
    term_t args[arity];
    memset(regs, 0, sizeof(regs));
    for (size_t i = 0; i < arity; i++)
    {
        term_t arg = partner->c->args[i];
        size_t reg;
        if (reginfo_search(cxt->reginfo, arg, &reg))
        {
            regs[i] = reg;
            args[i] = T;
        }
        else
        {
            if (type(arg) != VAR)
            {
                if (!chr_make_reg(cxt, &reg))
                    return false;
                if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_GET_VAL,
                        reg, arg))
                    return false;
                cxt->reginfo = reginfo_insert(cxt->reginfo, arg, reg);
                regs[i] = reg;
                args[i] = T;
            } 
            else
                args[i] = _;
        }
    }

    lookup_t lookup = make_lookup_a(args, arity);
    sym_t sym = make_sym(atom_name(partner->c->atom), arity, true);
    register_lookup(sym, lookup);
    
    spec_t spec = chr_make_spec(arity, regs);
    size_t reg_itr;
    if (!chr_make_reg(cxt, &reg_itr))
        return false;
    if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_LOOKUP, sym, spec,
            reg_itr))
        return false;
    size_t reg_c;
    if (!chr_make_reg(cxt, &reg_c))
        return false;
    if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_NEXT, reg_itr, reg_c,
            (word_t)partner->sign, spec))
        return false;
    partner->reg = reg_c;
    for (size_t i = 0; i < arity; i++)
    {
        if (regs[i] == 0)
        {
            size_t reg_arg;
            if (!chr_make_reg(cxt, &reg_arg))
                return false;
            if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_GET, reg_c,
                    i, reg_arg))
                return false;
            cxt->reginfo = reginfo_insert(cxt->reginfo, partner->c->args[i],
                reg_arg);
        }
    }

    if (partner->id != NULL)
    {
        term_t id = term_var(partner->id);
        if (reginfo_search(cxt->reginfo, id, NULL))
        {
            error("(%s: %zu) ID `%s' is used more than once in rule head",
                cxt->file, cxt->line, show(id));
            return false;
        }
        size_t reg_id;
        if (!chr_make_reg(cxt, &reg_id))
            return false;
        if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_GET_ID, partner->reg,
                reg_id))
            return false;
        cxt->reginfo = reginfo_insert(cxt->reginfo, id, reg_id);
    }

    return true;
}

/*
 * Compile a guard constraint.
 */
static bool chr_compile_guard(context_t cxt, word_t *instrs, size_t *len_ptr,
    constraint_t guard)
{
    atom_t atom = guard->c->atom;
    size_t arity = atom_arity(atom);

    if (atom == ATOM_SET)
    {
        // Special handling of assignment x := ...
        term_t x = guard->c->args[0];
        if (type(x) != VAR)
        {
            error("(%s: %zu) left-hand-side of assignment `:=' must be "
                "a variable; found `%s'", cxt->file, cxt->line, show(x));
            return false;
        }
        if (!reginfo_search(cxt->reginfo, x, NULL))
        {
            if (!chr_compile_expr(cxt, instrs, len_ptr, guard->c->args[1]))
                return false;
            size_t reg;
            if (!chr_make_reg(cxt, &reg))
                return false;
            if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_EVAL_POP, reg))
                return false;
            cxt->reginfo = reginfo_insert(cxt->reginfo, x, reg);
            return true;
        }
        else
            atom = ATOM_TEST_EQ;
    }

    for (size_t i = 0; i < arity; i++)
    {
        term_t arg = guard->c->args[i];
        if (!chr_compile_expr(cxt, instrs, len_ptr, arg))
            return false;
    }

    if (atom == ATOM_TEST_EQ)
    {
        if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_EVAL_CMP, CMP_EQ))
            return false;
    }
    else if (atom == ATOM_TEST_NEQ)
    {
        if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_EVAL_CMP, CMP_NEQ))
            return false;
    }
    else if (atom == ATOM_TEST_LT)
    {
        if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_EVAL_CMP, CMP_LT))
            return false;
    }
    else if (atom == ATOM_TEST_LEQ)
    {
        if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_EVAL_CMP, CMP_LEQ))
            return false;
    }
    else if (atom == ATOM_TEST_GT)
    {
        if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_EVAL_CMP, CMP_GT))
            return false;
    }
    else if (atom == ATOM_TEST_GEQ)
    {
        if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_EVAL_CMP, CMP_GEQ))
            return false;
    }
    else
    {
        error("(%s: %zu): unrecognized guard predicate `%s/%zu'",
            cxt->file, cxt->line, atom_name(atom), atom_arity(atom));
        return false;
    }
    
    return true;
}

/*
 * Compile a guard expression.
 */
static bool chr_compile_expr(context_t cxt, word_t *instrs, size_t *len_ptr,
    term_t expr)
{
    type_t tt = type(expr);
    switch (tt)
    {
        case VAR:
        {
            size_t reg;
            if (!reginfo_search(cxt->reginfo, expr, &reg))
            {
                error("(%s: %zu): unbound variable `%s' in rule guard",
                    cxt->file, cxt->line, show(expr));
                return false;
            }
            if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_EVAL_PUSH, reg))
                return false;
            return true;
        }
        case BOOL: case ATOM: case NUM: case NIL:
        {
            if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_EVAL_PUSH_VAL,
                    expr))
                return false;
            return true;
        }
        case FUNC:
        {
            func_t f = func(expr);
            atom_t atom = f->atom;
            if (atom_arity(atom) != 2)
            {
                error("(%s: %zu) expected binary operation; found `%s'",
                    cxt->file, cxt->line, show(expr));
                return false;
            }
            if (!chr_compile_expr(cxt, instrs, len_ptr, f->args[0]))
                return false;
            if (!chr_compile_expr(cxt, instrs, len_ptr, f->args[1]))
                return false;
            if (atom == ATOM_ADD)
            {
                if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_EVAL_BINOP,
                        BINOP_ADD))
                    return false;
            }
            else if (atom == ATOM_SUB)
            {
                if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_EVAL_BINOP,
                        BINOP_SUB))
                    return false;
            }
            else if (atom == ATOM_MUL)
            {
                if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_EVAL_BINOP,
                        BINOP_MUL))
                    return false;
            }
            else if (atom == ATOM_DIV)
            {
                if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_EVAL_BINOP,
                        BINOP_DIV))
                    return false;
            }
            else
            {
                error("(%s: %zu): unrecognized guard function `%s/%zu'",
                    cxt->file, cxt->line, atom_name(atom), atom_arity(atom));
                return false;
            }
            return true;
        }
        default:
            error("(%s: %zu): unsupported expression `%s' in rule guard",
                cxt->file, cxt->line, show(expr));
            return false;
    }
}

/*
 * Compile a body constraint.
 */
static bool chr_compile_body(context_t cxt, word_t *instrs, size_t *len_ptr,
    bool prop, bool and, constraint_t body)
{
    atom_t atom = body->c->atom;
    size_t arity = atom_arity(atom);
    size_t regs[arity];
    memset(regs, 0, sizeof(regs));

    if (and)
    {
        if (atom == ATOM_FALSE)
        {
            if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_FAIL))
                return false;
            return true;
        }
        else if (atom == ATOM_TRUE)
            return true;
    }
    else
    {
        if (atom == ATOM_TRUE || atom == ATOM_FALSE)
            panic("NYI: disjunctive rules with true/false");
    }

    for (size_t i = 0; i < arity; i++)
    {
        term_t arg = body->c->args[i];
        size_t reg;
        if (!reginfo_search(cxt->reginfo, arg, &reg))
        {
            if (prop && type(arg) == VAR)
            {
                error("(%s: %zu) propagation rule is not range-restricted; "
                    "variable `%s' does not appear in the rule head",
                    cxt->file, cxt->line, show(arg));
                return false;
            }
            if (!chr_make_reg(cxt, &reg))
                return false;
            if (type(arg) == VAR)
            {
                if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_GET_VAR, reg,
                        arg))
                    return false;
            }
            else if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_GET_VAL, reg,
                    arg))
                return false;
            cxt->reginfo = reginfo_insert(cxt->reginfo, arg, reg);
        }
        regs[i] = reg;
    }
    if (atom == ATOM_NEQ)
    {
        atom = body->c->atom = ATOM_EQ;
        body->sign = true;
    }
    if (atom == ATOM_EQ)
    {
        if (and)
        {
            if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_PROP_EQ,
                    (word_t)body->sign, regs[0], regs[1]))
                return false;
        }
        else
        {
            if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_DISJ_EQ,
                    (word_t)body->sign, regs[0], regs[1]))
                return false;
        }
    }
    else
    {
        if (atom == ATOM_PRINT)
        {
            if (!and)
            {
                error("(%s: %zu) print/1 can only be called from a "
                    "conjunctive context", cxt->file, cxt->line);
                return false;
            }
            if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_PRINT, regs[0]))
                return false;
        }
        else if (atom == ATOM_INC)
        {
            if (!and)
            {
                error("(%s: %zu) inc/3 can only be called from a "
                    "conjunctive context", cxt->file, cxt->line);
            }
            if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_INC, regs[1],
                    regs[2], regs[0]))
                return false;
        }
        else
        {
            spec_t spec = chr_make_spec(arity, regs);
            sym_t sym = make_sym(atom_name(atom), arity, true);
            if (and)
            {
                if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_PROP,
                        (word_t)body->sign, sym, spec))
                    return false;
            }
            else
            {
                if (!chr_push_instr(cxt, instrs, len_ptr, OPCODE_DISJUNCT,
                        (word_t)body->sign, sym, spec))
                    return false;
            }
        }
    }
    return true;
}

/*
 * Create a spec.
 */
static spec_t chr_make_spec(size_t len, size_t *regs)
{
    spec_t spec = (spec_t)gc_malloc(sizeof(struct spec_s) +
        len*sizeof(uint8_t));
    spec->len = len;
    for (size_t i = 0; i < len; i++)
        spec->args[i] = (uint8_t)regs[i];
    return spec;
}

/*
 * Get a new register.
 */
static bool chr_make_reg(context_t cxt, size_t *reg_ptr)
{
    *reg_ptr = cxt->reg;
    if (cxt->reg >= UINT8_MAX)
    {
        error("(%s: %zu) too many registers required; maximum is %zu",
            cxt->file, cxt->line, UINT8_MAX);
        return false;
    }
    cxt->reg++;
    return true;
}

/*
 * Push an instruction onto the buffer.
 */
static bool chr_push_instr(context_t cxt, word_t *instrs, size_t *len_ptr,
    opcode_t op, ...)
{
    size_t len = *len_ptr;
    size_t oplen = chr_opcode_len(op);
    
    if (len + oplen + 1 >= MAX_INSTRS)
    {
        error("(%s: %zu) instruction buffer overflow; maximum is %zu",
            cxt->file, cxt->line, MAX_INSTRS);
        return false;
    }

    instrs[len++] = (word_t)op;
    va_list args;
    va_start(args, op);
    for (size_t i = 0; i < oplen; i++)
    {
        word_t arg = va_arg(args, word_t);
        instrs[len++] = arg;
    }
    va_end(args);
    *len_ptr = len;
    return true;
}

/*
 * Process the constraints in a rule.
 */
static bool chr_preprocess(context_t cxt, term_t c, constraint_t cs,
    size_t *len, var_t id, size_t end, bool and, bool sign, bool kill,
    bool guard)
{
    if (*len >= end)
    {
        error("(%s: %zu) too many conjuncts in rule; maximum is %zu",
            cxt->file, cxt->line, end);
        return false;
    }

    func_t f;

chr_preprocess_restart:
    if (type(c) != FUNC)
    {
        if (type(c) == BOOL)
        {
            if (boolean(c))
                f = make_func(ATOM_TRUE);
            else
                f = make_func(ATOM_FALSE);
        }
        else
        {
            error("(%s: %zu) expected a constraint; found `%s'", cxt->file,
                cxt->line, show(c));
            return false;
        }
    }
    else
        f = func(c);

    if (f->atom == ATOM_ID)
    {
        term_t c0 = f->args[0];
        term_t id0 = f->args[1];
        if (type(id0) != VAR)
        {
            error("(%s: %zu) expected a variable ID; found `%s'", cxt->file,
                cxt->line, show(id0));
            return false;
        }
        if (id != NULL)
        {
            error("(%s: %zu) constraint with multiple IDs; found `%s' and "
                "`%s'", cxt->file, cxt->line, show(id0), show_var(id));
            return false;
        }
        id = var(id0);
        c = c0;

        goto chr_preprocess_restart;
    }

    if ((and  && f->atom == ATOM_AND) ||
        (!and && f->atom == ATOM_OR))
    {
        if (sign)
        {
            error("(%s: %zu) unexpected logical connective inside negation",
                cxt->file, cxt->line);
            return false;
        }
        if (id != NULL)
        {
            error("(%s: %zu) logical connectives cannot have IDs; found `%s'",
                cxt->file, cxt->line, show_var(id));
        }
        term_t arg1 = f->args[0], arg2 = f->args[1];
        if (!chr_preprocess(cxt, arg1, cs, len, id, end, and, false, kill,
                guard))
            return false;
        if (!chr_preprocess(cxt, arg2, cs, len, id, end, and, false, kill,
                guard))
            return false;
    }
    else if (f->atom == ATOM_NOT)
    {
        term_t arg = f->args[0];
        return chr_preprocess(cxt, arg, cs, len, id, end, and, true, kill,
            guard);
    }
    else
    {
        if (!chr_preprocess_cons(cxt, id, sign, kill, guard, f, cs + *len))
            return false;
        *len = *len + 1;
    }
    return true;
}

/*
 * Pre-process a constraint.
 */
static bool chr_preprocess_cons(context_t cxt, var_t id, bool sign, bool kill,
    bool guard, func_t f, constraint_t c)
{
    atom_t atom = f->atom;
    if (atom == ATOM_SIMP || atom == ATOM_PROP || atom == ATOM_GUARD ||
        atom == ATOM_KILL || atom == ATOM_ID)
    {
        error("(%s: %zu) usage of reserved constraint name `%s/%zu'",
            cxt->file, cxt->line, atom_name(atom), atom_arity(atom));
        return false;
    }

    size_t arity = atom_arity(atom);
    if (arity >= MAX_ARITY)
    {
        error("(%s: %zu) too many arguments for constraint %s; "
            "maximum is %zu", cxt->file, cxt->line, show_func(f),
            MAX_ARITY);
        return false;
    }
    for (size_t i = 0; i < arity; i++)
    {
        term_t arg = f->args[i];
        switch (type(arg))
        {
            case VAR: case BOOL: case ATOM: case NUM: case NIL: case STR:
                break;
            case FUNC:
            {
                if (guard)
                    break;
                func_t g = func(arg);
                if (g->atom == ATOM_NEG && type(g->args[0]) == NUM)
                {
                    num_t n = num(g->args[0]);
                    f->args[i] = term_int(-(int_t)n);
                    continue;
                }
                // Fallthrough:
            }
            default:
                error("(%s: %zu) unexpected constraint argument `!y%s!d'; "
                    "unsupported term type (!g%s!d)", cxt->file, cxt->line,
                    show(arg), type_name(type(arg)));
                return false;
        }
    }
    c->c = f;
    c->reg = 0;
    c->sign = sign;
    c->sched = false;
    c->kill = kill;
    c->id = id;

    return true;
}

/****************************************************************************/
/* DEBUGGING                                                                */
/****************************************************************************/

/*
 * Dump a specification.
 */
static void chr_dump_spec(spec_t spec)
{
    message_0("[");
    bool comma = false;
    for (size_t i = 0; i < spec->len; i++)
    {
        size_t reg = (size_t)spec->args[i];
        if (reg == 0)
            continue;
        if (comma)
            message_0(",");
        message_0("r%zu", reg);
        comma = true;
    }
    message_0("]");
}

/*
 * Dump a compiled CHR program.
 */
static void chr_dump_prog(word_t *prog)
{
    size_t ip = 0;
    opcode_t op;

chr_execute_loop:
    op = chr_instr_opcode(prog, ip);
    switch (op)
    {
        case OPCODE_GET:
        {
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 1);
            size_t idx = (size_t)chr_instr_arg(prog, ip, 2);
            size_t r2 = (size_t)chr_instr_arg(prog, ip, 3);
            message("\tget\t\tr%zu, %zu, r%zu", r1, idx, r2);
            CHR_NEXT();
        }
        case OPCODE_GET_VAL:
        {
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 1);
            term_t t1 = (term_t)chr_instr_arg(prog, ip, 2);
            message("\tget_val\t\tr%zu, %s", r1, show(t1));
            CHR_NEXT();
        }
        case OPCODE_GET_VAR:
        {
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 1);
            message("\tget_val\t\tr%zu", r1);
            CHR_NEXT();
        }
        case OPCODE_GET_ID:
        {
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 1);
            size_t r2 = (size_t)chr_instr_arg(prog, ip, 2);
            message("\tget_id\t\tr%zu, r%zu", r1, r2);
            CHR_NEXT();
        }
        case OPCODE_LOOKUP:
        {
            sym_t sym = (sym_t)chr_instr_arg(prog, ip, 1);
            spec_t spec = (spec_t)chr_instr_arg(prog, ip, 2);
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 3);
            message_0("\tlookup\t\t%s/%zu, ", sym->name, sym->arity);
            chr_dump_spec(spec);
            message(", r%zu", r1);
            CHR_NEXT();
        }
        case OPCODE_NEXT:
        {
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 1);
            size_t r2 = (size_t)chr_instr_arg(prog, ip, 2);
            bool sign = (bool)chr_instr_arg(prog, ip, 3);
            spec_t spec = (spec_t)chr_instr_arg(prog, ip, 4);
            message_0("\tnext\t\tr%zu, r%zu, %s, ", r1, r2, (sign? "-": "+"));
            chr_dump_spec(spec);
            message("");
            CHR_NEXT();
        }
        case OPCODE_EQUAL:
        {
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 1);
            size_t r2 = (size_t)chr_instr_arg(prog, ip, 2);
            message("\teq\t\tr%zu, r%zu", r1, r2);
            CHR_NEXT();
        }
        case OPCODE_EQUAL_VAL:
        {
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 1);
            term_t t2 = (term_t)chr_instr_arg(prog, ip, 2);
            message("\teq_val\t\tr%zu, %s", r1, show(t2));
            CHR_NEXT();
        }
        case OPCODE_DELETE:
        {
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 1);
            message("\tdelete\t\tr%zu", r1);
            CHR_NEXT();
        }
        case OPCODE_PROP:
        {
            bool sign = (bool)chr_instr_arg(prog, ip, 1);
            sym_t sym = (sym_t)chr_instr_arg(prog, ip, 2);
            spec_t spec = (spec_t)chr_instr_arg(prog, ip, 3);
            message_0("\tprop\t\t%s, %s/%zu, ", (sign? "-": "+"), sym->name,
                sym->arity);
            chr_dump_spec(spec);
            message("");
            CHR_NEXT();
        }
        case OPCODE_PROP_EQ:
        {
            bool sign = (bool)chr_instr_arg(prog, ip, 1);
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 2);
            size_t r2 = (size_t)chr_instr_arg(prog, ip, 3);
            message("\tprop_eq\t\t%s, r%zu, r%zu", (sign? "-": "+"), r1, r2);
            CHR_NEXT();
        }
        case OPCODE_DISJUNCT:
        {
            bool sign = (bool)chr_instr_arg(prog, ip, 1);
            sym_t sym = (sym_t)chr_instr_arg(prog, ip, 2);
            spec_t spec = (spec_t)chr_instr_arg(prog, ip, 3);
            message_0("\tdisjunct\t%s, %s/%zu, ", (sign? "-": "+"), sym->name,
                sym->arity, spec);
            chr_dump_spec(spec);
            message("");
            CHR_NEXT();
        }
        case OPCODE_DISJ_EQ:
        {
            bool sign = (bool)chr_instr_arg(prog, ip, 1);
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 2);
            size_t r2 = (size_t)chr_instr_arg(prog, ip, 3);
            message("\tdisj_eq\t\t%s, r%zu, r%zu",
                (sign? "-": "+"), r1, r2);
            CHR_NEXT();
        }
        case OPCODE_PROP_DISJ:
        {
            message("\tprop_disj\n");
            CHR_NEXT();
        }
        case OPCODE_FAIL:
        {
            message("\tfail\n\n");
            return;
        }
        case OPCODE_RETRY:
        {
            size_t n = (size_t)chr_instr_arg(prog, ip, 1);
            message("\tretry\t\t%zu\n\n", n);
            return;
        }
        case OPCODE_EVAL_PUSH:
        {
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 1);
            message("\tpush\t\tr%zu", r1);
            CHR_NEXT();
        }
        case OPCODE_EVAL_PUSH_VAL:
        {
            term_t t1 = (term_t)chr_instr_arg(prog, ip, 1);
            message("\tpush_val\t%s", show(t1));
            CHR_NEXT();
        }
        case OPCODE_EVAL_POP:
        {
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 1);
            message("\tpop\t\tr%zu", r1);
            CHR_NEXT();
        }
        case OPCODE_EVAL_CMP:
        {
            const char *cmp = "???";
            cmp_t cop = (cmp_t)chr_instr_arg(prog, ip, 1);
            switch (cop)
            {
                case CMP_EQ:
                    cmp = "=";
                    break;
                case CMP_NEQ:
                    cmp = "!=";
                    break;
                case CMP_LT:
                    cmp = "<";
                    break;
                case CMP_GT:
                    cmp = ">";
                    break;
                case CMP_LEQ:
                    cmp = "<=";
                    break;
                case CMP_GEQ:
                    cmp = ">=";
                    break;
            }
            message("\tcmp\t\t(%s)", cmp);
            CHR_NEXT();
        }
        case OPCODE_EVAL_BINOP:
        {
            const char *binop = "???";
            binop_t bop = (binop_t)chr_instr_arg(prog, ip, 1);
            switch (bop)
            {
                case BINOP_ADD:
                    binop = "+";
                    break;
                case BINOP_SUB:
                    binop = "-";
                    break;
                case BINOP_MUL:
                    binop = "*";
                    break;
                case BINOP_DIV:
                    binop = "/";
                    break;
            }
            message("\tbinop\t\t(%s)", binop);
            CHR_NEXT();
        }
        case OPCODE_PRINT:
        {
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 1);
            message("\tprint\t\tr%zu", r1);
            CHR_NEXT();
        }
        case OPCODE_INC:
        {
            size_t r1 = (size_t)chr_instr_arg(prog, ip, 1);
            size_t r2 = (size_t)chr_instr_arg(prog, ip, 2);
            size_t r3 = (size_t)chr_instr_arg(prog, ip, 3);
            message("\tinc\t\tr%zu, r%zu, r%zu", r1, r2, r3);
            CHR_NEXT();
        }
        default:
            panic("bad op-code (%u)", op);
    }
}

