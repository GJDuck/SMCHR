/*
 * solver_heaps.c
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
 * Variable aliases.
 */
#define H       0
#define P       1
#define V       2
#define H1      1
#define H2      2

/*
 * Symbols.
 */
sym_t IN;
sym_t EMP;
sym_t ONE;
sym_t SEP;
sym_t EQUAL;
sym_t SUB;
sym_t ALLOC;
sym_t ASSIGN;
sym_t DOM;

/*
 * Heaps offset.
 */
static size_t heaps_offset;

/*
 * Hepas state.
 */
#define FLAG_DELAY      1       // Already delayed.
static inline bool ALWAYS_INLINE heaps_is_set(prop_t prop, word_t flag)
{
    return ((prop->state & flag) != 0);
}
static inline void ALWAYS_INLINE heaps_set(prop_t prop, word_t flag)
{
    prop->state |= flag;
}

/*
 * Prototypes.
 */
static void heaps_init(void);
static void heaps_delay(term_t x, prop_t prop);
static void heaps_emp_handler(prop_t prop);
static void heaps_one_handler(prop_t prop);
static void heaps_sep_handler(prop_t prop);
static void heaps_eq_handler(prop_t prop);
static void heaps_sub_handler(prop_t prop);
static void heaps_alloc_handler(prop_t prop);
static void heaps_assign_handler(prop_t prop);
static void heaps_in_handler(prop_t prop);
static void heaps_dom_handler(prop_t prop);
static bool heaps_propagate_in(reason_t reason, term_t h, term_t p, term_t v);
static bool heaps_propagate_eq(reason_t reason, term_t x, term_t y);
static bool heaps_propagate_neq(reason_t reason, term_t x, term_t y);
extern decision_t heaps_ask_eq(reason_t reason, term_t x, term_t y);

/*
 * Solver.
 */
static struct solver_s solver_heaps_0 =
{
    heaps_init,
    NULL,
    "heaps"
};
solver_t solver_heaps = &solver_heaps_0;

/*
 * Solver initialization.
 */
