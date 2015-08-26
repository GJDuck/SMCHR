/*
 * pass_rewrite.c
 * Copyright (C) 2014 National University of Singapore
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "expr.h"
#include "log.h"
#include "map.h"
#include "names.h"
#include "pass_cnf.h"
#include "pass_flatten.h"
#include "show.h"

#define MAX_DEPTH       64

typedef struct rule_s *rule_t;
struct rule_s
{
    expr_t head;
    expr_t body;
    rule_t next;
};

MAP_DECL(subs, expr_t, expr_t, expr_compare);
MAP_DECL(cseinfo, expr_t, expr_t, expr_compare);
MAP_DECL(ruleinfo, atom_t, rule_t, compare_atom);

struct context_s
{
    ruleinfo_t rules;
    cseinfo_t cseinfo;
    size_t depth;
    size_t varid;
    const char *file;
    size_t line;
};
typedef struct context_s *context_t;

static ruleinfo_t rules;
atom_t ATOM_REWRITE;

/*
 * Prototypes.
 */
static expr_t rewrite_expr(context_t cxt, expr_t e);
static expr_t rewrite(context_t cxt, expr_t e);
static bool match(expr_t head, expr_t e, subs_t *subs);
static expr_t replace(context_t cxt, expr_t body, subs_t *subs);

/*
 * Initialize this module.
 */
extern void rewrite_init(void)
{
    rules = ruleinfo_init();
    if (!gc_root(&rules, sizeof(rules)))
        panic("failed to set GC root for rewrite rules: %s", strerror(errno));

    ATOM_REWRITE = make_atom("-->", 2);
    typesig_t sig_bbb = make_typesig(TYPEINST_BOOL, TYPEINST_BOOL,
        TYPEINST_BOOL);
    typeinst_declare(ATOM_REWRITE, sig_bbb);
}

/*
 * Register a rewrite rule.
 */
extern bool register_rewrite_rule(term_t rule, const char *filename,
    size_t lineno)
{
    typeinfo_t tinfo;
    if (!typecheck(filename, lineno, rule, &tinfo))
        return false;

    if (type(rule) != FUNC)
    {
register_rewrite_rule_bad_rule_error:
        error("(%s: %zu) expected a rewrite rule; found `!y%s!d'", filename,
            lineno, show(rule));
        return false;
    }
    func_t f = func(rule);
    if (f->atom != ATOM_REWRITE)
        goto register_rewrite_rule_bad_rule_error;
    term_t head = f->args[0];
    term_t body = f->args[1];

    if (type(head) == BOOL)
    {
register_rewrite_rule_bad_head_error:
        error("(%s: %zu) rewrite rule head must be a predicate; "
            "found `!y%s!d'", filename, lineno, show(head));
        return false;
    }

    expr_t lhs = expr_compile(tinfo, head);
    exprop_t op = expr_op(lhs);
    if (op == EXPROP_AND || op == EXPROP_OR)
        goto register_rewrite_rule_bad_head_error;
    expr_t rhs = expr_compile(tinfo, body);

    rhs = pass_flatten_expr(filename, lineno, rhs);
    rhs = pass_nnf_expr(filename, lineno, rhs);

    bool neg = (op == EXPROP_NOT);
    rule_t entry = (rule_t)gc_malloc(sizeof(struct rule_s));
    entry->head = lhs;
    entry->body = rhs;
    entry->next = NULL;

    atom_t key = (neg? expr_sym(expr_arg(lhs, 0)): expr_sym(lhs));
    rule_t rs;
    if (ruleinfo_search(rules, key, &rs))
        entry->next = rs;
    rules = ruleinfo_insert(rules, key, entry);

    debug("!bREWRITE RULE!d: %s --> %s", show(expr_term(lhs)),
        show(expr_term(rhs)));

    return true;
}

/*
 * Apply rewrite rules pass.
 */
extern expr_t pass_rewrite_expr(const char *filename, size_t lineno, expr_t e)
{
    if (ruleinfo_isempty(rules))
        return e;

    struct context_s context0;
    context_t cxt = &context0;
    cxt->cseinfo = cseinfo_init();
    cxt->rules = rules;
    cxt->depth = 0;
    cxt->varid = 0;
    cxt->file = filename;
    cxt->line = lineno;

    e = rewrite_expr(cxt, e);
    return e;
}

/*
 * Rewrite an expr_t.
 */