static void heaps_init(void)
{
    IN  = make_sym("in", 3, true);
    EMP = make_sym("emp", 1, true);
    ONE = make_sym("one", 3, true);
    SEP = make_sym("sep", 3, true);
    EQUAL = make_sym("eq", 2, true);
    SUB = make_sym("sub", 2, true);
    ALLOC = make_sym("alloc", 4, true);
    ASSIGN = make_sym("assign", 4, true);
    DOM = make_sym("dom", 2, true);

    EQUAL->flags |= FLAG_COMMUTATIVE;

    typeinst_t TYPEINST_VAR_HEAP = make_var_typeinst(make_typeinst("heap"));
    typesig_t sig_bh = make_typesig(TYPEINST_BOOL, TYPEINST_VAR_HEAP);
    typesig_t sig_bhh = make_typesig(TYPEINST_BOOL, TYPEINST_VAR_HEAP,
        TYPEINST_VAR_HEAP);
    typesig_t sig_bhnn = make_typesig(TYPEINST_BOOL, TYPEINST_VAR_HEAP,
        TYPEINST_VAR_NUM, TYPEINST_VAR_NUM);
    typesig_t sig_bhhh = make_typesig(TYPEINST_BOOL, TYPEINST_VAR_HEAP,
        TYPEINST_VAR_HEAP, TYPEINST_VAR_HEAP);
    typesig_t sig_bhhnn = make_typesig(TYPEINST_BOOL, TYPEINST_VAR_HEAP,
        TYPEINST_VAR_HEAP, TYPEINST_VAR_NUM, TYPEINST_VAR_NUM);
    typesig_t sig_bhn = make_typesig(TYPEINST_BOOL, TYPEINST_VAR_HEAP,
        TYPEINST_VAR_NUM);

    register_typesig(IN, sig_bhnn);
    register_typesig(EMP, sig_bh);
    register_typesig(ONE, sig_bhnn);
    register_typesig(SEP, sig_bhhh);
    register_typesig(EQUAL, sig_bhh);
    register_typesig(SUB, sig_bhh);
    register_typesig(ALLOC, sig_bhhnn);
    register_typesig(ASSIGN, sig_bhhnn);
    register_typesig(DOM, sig_bhn);

    register_solver(IN, 3, EVENT_ALL, heaps_in_handler,
        make_lookup(T, T, T), make_lookup(T, T, _), make_lookup(T, _, _));
    register_solver(EMP, 6, EVENT_ALL, heaps_emp_handler);
    register_solver(ONE, 6, EVENT_ALL, heaps_one_handler);
    register_solver(SEP, 6, EVENT_ALL, heaps_sep_handler);
    register_solver(EQUAL, 6, EVENT_ALL, heaps_eq_handler);
    register_solver(SUB, 6, EVENT_ALL, heaps_sub_handler);
    register_solver(ALLOC, 7, EVENT_ALL, heaps_alloc_handler);
    register_solver(ASSIGN, 7, EVENT_ALL, heaps_assign_handler);
    register_solver(DOM, 5, EVENT_ALL, heaps_dom_handler,
        make_lookup(T, _));

    register_lookup(EQ, make_lookup(T, T));
    register_lookup(GT, make_lookup(T, T));
    register_lookup(EQ_PLUS_C, make_lookup(T, T, _));

    proplist_t template = NULL;
    heaps_offset = alloc_extra(WORD_SIZEOF(template), (word_t *)&template);

    {
        term_t h = term_var(make_var("H")),
               h1 = term_var(make_var("H1")),
               h2 = term_var(make_var("H2")),
               s = term_var(make_var("s")),
               t = term_var(make_var("t")),
               u = term_var(make_var("u")),
               p = term_var(make_var("p")),
               v = term_var(make_var("v"));

        // not emp(H) ---> in(H, s, t)
        term_t emp = term("emp", h);
        term_t in = term("in", h, s, t);
        term_t head = term("not", emp);
        rewrite_rule(head, in);

        // not one(H, p, v) ---> emp(H) \/
        //                       (in(H, s, t) /\ (p != s \/ v != t));
        head = term("one", h, p, v);
        head = term("not", head);
        term_t case_1  = emp;
        term_t p_neq_s = term("!=", p, s);
        term_t v_neq_t = term("!=", v, t);
        term_t diffs   = term("\\/", p_neq_s, v_neq_t);
        term_t case_2  = term("/\\", in, diffs);
        term_t body    = term("\\/", case_1, case_2);
        rewrite_rule(head, body);

        // not eq(H1, H2) ---> (in(H1, s, t) /\ not in(H2, s, t)) \/
        //                     (not in(H1, s, t) /\ in(H2, s, t));
        head = term("eq", h1, h2);
        head = term("not", head);
        term_t in1 = term("in", h1, s, t);
        term_t in2 = term("in", h2, s, t);
        term_t not_in1 = term("not", in1);
        term_t not_in2 = term("not", in2);
        case_1 = term("/\\", in1, not_in2);
        case_2 = term("/\\", in2, not_in1);
        body = term("\\/", case_1, case_2);
        rewrite_rule(head, body);

        // not sub(H1, H2) ---> in(H1, s, t) /\ not in(H2, s, t))
        head = term("sub", h1, h2);
        head = term("not", head);
        body = case_1;
        rewrite_rule(head, body);

        // not sep(h, h1, h2) --->
        //          (in(H, s, t) /\ not in(H1, s, t) /\ not in(H2, s, t)) \/
        //          (not in(H, s, t) /\ (in(H1, s, t) \/ in(H2, s, t)) \/
        //          (in(H1, s, t) /\ in(H2, s, u))
        head = term("sep", h, h1, h2);
        head = term("not", head);
        term_t not_in = term("not", in);
        case_1 = term("/\\", not_in1, not_in2);
        case_1 = term("/\\", in, case_1);
        case_2 = term("\\/", in1, in2);
        case_2 = term("/\\", not_in, case_2);
        term_t in3 = term("in", h2, s, u);
        term_t case_3 = term("/\\", in1, in3);
        body = term("\\/", case_1, case_2);
        body = term("\\/", body, case_3);
        rewrite_rule(head, body);
    }
}

/*
 * Delay a propagator on a heap variable.
 */
static void heaps_delay(term_t x, prop_t prop)
{
    if (heaps_is_set(prop, FLAG_DELAY))
        return;
    proplist_t *delays = (proplist_t *)extra(var(x), heaps_offset);
    *delays = delay(prop, *delays);
}

/*
 * H = 0 handler.
 */
static void heaps_emp_handler(prop_t prop)
{
    debug("!mHEAPS!d WAKE %s", show_cons(constraint(prop)));

    cons_t c = constraint(prop);
    term_t h = c->args[H];

    heaps_delay(h, prop);
    heaps_set(prop, FLAG_DELAY);

    switch (decision(c->b))
    {
        case TRUE:
        {
            reason_t reason = make_reason(c->b);
            cons_t dom = find(reason, TRUE, DOM, h, _);
            if (dom != NULL)
            {
                antecedent(reason, dom->b);
                fail(reason);
            }
            return;
        }
        default:
            return;
    }
}

/*
 * H = {p |-> v} handler.
 */
static void heaps_one_handler(prop_t prop)
{
    debug("!mHEAPS!d WAKE %s", show_cons(constraint(prop)));

    cons_t c = constraint(prop);
    term_t h = c->args[H];

    heaps_delay(h, prop);
    heaps_set(prop, FLAG_DELAY);

    switch (decision(c->b))
    {
        case TRUE:
        {
            reason_t reason = make_reason(c->b);
            size_t sp = save(reason);
            term_t p = c->args[P];
            term_t v = c->args[V];

            if (heaps_propagate_in(reason, h, p, v))
                propagate(reason);
            restore(reason, sp);

            cons_t in;
            for (itr_t i = findall(reason, TRUE, IN, h, _, _); get(i, &in);
                    next(i))
            {
                antecedent(reason, in->b);
                term_t w = in->args[V];
                if (heaps_propagate_eq(reason, v, w))
                    propagate(reason);
            }

            cons_t dom;
            for (itr_t i = findall(reason, TRUE, DOM, h, _); get(i, &dom);
                    next(i))
            {
                antecedent(reason, dom->b);
                term_t q = dom->args[P];
                if (heaps_propagate_eq(reason, p, q))
                    propagate(reason);
            }
            return;
        }
        default:
            return;
    }
}

/*
 * H = H1 * H2 handler.
 */