static expr_t rewrite_expr(context_t cxt, expr_t e)
{
    if (expr_gettype(e) != EXPRTYPE_OP)
        return e;
 
    exprop_t op = expr_op(e);
    switch (op)
    {
        case EXPROP_AND: case EXPROP_OR:
        {
            expr_t acc = (op == EXPROP_OR? expr_bool(false): expr_bool(true));
            expr_t k, v;
            for (expritr_t i = expritr(e); expr_getpair(i, &k, &v);
                    expr_next(i))
            {
                if (v == expr_bool(true))
                    k = expr_not(k);
                k = rewrite_expr(cxt, k);
                acc = (op == EXPROP_OR? expr_or(acc, k): expr_and(acc, k));
            }
            return acc;
        }
        default:
        {
            e = rewrite(cxt, e);
            return e;
        }
    }
}

/*
 * Apply a rewrite rule.
 */
static expr_t rewrite(context_t cxt, expr_t e)
{
    if (expr_gettype(e) != EXPRTYPE_OP)
        return e;
    if (expr_op(e) == EXPROP_NOT)
    {
        expr_t arg = expr_arg(e, 0);
        if (expr_gettype(arg) != EXPRTYPE_OP)
            return e;
    }

    expr_t res;
    if (cseinfo_search(cxt->cseinfo, e, &res))
        return res;

    atom_t key = (expr_op(e) == EXPROP_NOT? expr_sym(expr_arg(e, 0)):
        expr_sym(e));

    rule_t rs;
    if (!ruleinfo_search(cxt->rules, key, &rs))
        return e;


    cxt->depth++;
    bool found = false;
    for (rule_t rule = rs; rule != NULL; rule = rule->next)
    {
        subs_t subs = subs_init();
        if (match(rule->head, e, &subs))
        {
            if (cxt->depth >= MAX_DEPTH)
            {
                warning("(%s: %zu) failed to rewrite expression `!y%s!d'; "
                    "maximum recursive depth of %zu was reached", cxt->file, 
                    cxt->line, show(expr_term(e)), MAX_DEPTH);
                break;
            }
            res = replace(cxt, rule->body, &subs);
            debug("!bREWRITE!d %s --> %s", show(expr_term(key)),
                show(expr_term(res)));
            found = true;
            break;
        }
    }
    cxt->depth--;

    if (!found)
        return e;

    cxt->cseinfo = cseinfo_insert(cxt->cseinfo, e, res);
    return res;
}

/*
 * Expression matching.
 */
static bool match(expr_t head, expr_t e, subs_t *subs)
{
    switch (expr_gettype(head))
    {
        case EXPRTYPE_VAR:
        {
            expr_t old_e;
            if (subs_search(*subs, head, &old_e))
                return (expr_compare(e, old_e) == 0);
            *subs = subs_insert(*subs, head, e);
            return true;
        }
        case EXPRTYPE_OP:
            break;
        default:
            return (expr_compare(head, e) == 0);
    }

    if (expr_gettype(e) != EXPRTYPE_OP)
        return false;
    if (expr_sym(e) != expr_sym(head))
        return false;

    size_t arity = expr_arity(e);
    for (size_t i = 0; i < arity; i++)
    {
        expr_t head_arg = expr_arg(head, i);
        expr_t arg = expr_arg(e, i);
        if (!match(head_arg, arg, subs))
            return false;
    }

    return true;
}

/*
 * Expression replacement.
 */
static expr_t replace(context_t cxt, expr_t body, subs_t *subs)
{
    switch (expr_gettype(body))
    {
        case EXPRTYPE_VAR:
        {
            expr_t val;
            if (subs_search(*subs, body, &val))
                return val;
            char *name = unique_name("R", &cxt->varid);
            var_t v0 = make_var(name);
            val = expr_var(v0);
            *subs = subs_insert(*subs, body, val);
            return val;
        }
        case EXPRTYPE_OP:
            break;
        default:
            return body;
    }

    exprop_t op = expr_op(body);
    switch (op)
    {
        case EXPROP_NOT: case EXPROP_AND: case EXPROP_OR:
        {
            expr_t acc = (op == EXPROP_OR? expr_bool(false): expr_bool(true));
            expr_t k, v;
            for (expritr_t i = expritr(body); expr_getpair(i, &k, &v);
                    expr_next(i))
            {
                bool d = (op != EXPROP_OR);
                d = (v == expr_bool(true)? !d: d);
                k = replace(cxt, k, subs);
                if (v == expr_bool(true))
                    k = expr_not(k);
                k = rewrite(cxt, k);
                acc = (op == EXPROP_OR? expr_or(acc, k): expr_and(acc, k));
            }
            return acc;
        }
        default:
        {
            size_t arity = expr_arity(body);
            expr_t args[arity];
            expr_args(body, args);
            for (size_t i = 0; i < arity; i++)
                args[i] = replace(cxt, args[i], subs);
            expr_t e = expr(op, args);
            e = rewrite(cxt, e);
            return e;
        }
    }
}