static void heaps_sep_handler(prop_t prop)
{
    debug("!mHEAPS!d WAKE %s", show_cons(constraint(prop)));

    cons_t c = constraint(prop);
    term_t h  = c->args[H];
    term_t h1 = c->args[H1];
    term_t h2 = c->args[H2];
    
    heaps_delay(h, prop);
    heaps_delay(h1, prop);
    heaps_delay(h2, prop);
    heaps_set(prop, FLAG_DELAY);
    
    switch (decision(c->b))
    {
        case TRUE:
        {
            reason_t reason = make_reason(c->b);

            // h = h1 * h2 /\ dom(h1, p) /\ dom(h2, q) ==> p != q
            // h = h1 * h2 /\ dom(h1, p) ==> dom(h, p)
            cons_t dom1, dom2;
            for (itr_t i = findall(reason, TRUE, DOM, h1, _); get(i, &dom1);
                next(i))
            {
                antecedent(reason, dom1->b);
                size_t sp = save(reason);
                term_t p = dom1->args[P];
                cons_t dom = make_cons(reason, DOM, h, p);
                consequent(reason, dom->b);
                propagate(reason);
                restore(reason, sp);

                for (itr_t j = findall(reason, TRUE, DOM, h2, _);
                    get(j, &dom2); next(j))
                {
                    antecedent(reason, dom2->b);
                    term_t q = dom2->args[P];
                    if (heaps_propagate_neq(reason, p, q))
                        propagate(reason);
                }
            }

            // h = h1 * h2 /\ dom(h2, p) ==> dom(h, p)
            for (itr_t i = findall(reason, TRUE, DOM, h2, _); get(i, &dom2);
                next(i))
            {
                antecedent(reason, dom2->b);
                term_t p = dom2->args[P];
                cons_t dom = make_cons(reason, DOM, h, p);
                consequent(reason, dom->b);
                propagate(reason);
            }

            // h = h1 * h2 /\ in(h1, p, v) ==> in(h, p, v)
            cons_t in1, in2;
            for (itr_t i = findall(reason, TRUE, IN, h1, _, _); get(i, &in1);
                next(i))
            {
                antecedent(reason, in1->b);
                term_t p = in1->args[P];
                term_t v = in1->args[V];
                if (heaps_propagate_in(reason, h, p, v))
                    propagate(reason);
            }

            // h = h1 * h2 /\ in(h2, p, v) ==> in(h, p, v)
            for (itr_t i = findall(reason, TRUE, IN, h2, _, _); get(i, &in2);
                next(i))
            {
                antecedent(reason, in2->b);
                term_t p = in2->args[P];
                term_t v = in2->args[V];
                if (heaps_propagate_in(reason, h, p, v))
                    propagate(reason);
            }

            // h = h1 * h2 /\ in(h, p, v) ==> in(h1, p, v) \/ in(h2, p, v)
            cons_t in;
            for (itr_t i = findall(reason, TRUE, IN, h, _, _); get(i, &in);
                next(i))
            {
                antecedent(reason, in->b);
                term_t p = in->args[P];
                term_t v = in->args[V];
                if (heaps_propagate_in(reason, h1, p, v) &&
                    heaps_propagate_in(reason, h2, p, v))
                    propagate(reason);
            }

            // h = h1 * h2 /\ dom(h, p) ==> dom(h1, p) \/ dom(h2, p)
            cons_t dom;
            for (itr_t i = findall(reason, TRUE, DOM, h, _); get(i, &dom);
                next(i))
            {
                antecedent(reason, dom->b);
                term_t p = dom->args[P];
                dom1 = make_cons(reason, DOM, h1, p);
                consequent(reason, dom1->b);
                dom2 = make_cons(reason, DOM, h2, p);
                consequent(reason, dom2->b);
                propagate(reason);
            }

            return;
        }
        default:
            return;
    }
}

/*
 * in(H, p, v) handler.
 */
static void heaps_in_handler(prop_t prop)
{
    debug("!mHEAPS!d WAKE %s", show_cons(constraint(prop)));

    cons_t in = constraint(prop);
    term_t h = in->args[H];
    term_t p = in->args[P];
    term_t v = in->args[V];

    switch (decision(in->b))
    {
        case TRUE:
        {
            reason_t reason = make_reason(in->b);
            size_t sp = save(reason);

            // in(h, p, v) ==> dom(h, p)
            cons_t dom = make_cons(reason, DOM, h, p);
            consequent(reason, dom->b);
            propagate(reason);
            restore(reason, sp);
 
            // in(h, p, v) /\ in(h, p, w) ==> v = w
            cons_t in1;
            for (itr_t i = findall(reason, TRUE, IN, h, p, _); get(i, &in1);
                next(i))
            {
                if (in1 == in)
                    continue;
                antecedent(reason, in1->b);
                term_t w = in1->args[V];
                if (heaps_propagate_eq(reason, v, w))
                    propagate(reason);
                purge(in);
                return;
            }

            // Signal that we have a new in/3 
            proplist_t *delays = (proplist_t *)extra(var(h), heaps_offset);
            event(*delays);

            return;
        }

        default:
            return;
    }
}

/*
 * H1 = H2 handler.
 */
static void heaps_eq_handler(prop_t prop)
{
    debug("!mHEAPS!d WAKE %s", show_cons(constraint(prop)));

    cons_t c = constraint(prop);
    term_t h1 = c->args[H];
    term_t h2 = c->args[H1];

    heaps_delay(h1, prop);
    heaps_delay(h2, prop);
    heaps_set(prop, FLAG_DELAY);

    switch (decision(c->b))
    {
        case TRUE:
        {
            reason_t reason = make_reason(c->b);
            cons_t in;
            for (itr_t i = findall(reason, TRUE, IN, h1, _, _); get(i, &in);
                next(i))
            {
                antecedent(reason, in->b);
                term_t p = in->args[P];
                term_t v = in->args[V];
                if (heaps_propagate_in(reason, h2, p, v))
                    propagate(reason);
            }

            for (itr_t i = findall(reason, TRUE, IN, h2, _, _); get(i, &in);
                next(i))
            {
                antecedent(reason, in->b);
                term_t p = in->args[P];
                term_t v = in->args[V];
                if (heaps_propagate_in(reason, h1, p, v))
                    propagate(reason);
            }

            cons_t dom;
            for (itr_t i = findall(reason, TRUE, DOM, h1, _); get(i, &dom);
                next(i))
            {
                antecedent(reason, dom->b);
                term_t p = dom->args[P];
                cons_t dom2 = make_cons(reason, DOM, h2, p);
                consequent(reason, dom2->b);
                propagate(reason);
            }

            for (itr_t i = findall(reason, TRUE, DOM, h2, _); get(i, &dom);
                next(i))
            {
                antecedent(reason, dom->b);
                term_t p = dom->args[P];
                cons_t dom1 = make_cons(reason, DOM, h1, p);
                consequent(reason, dom1->b);
                propagate(reason);
            }

            return;
        }
        default:
            return;
    }
}

/*
 * H1 subheap H2 handler.
 */
static void heaps_sub_handler(prop_t prop)
{
    debug("!mHEAPS!d WAKE %s", show_cons(constraint(prop)));

    cons_t c = constraint(prop);
    term_t h1 = c->args[H];
    term_t h2 = c->args[H1];

    heaps_delay(h1, prop);
    heaps_delay(h2, prop);
    heaps_set(prop, FLAG_DELAY);

    switch (decision(c->b))
    {
        case TRUE:
        {
            reason_t reason = make_reason(c->b);
            cons_t in;
            for (itr_t i = findall(reason, TRUE, IN, h1, _, _); get(i, &in);
                next(i))
            {
                antecedent(reason, in->b);
                term_t p = in->args[P];
                term_t v = in->args[V];
                if (heaps_propagate_in(reason, h2, p, v))
                    propagate(reason);
            }

            cons_t dom;
            for (itr_t i = findall(reason, TRUE, DOM, h1, _); get(i, &dom);
                next(i))
            {
                antecedent(reason, dom->b);
                term_t p = dom->args[P];
                cons_t dom2 = make_cons(reason, DOM, h2, p);
                consequent(reason, dom2->b);
                propagate(reason);
            }
            return;
        }
        default:
            return;
    }
}

/*
 * alloc handler.
 */
static void heaps_alloc_handler(prop_t prop)
{
    debug("!mHEAPS!d WAKE %s", show_cons(constraint(prop)));

    cons_t c = constraint(prop);
    if (decision(c->b) != TRUE)
        return;
    reason_t reason = make_reason(c->b);
    size_t sp = save(reason);

    term_t h = c->args[H];
    term_t h1 = c->args[1];
    term_t p = c->args[2];
    term_t v = c->args[3];

    heaps_delay(h, prop);
    heaps_delay(h1, prop);
    heaps_set(prop, FLAG_DELAY);

    if (heaps_propagate_in(reason, h, p, v))
        propagate(reason);
    restore(reason, sp);

    cons_t in;
    for (itr_t i = findall(reason, TRUE, IN, h, _, _); get(i, &in); next(i))
    {
        antecedent(reason, in->b);
        term_t q = in->args[P];
        term_t w = in->args[V];
        if (heaps_propagate_in(reason, h1, q, w) &&
            heaps_propagate_eq(reason, p, q) &&
            !islate(reason))
            propagate(reason);
    }
    for (itr_t i = findall(reason, TRUE, IN, h1, _, _); get(i, &in); next(i))
    {
        antecedent(reason, in->b);
        term_t q = in->args[P];
        term_t w = in->args[V];
        sp = save(reason);
        if (heaps_propagate_neq(reason, p, q))
            propagate(reason);
        restore(reason, sp);
        if (heaps_propagate_in(reason, h, q, w))
            propagate(reason);
    }

    cons_t dom;
    for (itr_t i = findall(reason, TRUE, DOM, h, _); get(i, &dom); next(i))
    {
        antecedent(reason, dom->b);
        term_t q = dom->args[P];
        if (heaps_propagate_eq(reason, p, q))
        {
            cons_t dom1 = make_cons(reason, DOM, h1, q);
            consequent(reason, dom1->b);
            propagate(reason);
        }
    }
    for (itr_t i = findall(reason, TRUE, DOM, h1, _); get(i, &dom); next(i))
    {
        antecedent(reason, dom->b);
        term_t q = dom->args[P];
        sp = save(reason);
        if (heaps_propagate_neq(reason, p, q))
            propagate(reason);
        restore(reason, sp);
        cons_t dom1 = make_cons(reason, DOM, h, q);
        consequent(reason, dom1->b);
        propagate(reason);
    }
}

/*
 * assign handler.
 */
static void heaps_assign_handler(prop_t prop)
{
    debug("!mHEAPS!d WAKE %s", show_cons(constraint(prop)));

    cons_t c = constraint(prop);
    if (decision(c->b) != TRUE)
        return;
    reason_t reason = make_reason(c->b);
    size_t sp = save(reason);

    term_t h = c->args[H];
    term_t h1 = c->args[1];
    term_t p = c->args[2];
    term_t v = c->args[3];

    heaps_delay(h, prop);
    heaps_delay(h1, prop);
    heaps_set(prop, FLAG_DELAY);

    if (heaps_propagate_in(reason, h, p, v))
        propagate(reason);
    restore(reason, sp);

    cons_t dom = make_cons(reason, DOM, h1, p);
    consequent(reason, dom->b);
    propagate(reason);
    restore(reason, sp);

    cons_t in;
    for (itr_t i = findall(reason, TRUE, IN, h, _, _); get(i, &in); next(i))
    {
        antecedent(reason, in->b);
        term_t q = in->args[P];
        term_t w = in->args[V];
        if (heaps_propagate_in(reason, h1, q, w) &&
            heaps_propagate_eq(reason, p, q) &&
            !islate(reason))
            propagate(reason);
    }
    for (itr_t i = findall(reason, TRUE, IN, h1, _, _); get(i, &in); next(i))
    {
        antecedent(reason, in->b);
        term_t q = in->args[P];
        term_t w = in->args[V];
        if (heaps_propagate_in(reason, h, q, w) &&
            heaps_propagate_eq(reason, p, q) &&
            !islate(reason))
            propagate(reason);
    }

    for (itr_t i = findall(reason, TRUE, DOM, h, _); get(i, &dom); next(i))
    {
        antecedent(reason, dom->b);
        term_t q = dom->args[P];
        if (heaps_propagate_eq(reason, p, q))
        {
            cons_t dom1 = make_cons(reason, DOM, h1, q);
            consequent(reason, dom1->b);
            propagate(reason);
        }
    }
    for (itr_t i = findall(reason, TRUE, DOM, h1, _); get(i, &dom); next(i))
    {
        antecedent(reason, dom->b);
        term_t q = dom->args[P];
        if (heaps_propagate_eq(reason, p, q))
        {
            cons_t dom1 = make_cons(reason, DOM, h, q);
            consequent(reason, dom1->b);
            propagate(reason);
        }
    }
}

/*
 * dom(H, p) handler.
 */
static void heaps_dom_handler(prop_t prop)
{
    debug("!mHEAPS!d WAKE %s", show_cons(constraint(prop)));

    cons_t c = constraint(prop);
    term_t h = c->args[H];

    switch (decision(c->b))
    {
        case TRUE:
        {
            // Signal that we have a new dom/3 
            proplist_t *delays = (proplist_t *)extra(var(h), heaps_offset);
            event(*delays);
            return;
        }
        default:
            return;
    }
}

/*
 * Propagate a in(h, p, v) constraint.
 */
static bool heaps_propagate_in(reason_t reason, term_t h, term_t p, term_t v)
{
    debug("!mHEAPS!d PROPAGATE in(%s, %s, %s)", show(h), show(p), show(v));

    cons_t in = find(reason, TRUE, IN, h, p, _);
    if (in != NULL)
    {
        antecedent(reason, in->b);
        term_t w = in->args[V];
        return heaps_propagate_eq(reason, v, w);
    }

    in = make_cons(reason, IN, h, p, v);
    consequent(reason, in->b);
    return true;
}

/*
 * Propagate a (x = y) constraint.
 */
static bool heaps_propagate_eq(reason_t reason, term_t x, term_t y)
{
    debug("!mHEAPS!d PROPAGATE %s = %s", show(x), show(y));

#if 0
    switch (heaps_ask_eq(reason, x, y))
    {
        case TRUE:
            return false;
        case FALSE:
            return true;
        case UNKNOWN:
            break;
    }
#else
    if (x == y)
        return false;
#endif

    cons_t eq = make_cons(reason, EQ, x, y);
    consequent(reason, eq->b);
    return true;
}

/*
 * Propagate a (x != y) constraint.
 */
static bool heaps_propagate_neq(reason_t reason, term_t x, term_t y)
{
    debug("heaps_propagate_neq(%s, %s)", show(x), show(y));

#if 0
    switch (heaps_ask_eq(reason, x, y))
    {
        case FALSE:
            return false;
        case TRUE:
            return true;
        case UNKNOWN:
            break;
    }
#else
    if (x == y)
        return true;
#endif

    cons_t eq = make_cons(reason, EQ, x, y);
    consequent(reason, -eq->b);
    return true;
}

/*
 * Heaps ask equality.
 */
extern decision_t heaps_ask_eq(reason_t reason, term_t x, term_t y)
{
    debug("ASK %s = %s", show(x), show(y));

    if (match_vars(reason, var(x), var(y)))
    {
        debug("TRUE");
        return TRUE;
    }

    cons_t eq = find(reason, SET, EQ, x, y);
    if (eq != NULL)
    {
        antecedent(reason, literal(eq->b));
        return decision(eq->b);
    }
    eq = find(reason, SET, EQ, y, x);
    if (eq != NULL)
    {
        antecedent(reason, literal(eq->b));
        return decision(eq->b);
    }

    cons_t gt = find(reason, TRUE, GT, x, y);
    if (gt != NULL)
    {
        antecedent(reason, gt->b);
        return FALSE;
    }
    gt = find(reason, TRUE, GT, y, x);
    if (gt != NULL)
    {
        antecedent(reason, gt->b);
        return FALSE;
    }

    cons_t plusc = find(reason, TRUE, EQ_PLUS_C, x, y, _);
    if (plusc != NULL)
    {
        antecedent(reason, plusc->b);
        return FALSE;
    }
    plusc = find(reason, TRUE, EQ_PLUS_C, y, x, _);
    if (plusc != NULL)
    {
        antecedent(reason, plusc->b);
        return FALSE;
    }

    return UNKNOWN;
}

