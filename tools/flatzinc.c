/*
 * flatzinc.c
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

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "map.h"
#include "misc.h"
#include "show.h"
#include "term.h"

/*
 * Variable sets.
 */
MAP_DECL(varset, char *, var_t, strcmp_compare);

/*
 * FZN tokens.
 */
enum token_e
{
    TOKEN_ARRAY = 1000,
    TOKEN_BOOL,
    TOKEN_CONSTRAINT,
    TOKEN_FALSE,
    TOKEN_FLOAT,
    TOKEN_INT,
    TOKEN_MAXIMIZE,
    TOKEN_MINIMIZE,
    TOKEN_OF,
    TOKEN_PREDICATE,
    TOKEN_SATISFY,
    TOKEN_SET,
    TOKEN_SOLVE,
    TOKEN_TRUE,
    TOKEN_VAR,
    TOKEN_DOTDOT,
    TOKEN_COLONCOLON,
    TOKEN_INT_LIT,
    TOKEN_FLOAT_LIT,
    TOKEN_STRING_LIT,
    TOKEN_IDENT,
    TOKEN_EOF,
    TOKEN_ERROR
};
typedef int token_t;

#define TOKEN_MAXLEN    64
#define MAXARGS         1024

struct tokenlist_s
{
    token_t token;
    term_t val;
    struct tokenlist_s *next;
};
typedef struct tokenlist_s *tokenlist_t;

/*
 * FZN special names.
 */
struct name_s
{
    const char *name;
    token_t token;
};
static struct name_s names[] = 
{
    {"array",       TOKEN_ARRAY},
    {"bool",        TOKEN_BOOL},
    {"constraint",  TOKEN_CONSTRAINT},
    {"false",       TOKEN_FALSE},
    {"float",       TOKEN_FLOAT},
    {"int",         TOKEN_INT},
    {"maximize",    TOKEN_MAXIMIZE},
    {"minimize",    TOKEN_MINIMIZE},
    {"of",          TOKEN_OF},
    {"predicate",   TOKEN_PREDICATE},
    {"satisfy",     TOKEN_SATISFY},
    {"set",         TOKEN_SET},
    {"solve",       TOKEN_SOLVE},
    {"true",        TOKEN_TRUE},
    {"var",         TOKEN_VAR},
};
static int name_compare(const void *a, const void *b)
{
    struct name_s *aa = (struct name_s *)a;
    struct name_s *bb = (struct name_s *)b;
    return strcmp(aa->name, bb->name);
}

/*
 * Translation context.
 */
MAP_DECL(declinfo, var_t, term_t, compare_var);
MAP_DECL(lookupinfo, func_t, var_t, compare_func);
struct context_s
{
    declinfo_t declinfo;
    lookupinfo_t lookupinfo;
};
typedef struct context_s *context_t;

/*
 * Special atoms.
 */
static atom_t AND;
static atom_t DOM;
static atom_t SET_EMPTY;
static atom_t SET_ELEM;
static atom_t ARRAY_EMPTY;
static atom_t ARRAY_ELEM;
static atom_t LOOKUP;
static atom_t RANGE;

static atom_t BOOL_LT;
static atom_t BOOL_LE;
static atom_t BOOL_XOR;
static atom_t BOOL2INT;

static atom_t INT_EQ;
static atom_t INT_NE;
static atom_t INT_LE;
static atom_t INT_LT;
static atom_t INT_MAX_;
static atom_t INT_PLUS;
static atom_t INT_TIMES;
static atom_t INT_LIN_EQ;
static atom_t INT_LIN_LE;
static atom_t INT_EQ_REIF;
static atom_t INT_NE_REIF;
static atom_t INT_LE_REIF;
static atom_t INT_LT_REIF;
static atom_t INT_LIN_EQ_REIF;
static atom_t INT_LIN_LE_REIF;

static atom_t ARRAY_BOOL_OR;
static atom_t ARRAY_BOOL_AND;

/*
 * Prototypes.
 */
static term_t fzn_process(context_t cxt, term_t model);
static term_t fzn_process_expr(context_t cxt, term_t expr);
static term_t fzn_process_fold(context_t cxt, atom_t op, term_t base,
    term_t array);
static term_t fzn_process_linear(context_t cxt, term_t cs, term_t xs);
static term_t fzn_process_dom(context_t cxt, term_t x, term_t dom);
extern term_t fzn_parse(const char *filename);
static tokenlist_t fzn_parse_solve_item(tokenlist_t tokens);
static tokenlist_t fzn_parse_var_item(tokenlist_t tokens, term_t *model);
static tokenlist_t fzn_parse_predicate_item(tokenlist_t tokens);
static tokenlist_t fzn_parse_predicate_arg(tokenlist_t tokens);
static tokenlist_t fzn_parse_typeinst_expr(tokenlist_t tokens, term_t *domain);
static tokenlist_t fzn_parse_constraint_item(tokenlist_t tokens, term_t *model);
static tokenlist_t fzn_parse_expr(tokenlist_t tokens, term_t *expr);
static tokenlist_t fzn_expect_token(tokenlist_t tokens, token_t t, term_t *val);
static void NO_RETURN fzn_unexpected_token(token_t token, const char *where);
static tokenlist_t fzn_get_tokens(FILE *file);
static token_t fzn_get_token(FILE *file, varset_t *vars, term_t *val);
static token_t fzn_get_num_token(FILE *file, term_t *val);
static const char *fzn_get_token_name(token_t token);

/*
 * STUB
 */
int main(int argc, char **argv)
{
    if (!gc_init())
        panic("failed to initialize the garbage collector: %s",
            strerror(errno));

    if (argc != 2)
    {
        message("!yusage!d: %s file.fzn", argv[0]);
        return EXIT_FAILURE;
    }

    term_init();
    AND = make_atom("/\\", 2);
    DOM = make_atom("dom", 2);
    SET_EMPTY = make_atom("{}", 0);
    SET_ELEM = make_atom("{|}", 2);
    ARRAY_EMPTY = make_atom("[]", 0);
    ARRAY_ELEM = make_atom("[|]", 2);
    LOOKUP = make_atom("lookup", 2);
    RANGE = make_atom("range", 2);
   
    BOOL_LT = make_atom("bool_lt", 2);
    BOOL_LE = make_atom("bool_le", 2); 
    BOOL_XOR = make_atom("bool_xor", 3); 
    BOOL2INT = make_atom("bool2int", 2);
    
    INT_EQ = make_atom("int_eq", 2);
    INT_NE = make_atom("int_ne", 2);
    INT_LE = make_atom("int_le", 2);
    INT_LT = make_atom("int_lt", 2);
    INT_MAX_ = make_atom("int_max", 3);
    INT_PLUS = make_atom("int_plus", 3);
    INT_TIMES = make_atom("int_times", 3);
    INT_LIN_EQ = make_atom("int_lin_eq", 3);
    INT_LIN_LE = make_atom("int_lin_le", 3);
    INT_EQ_REIF = make_atom("int_eq_reif", 3);
    INT_NE_REIF = make_atom("int_ne_reif", 3);
    INT_LE_REIF = make_atom("int_le_reif", 3);
    INT_LT_REIF = make_atom("int_lt_reif", 3);
    INT_LIN_EQ_REIF = make_atom("int_lin_eq_reif", 4);
    INT_LIN_LE_REIF = make_atom("int_lin_le_reif", 4);

    ARRAY_BOOL_OR = make_atom("array_bool_or", 2);
    ARRAY_BOOL_AND = make_atom("array_bool_and", 2);

    struct context_s cxt_0;
    context_t cxt = &cxt_0;
    cxt->declinfo   = declinfo_init();
    cxt->lookupinfo = lookupinfo_init();

    term_t model_0 = fzn_parse(argv[1]);

//    show_file(stdout, model_0);
//    putchar('\n');

    term_t model_1 = fzn_process(cxt, model_0);

    show_file(stdout, model_1);
    putchar('\n');

    return 0;
}

/****************************************************************************/
/* CONVERSION                                                               */
/****************************************************************************/

/*
 * Process a FZN model.
 */
static term_t fzn_process(context_t cxt, term_t model)
{
    type_t t = type(model);
    switch (t)
    {
        case BOOL:
            return model;
        case FUNC:
            break;
        default:
            fatal("unexpected model type");
    }

    func_t f = func(model);
    atom_t atom = f->atom;
    if (atom == AND)
    {
        term_t arg0 = fzn_process(cxt, f->args[0]);
        term_t arg1 = fzn_process(cxt, f->args[1]);
        if (type(arg0) == BOOL)
        {
            if (boolean(arg0))
                return arg1;
            else
                return arg0;
        }
        if (type(arg1) == BOOL)
        {
            if (boolean(arg1))
                return arg0;
            else
                return arg1;
        }
        return term_func(make_func(AND, arg0, arg1));
    }
    size_t arity = atom_arity(atom);
    term_t args[arity];
    for (size_t i = 0; i < arity; i++)
        args[i] = fzn_process_expr(cxt, f->args[i]);

    if (atom == DOM)
        return fzn_process_dom(cxt, args[0], args[1]);
    if (atom == BOOL_LT)
    {
        term_t not1 = term_func(make_func(make_atom("not", 1), args[0]));
        return term_func(make_func(make_atom("/\\", 2), not1, args[1]));
    }
    if (atom == BOOL_LE)
    {
        term_t not1 = term_func(make_func(make_atom("not", 1), args[0]));
        return term_func(make_func(make_atom("\\/", 2), not1, args[1]));
    }
    if (atom == BOOL_XOR)
    {
        term_t xs = term_func(make_func(make_atom("xor", 2), args[1],
            args[2]));
        return term_func(make_func(make_atom("<->", 2), args[0], xs));
    }
    if (atom == BOOL2INT)
    {
        term_t eq0 = term_func(make_func(make_atom("=", 2), args[1],
            term_int(0)));
        term_t eq1 = term_func(make_func(make_atom("=", 2), args[1],
            term_int(1)));
        term_t iff1 = term_func(make_func(make_atom("<->", 2), args[0], eq1));
        term_t notb = term_func(make_func(make_atom("not", 1), args[0]));
        term_t iff2 = term_func(make_func(make_atom("<->", 2), notb, eq0));
        return term_func(make_func(make_atom("/\\", 2), iff1, iff2));
    }
    if (atom == INT_EQ)
        return term_func(make_func(make_atom("=", 2), args[0], args[1]));
    if (atom == INT_NE)
        return term_func(make_func(make_atom("!=", 2), args[0], args[1]));
    if (atom == INT_LE)
        return term_func(make_func(make_atom("<=", 2), args[0], args[1]));
    if (atom == INT_LT)
        return term_func(make_func(make_atom("<", 2), args[0], args[1]));
    if (atom == INT_MAX_)
    {
        term_t le = term_func(make_func(make_atom("<=", 2), args[0], args[1]));
        term_t ge = term_func(make_func(make_atom("<=", 2), args[1], args[0]));
        term_t eq1 = term_func(make_func(make_atom("=", 2), args[2], args[0]));
        term_t eq2 = term_func(make_func(make_atom("=", 2), args[2], args[1]));
        term_t imp1 = term_func(make_func(make_atom("->", 2), le, eq2));
        term_t imp2 = term_func(make_func(make_atom("->", 2), ge, eq1));
        return term_func(make_func(make_atom("/\\", 2), imp1, imp2));
    }
    if (atom == INT_PLUS)
    {
        term_t xs = term_func(make_func(make_atom("+", 2), args[1], args[2]));
        return term_func(make_func(make_atom("=", 2), args[0], xs));
    }
    if (atom == INT_TIMES)
    {
        term_t xs = term_func(make_func(make_atom("*", 2), args[1], args[2]));
        return term_func(make_func(make_atom("=", 2), args[0], xs));
    }       
    if (atom == INT_LIN_EQ)
    {
        term_t xs = fzn_process_linear(cxt, args[0], args[1]);
        return term_func(make_func(make_atom("=", 2), xs, args[2]));
    }
    if (atom == INT_LIN_LE)
    {
        term_t xs = fzn_process_linear(cxt, args[0], args[1]);
        return term_func(make_func(make_atom("<=", 2), xs, args[2]));
    }
    if (atom == INT_LIN_EQ_REIF)
    {
        term_t xs = fzn_process_linear(cxt, args[0], args[1]);
        term_t eq = term_func(make_func(make_atom("=", 2), xs, args[2]));
        return term_func(make_func(make_atom("<->", 2), args[3], eq));
    }
    if (atom == INT_EQ_REIF)
    {
        term_t eq = term_func(make_func(make_atom("=", 2), args[0], args[1]));
        return term_func(make_func(make_atom("<->", 2), args[2], eq));
    }
    if (atom == INT_NE_REIF)
    {
        term_t ne = term_func(make_func(make_atom("!=", 2), args[0], args[1]));
        return term_func(make_func(make_atom("<->", 2), args[2], ne));
    }
    if (atom == INT_LE_REIF)
    {
        term_t le = term_func(make_func(make_atom("<=", 2), args[0], args[1]));
        return term_func(make_func(make_atom("<->", 2), args[2], le));
    }
    if (atom == INT_LT_REIF)
    {
        term_t lt = term_func(make_func(make_atom("<", 2), args[0], args[1]));
        return term_func(make_func(make_atom("<->", 2), args[2], lt));
    }
    if (atom == INT_LIN_LE_REIF)
    {
        term_t xs = fzn_process_linear(cxt, args[0], args[1]);
        term_t eq = term_func(make_func(make_atom("<=", 2), xs, args[2]));
        return term_func(make_func(make_atom("<->", 2), args[3], eq));
    }
    if (atom == ARRAY_BOOL_AND)
    {
        term_t and = fzn_process_fold(cxt, AND, TERM_TRUE, args[0]);
        return term_func(make_func(make_atom("<->", 2), args[1], and));
    }
    if (atom == ARRAY_BOOL_OR)
    {
        term_t or = fzn_process_fold(cxt, make_atom("\\/", 2), TERM_FALSE,
            args[0]);
        term_t b = fzn_process_expr(cxt, args[1]);
        return term_func(make_func(make_atom("<->", 2), args[1], or));
    }

    warning("unknown contraint %s/%zu", atom_name(atom), atom_arity(atom));
    return TERM_TRUE;
}

/*
 * Process a FZN expression.
 */
static term_t fzn_process_expr(context_t cxt, term_t expr)
{
    type_t t = type(expr);
    switch (t)
    {
        case BOOL: case NUM:
            return expr;
        case VAR:
            return expr;
        case FUNC:
            break;
        default:
            warning("cannot translate FZN expression `%s'", show(expr));
            return expr;
    }

    func_t f = func(expr);
    atom_t atom = f->atom;

    if (atom == ARRAY_EMPTY)
        return expr;
    if (atom == ARRAY_ELEM)
    {
        term_t arg = fzn_process_expr(cxt, f->args[0]);
        term_t tail = fzn_process_expr(cxt, f->args[1]);
        return term_func(make_func(ARRAY_ELEM, arg, tail));
    }
    if (atom == SET_EMPTY)
        return expr;
    if (atom == SET_ELEM)
    {
        term_t arg = fzn_process_expr(cxt, f->args[0]);
        term_t tail = fzn_process_expr(cxt, f->args[1]);
        return term_func(make_func(SET_ELEM, arg, tail));
    }
    if (atom == RANGE)
        return expr;
    if (atom == LOOKUP)
    {
        var_t a = var(f->args[0]);
        num_t n = num(f->args[1]);
        var_t x;
        if (!lookupinfo_search(cxt->lookupinfo, f, &x))
        {
            char buf[BUFSIZ];
            snprintf(buf, sizeof(buf)-1, "%s_%zu", a->name, (size_t)n);
            x = make_var(buf);
            cxt->lookupinfo = lookupinfo_insert(cxt->lookupinfo, f, x);
        }
        return term_var(x);
    }

    warning("cannot translate FZN expression `%s'", show(expr));
    return expr;
}

/*
 * Folding.
 */
static term_t fzn_process_fold(context_t cxt, atom_t op, term_t base,
    term_t array)
{
    if (type(array) != FUNC)
        fatal("expected array; found `%s'", show(array));
    func_t f = func(array);
    atom_t atom = f->atom;
    if (atom == ARRAY_EMPTY)
        return base;
    if (atom != ARRAY_ELEM)
        fatal("expected array; found `%s'", show(array));
    term_t elem = f->args[0];
    term_t tail = f->args[1];
    if (type(tail) == FUNC && func(tail)->atom == ARRAY_EMPTY)
        return elem;
    tail = fzn_process_fold(cxt, op, base, tail);
    return term_func(make_func(op, elem, tail));
}

/*
 * Linear.
 */
static term_t fzn_process_linear(context_t cxt, term_t cs, term_t xs)
{
    if (type(cs) != FUNC)
        fatal("expected array; found `%s'", show(cs));
    if (type(xs) != FUNC)
        fatal("expected array; found `%s'", show(xs));
    func_t f = func(cs), g = func(xs);
    if (f->atom != ARRAY_EMPTY && f->atom != ARRAY_ELEM)
        fatal("expected array; found `%s'", show(cs));
    if (g->atom != ARRAY_EMPTY && g->atom != ARRAY_ELEM)
        fatal("expected array; found `%s'", show(xs));
    if (f->atom == ARRAY_EMPTY)
    {
        if (g->atom != ARRAY_EMPTY)
            fatal("mis-matched array lengths");
        return term_int(0);
    }
    term_t c = f->args[0];
    if (type(c) != NUM)
        fatal("expected number; found `%s'", show(c));
    term_t x = g->args[0];
    term_t c_x = term_func(make_func(make_atom("*", 2), c, x));
    cs = f->args[1];
    xs = g->args[1];
    if (type(cs) == FUNC && type(xs) == FUNC &&
            func(cs)->atom == ARRAY_EMPTY && 
            func(xs)->atom == ARRAY_EMPTY)
        return c_x;
    term_t tail = fzn_process_linear(cxt, cs, xs);
    return term_func(make_func(make_atom("+", 2), c_x, tail));
}

/*
 * Domain.
 */
static term_t fzn_process_dom(context_t cxt, term_t x, term_t dom)
{
    if (type(x) != VAR)
        fatal("expected variable; found `%s'", show(x));
    if (type(dom) != FUNC)
        fatal("expected domain; found `%s'", show(dom));
    func_t f = func(dom);
    if (f->atom == RANGE)
    {
        ssize_t lb = (ssize_t)num(f->args[0]);
        ssize_t ub = (ssize_t)num(f->args[1]);
        if (lb > ub)
            fatal("lower-bound %zu is greater than upper bound %zu", lb, ub);
        if (lb == ub)
        {
            term_t eqc = term_func(make_func(make_atom("=", 2), x,
                term_int(lb)));
            return eqc;
        }
        term_t domc = term_func(make_func(make_atom("int_dom", 3), x,
            term_int(lb), term_int(ub)));
        return domc;
#if 0
        term_t lbc = term_func(make_func(make_atom("int_lb", 2), x,
            term_int(lb)));
        term_t r = lbc;
        term_t ubc = term_func(make_func(make_atom("int_lb", 2), x,
            term_int(ub+1)));
        ubc = term_func(make_func(make_atom("not", 1), ubc));
        r = term_func(make_func(AND, r, ubc));
        for (size_t i = lb; i <= ub; i++)
        {
            term_t eqc = term_func(make_func(make_atom("=", 2), x,
                term_int(i)));
            term_t imp = term_func(make_func(make_atom("->", 2), eqc,
                lbc));
            r = term_func(make_func(AND, r, imp));
            size_t j = i + 1;
            term_t lbc_1 = term_func(make_func(make_atom("int_lb", 2), x,
                term_int(j)));
            imp = term_func(make_func(make_atom("->", 2), lbc_1, lbc));
            r = term_func(make_func(AND, r, imp));
            ubc = term_func(make_func(make_atom("not", 1), lbc_1));
            imp = term_func(make_func(make_atom("->", 2), eqc, ubc));
            r = term_func(make_func(AND, r, imp));
            term_t n_lbc = term_func(make_func(make_atom("not", 1), lbc));
            term_t or = term_func(make_func(make_atom("\\/", 2), lbc_1, n_lbc));
            term_t neqc = term_func(make_func(make_atom("not", 1), eqc));
            imp = term_func(make_func(make_atom("->", 2), neqc, or));
            r = term_func(make_func(AND, r, imp));
            lbc = lbc_1;
        }
        return r;
#endif
    }
    if (f->atom == SET_ELEM)
    {
        panic("NYI");
#if 0
        term_t r = (NULL)
        while (true)
        {
            term_t x = f->args[0];
            dom = f->args[1];
#endif
    }
    fatal("expected domain; found `%s'", show(dom));
}

/****************************************************************************/
/* PARSER                                                                   */
/****************************************************************************/

/*
 * Parse a FZN file.
 */
extern term_t fzn_parse(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (file == NULL)
        fatal("unable to open file %s: %s", filename, strerror(errno));
    tokenlist_t tokens = fzn_get_tokens(file);
    fclose(file);
    
    term_t model = TERM_TRUE;
    while (true)
    {
        token_t token = tokens->token;
        switch (token)
        {
            case TOKEN_PREDICATE:
                tokens = fzn_parse_predicate_item(tokens);  
                break;
            case TOKEN_CONSTRAINT:
                tokens = fzn_parse_constraint_item(tokens, &model);
                break;
            case TOKEN_SOLVE:
                tokens = fzn_parse_solve_item(tokens);
                break;
            case TOKEN_VAR:
                tokens = fzn_parse_var_item(tokens, &model);
                break;
            case TOKEN_ARRAY:
                tokens = fzn_parse_var_item(tokens, &model);
                break;
            case TOKEN_EOF:
                return model;
            default:
                fzn_unexpected_token(token, "item start");
        }
    }
}

/*
 * Parse a solve item.
 */
static tokenlist_t fzn_parse_solve_item(tokenlist_t tokens)
{
    tokens = fzn_expect_token(tokens, TOKEN_SOLVE, NULL);
    while (tokens->token == TOKEN_COLONCOLON)
    {
        tokens = tokens->next;
        term_t dummy;
        tokens = fzn_parse_expr(tokens, &dummy);
    }
    token_t token = tokens->token;
    tokens = tokens->next;
    switch (token)
    {
        case TOKEN_SATISFY:
            break;
        case TOKEN_MINIMIZE: case TOKEN_MAXIMIZE:
        {
            term_t dummy;
            tokens = fzn_parse_expr(tokens, &dummy);
            break;
        }
        default:
            fzn_unexpected_token(token, "after `solve'");
    }
    tokens = fzn_expect_token(tokens, ';', NULL);
    return tokens;
}

/*
 * Page a variable item.
 */
static tokenlist_t fzn_parse_var_item(tokenlist_t tokens, term_t *model)
{
    token_t token = tokens->token;
    tokens = tokens->next;
    term_t domain = (term_t)NULL;
    bool array = false;
    term_t lb, ub;
    switch (token)
    {
        case TOKEN_VAR:
            tokens = fzn_parse_typeinst_expr(tokens, &domain);
            break;
        case TOKEN_ARRAY:
            array = true;
            tokens = fzn_expect_token(tokens, '[', NULL);
            tokens = fzn_expect_token(tokens, TOKEN_INT_LIT, &lb);
            tokens = fzn_expect_token(tokens, TOKEN_DOTDOT, NULL);
            tokens = fzn_expect_token(tokens, TOKEN_INT_LIT, &ub);
            tokens = fzn_expect_token(tokens, ']', NULL);
            tokens = fzn_expect_token(tokens, TOKEN_OF, NULL);
            if (tokens->token == TOKEN_VAR)
                tokens = tokens->next;
            tokens = fzn_parse_typeinst_expr(tokens, &domain);
            break;
        default:
            fzn_unexpected_token(token, "var item");
    }
    tokens = fzn_expect_token(tokens, ':', NULL);
    term_t id;
    tokens = fzn_expect_token(tokens, TOKEN_IDENT, &id);
    while (tokens->token == TOKEN_COLONCOLON)
    {
        tokens = tokens->next;
        term_t dummy;
        tokens = fzn_parse_expr(tokens, &dummy);
    }
    if (domain != (term_t)NULL)
    {
        if (!array)
        {
            term_t dom = term_func(make_func(DOM, id, domain));
            *model = term_func(make_func(AND, dom, *model));
        }
        else
        {
            for (size_t i = (size_t)num(lb); i <= (size_t)num(ub); i++)
            {
                term_t lookup = term_func(make_func(LOOKUP, id,
                    term_int(i)));
                term_t dom = term_func(make_func(DOM, lookup, domain));
                *model = term_func(make_func(AND, dom, *model));
            }
        }
    }
    term_t expr = (term_t)NULL;
    if (tokens->token == '=')
    {
        tokens = tokens->next;
        tokens = fzn_parse_expr(tokens, &expr);
        if (!array)
        {
            term_t eq = term_func(make_func(INT_EQ, id, expr));
            *model = term_func(make_func(AND, eq, *model));
        }
        else
        {
            for (size_t i = (size_t)num(lb); i <= (size_t)num(ub); i++)
            {
                if (type(expr) != FUNC)
                    fatal("expected an array expression");
                func_t f = func(expr);
                if (f->atom != ARRAY_ELEM)
                {
                    if (f->atom == ARRAY_EMPTY)
                        fatal("array size mis-match");
                    else
                        fatal("expected an array expression");
                }
                term_t arg = f->args[0];
                expr = f->args[1];
                term_t lookup = term_func(make_func(LOOKUP, id,
                    term_int(i)));
                term_t eq = term_func(make_func(INT_EQ, lookup, arg));
                *model = term_func(make_func(AND, eq, *model));
            }
        }
    }
    tokens = fzn_expect_token(tokens, ';', NULL);
    return tokens;
}

/*
 * Parse a predicate item.
 */
static tokenlist_t fzn_parse_predicate_item(tokenlist_t tokens)
{
    tokens = fzn_expect_token(tokens, TOKEN_IDENT, NULL);
    tokens = fzn_expect_token(tokens, '(', NULL);
    while (true)
    {
        tokens = fzn_parse_predicate_arg(tokens);
        if (tokens->token == ')')
        {
            tokens = tokens->next;
            break;
        }
        if (tokens->token != ',')
            fatal("expected token `,' or `)'; found token `%s'",
                fzn_get_token_name(tokens->token));
        tokens = tokens->next;
    }
    tokens = fzn_expect_token(tokens, ';', NULL);
    return tokens;
}

/*
 * Parse a predicate arg.
 */
static tokenlist_t fzn_parse_predicate_arg(tokenlist_t tokens)
{
    tokens = fzn_expect_token(tokens, TOKEN_PREDICATE, NULL);
    token_t token = tokens->token;
    if (token == TOKEN_VAR)
    {
        tokens = tokens->next;
        tokens = fzn_parse_typeinst_expr(tokens, NULL);
    }
    else if (token == TOKEN_ARRAY)
    {
        tokens = tokens->next;
        tokens = fzn_expect_token(tokens, '[', NULL);
        token = tokens->token;
        tokens = tokens->next;
        switch (token)
        {
            case TOKEN_INT:
                break;
            case TOKEN_INT_LIT:
                tokens = fzn_expect_token(tokens, TOKEN_DOTDOT, NULL);
                tokens = fzn_expect_token(tokens, TOKEN_INT_LIT, NULL);
                break;
            default:
                fzn_unexpected_token(token, "array index type-inst");
        }
        tokens = fzn_expect_token(tokens, TOKEN_OF, NULL);
        if (tokens->token == TOKEN_VAR)
            tokens = tokens->next;
        tokens = fzn_parse_typeinst_expr(tokens, NULL);
    }
    else
        tokens = fzn_parse_typeinst_expr(tokens, NULL);
    tokens = fzn_expect_token(tokens, ':', NULL);
    tokens = fzn_expect_token(tokens, TOKEN_IDENT, NULL);
    return tokens;
}

/*
 * Parse a type-inst expression.
 */
static tokenlist_t fzn_parse_typeinst_expr(tokenlist_t tokens, term_t *domain)
{
    token_t token = tokens->token;
    term_t val = tokens->val;
    tokens = tokens->next;
    switch (token)
    {
        case TOKEN_BOOL: case TOKEN_INT: case TOKEN_FLOAT:
            return tokens;
        case TOKEN_INT_LIT: token_int_lit:
            tokens = fzn_expect_token(tokens, TOKEN_DOTDOT, NULL);
            term_t val_2 = tokens->val;
            tokens = fzn_expect_token(tokens, TOKEN_INT_LIT, NULL);
            if (domain != NULL)
                *domain = term_func(make_func(RANGE, val, val_2));
            return tokens;
        case TOKEN_FLOAT_LIT:
            tokens = fzn_expect_token(tokens, TOKEN_DOTDOT, NULL);
            tokens = fzn_expect_token(tokens, TOKEN_FLOAT_LIT, NULL);
            return tokens;
        case '{': token_set:
        {
            term_t set = (term_t)NULL;
            if (domain != NULL)
                set = term_func(make_func(SET_EMPTY));
            while (true)
            {
                tokens = fzn_expect_token(tokens, TOKEN_INT_LIT, &val);
                if (domain != NULL)
                    set = term_func(make_func(SET_ELEM, val, set));
                if (tokens->token == '}')
                    break;
                tokens = fzn_expect_token(tokens, ',', NULL);
            }
            if (domain != NULL)
                *domain = set;
            tokens = tokens->next;
            return tokens;
        }
        case TOKEN_SET:
            tokens = fzn_expect_token(tokens, TOKEN_OF, NULL);
            token = tokens->token;
            tokens = tokens->next;
            switch (token)
            {
                case TOKEN_INT:
                    return tokens;
                case TOKEN_INT_LIT:
                    goto token_int_lit;
                case '{':
                    goto token_set;
                default:
                    fzn_unexpected_token(token, "after `set of'");
            }
        default:
            fzn_unexpected_token(token, "type-inst expr");
    }
    return tokens;
}

/*
 * Parse a constraint item.
 */
static tokenlist_t fzn_parse_constraint_item(tokenlist_t tokens, term_t *model)
{
    tokens = fzn_expect_token(tokens, TOKEN_CONSTRAINT, NULL);
    term_t id;
    tokens = fzn_expect_token(tokens, TOKEN_IDENT, &id);
    tokens = fzn_expect_token(tokens, '(', NULL);
    term_t args[MAXARGS];
    size_t i;
    for (i = 0; i < MAXARGS; i++)
    {
        term_t arg;
        tokens = fzn_parse_expr(tokens, &arg);
        args[i] = arg;
        if (tokens->token == ')')
        {
            tokens = tokens->next;
            break;
        }
        if (tokens->token != ',')
            fatal("expected token `,' or `)'; found token `%s'",
                fzn_get_token_name(tokens->token));
        tokens = tokens->next;
    }
    if (i == MAXARGS)
        fatal("constraint with too many arguments; maximum is %zu", MAXARGS);
    while (tokens->token == TOKEN_COLONCOLON)
    {
        tokens = tokens->next;
        term_t dummy;
        tokens = fzn_parse_expr(tokens, &dummy);
    }
    tokens = fzn_expect_token(tokens, ';', NULL);
    size_t arity = i + 1;
    term_t cons = term_func(make_func_a(make_atom(var(id)->name, arity),
        args));
    *model = term_func(make_func(AND, *model, cons));
    return tokens;
}

/*
 * Parse an expression.
 */
static tokenlist_t fzn_parse_expr(tokenlist_t tokens, term_t *expr)
{
    token_t token = tokens->token;
    term_t val = tokens->val;
    tokens = tokens->next;
    switch (token)
    {
        case TOKEN_TRUE:
            *expr = TERM_TRUE;
            return tokens;
        case TOKEN_FALSE:
            *expr = TERM_FALSE;
            return tokens;
        case TOKEN_INT_LIT:
            if (tokens->token == TOKEN_DOTDOT)
            {
                tokens = tokens->next;
                term_t hi;
                tokens = fzn_expect_token(tokens, TOKEN_INT_LIT, &hi);
                *expr = term_func(make_func(RANGE, val, hi));
                return tokens;
            }
            *expr = val;
            return tokens;
        case TOKEN_FLOAT_LIT:
            *expr = val;
            return tokens;
        case TOKEN_STRING_LIT:
            *expr = val;
            return tokens;
        case '{':
        {
            term_t set = term_func(make_func(SET_EMPTY));
            if (tokens->token == '}')
            {
                tokens = tokens->next;
                *expr = set;
                return tokens;
            }
            while (true)
            {
                term_t elem;
                tokens = fzn_parse_expr(tokens, &elem);
                set = term_func(make_func(SET_ELEM, elem, set));
                if (tokens->token == '}')
                    break;
                tokens = fzn_expect_token(tokens, ',', NULL);
            }
            tokens = tokens->next;
            *expr = set;
            return tokens;
        }
        case '[':
        {
            term_t array = term_func(make_func(ARRAY_EMPTY));
            if (tokens->token == ']')
            {
                tokens = tokens->next;
                *expr = array;
                return tokens;
            }
            while (true)
            {
                term_t elem;
                tokens = fzn_parse_expr(tokens, &elem);
                array = term_func(make_func(ARRAY_ELEM, elem, array));
                if (tokens->token == ']')
                    break;
                tokens = fzn_expect_token(tokens, ',', NULL);
            }
            tokens = tokens->next;
            *expr = array;
            return tokens;
        }
        case TOKEN_IDENT:
        {
            switch (tokens->token)
            {
                case '[':
                {
                    tokens = tokens->next;
                    term_t idx;
                    tokens = fzn_expect_token(tokens, TOKEN_INT_LIT, &idx);
                    tokens = fzn_expect_token(tokens, ']', NULL);
                    *expr = term_func(make_func(LOOKUP, val, idx));
                    break;
                }
                case '(':
                {
                    *expr = TERM_NIL;
                    tokens = tokens->next;
                    while (true)
                    {
                        term_t dummy;
                        tokens = fzn_parse_expr(tokens, &dummy);
                        if (tokens->token == ')')
                        {
                            tokens = tokens->next;
                            break;
                        }
                        if (tokens->token != ',')
                            fatal("expected token `,' or `)'; found token `%s'",
                                fzn_get_token_name(tokens->token));
                        tokens = tokens->next;
                    }
                    break;
                }
                default:
                    *expr = val;
                    break;
            }
            return tokens;
        }
        default:
            fzn_unexpected_token(token, "expr");
    }
}

/*
 * Expect a token.
 */
static tokenlist_t fzn_expect_token(tokenlist_t tokens, token_t t, term_t *val)
{
    token_t token = tokens->token;
    if (token != t)
        fatal("expected token `%s'; found token `%s'",
            fzn_get_token_name(t), fzn_get_token_name(token));
    if (val != NULL)
        *val = tokens->val;
    tokens = tokens->next;
    return tokens;
}

/*
 * Unexpected token.
 */
static void fzn_unexpected_token(token_t token, const char *where)
{
    fatal("unexpected token `%s' (%s)", fzn_get_token_name(token), where);
}


/*
 * Get all tokens.
 */
static tokenlist_t fzn_get_tokens(FILE *file)
{
    tokenlist_t tokens = NULL, curr = NULL;
    varset_t vars = varset_init();
    token_t token = TOKEN_ERROR;
    while (token != TOKEN_EOF)
    {
        term_t val;
        token = fzn_get_token(file, &vars, &val);
        if (token == TOKEN_ERROR)
            fatal("bad token");
        tokenlist_t node = (tokenlist_t)gc_malloc(sizeof(struct tokenlist_s));
        node->token = token;
        node->val = val;
        node->next = NULL;
        if (curr != NULL)
            curr->next = node;
        else
            tokens = node;
        curr = node;
        debug("TOKEN %s [%d] = %s", fzn_get_token_name(token), token,
            show(val));
    }
    return tokens;
}

/*
 * Get a token.
 */
static token_t fzn_get_token(FILE *file, varset_t *vars, term_t *val)
{
    char c = EOF;
    if (val != NULL)
        *val = TERM_NIL;
fzn_get_token_start:
    while (!feof(file) && isspace(c = getc(file)))
        ;
    if (c == EOF)
        return TOKEN_EOF;
    switch (c)
    {
        case '%':
            while (!feof(file) && (c = getc(file)) != '\n')
                ;
            goto fzn_get_token_start;
        case ';': case '(': case ')': case '[': case ']': case '{': case '}':
        case ',': case '=':
            return c;
        case ':':
        {
            c = getc(file);
            if (c != ':')
            {
                ungetc(c, file);
                return ':';
            }
            return TOKEN_COLONCOLON;
        }
        case '.':
        {
            c = getc(file);
            if (c == '.')
                return TOKEN_DOTDOT;
            else
                fatal("expected `.' after `.'");
        }
        case '-':
            ungetc(c, file);
            return fzn_get_num_token(file, val);
        case '\"':
        {
            char buf[BUFSIZ+1];
            size_t i;
            for (i = 0; i < BUFSIZ && (c = getc(file)) != '\"' &&
                    c != EOF && c != '\n'; i++)
                buf[i] = c;
            if (c != '\"')
                fatal("unclosed string");
            buf[i] = '\0';
            if (val != NULL)
                *val = term_string(make_string(buf));
            return TOKEN_STRING_LIT;
        }
        default:
            break;
    }

    if (c >= '0' && c <= '9')
    {
        ungetc(c, file);
        return fzn_get_num_token(file, val);
    }

    if (c == '_' || isalpha(c))
    {
        char buf[BUFSIZ+1];
        buf[0] = c;
        size_t i;
        for (i = 1; i < BUFSIZ && (c = getc(file)) != '\"' &&
                (isalnum(c) || c == '_'); i++)
            buf[i] = c;
        if (i == BUFSIZ)
            fatal("identifier is too big");
        ungetc(c, file);
        buf[i] = '\0';
        struct name_s key = {buf, 0};
        struct name_s *entry = bsearch(&key, names,
            sizeof(names)/sizeof(struct name_s), sizeof(struct name_s),
            name_compare);
        if (entry != NULL)
            return entry->token;
        var_t x;
        if (!varset_search(*vars, buf, &x))
        {
            x = make_var(buf);
            *vars = varset_insert(*vars, (char *)x->name, x);
        }
        if (val != NULL)
            *val = term_var(x);
        return TOKEN_IDENT;
    }

    fatal("unexpected character `%c' in input", c);
}

/*
 * Get a number token.
 */
static token_t fzn_get_num_token(FILE *file, term_t *val)
{
    char buf[TOKEN_MAXLEN+1];
    size_t len = 0;
    token_t type = TOKEN_INT_LIT;
    char c;

    if ((c = getc(file)) == '-')
        buf[len++] = '-';
    else 
        ungetc(c, file);
    while (isdigit(c = getc(file)))
    {
        buf[len++] = c;
        if (len >= TOKEN_MAXLEN)
            fatal("number is too big");
    }

    /*
     * XXX: No float support becuase we cannot use getc/ungetc to build a
     *      lexer that requires a look-ahead of 2.  We need this to
     *      handle ranges likes "3..20".
     */
#if 0
    if (c == '.')
    {
        type = TOKEN_FLOAT_LIT;
        buf[len++] = c;
        if (len >= TOKEN_MAXLEN)
            return TOKEN_ERROR;
        while (isdigit(c = getc(file)))
        {
            buf[len++] = c;
            if (len >= TOKEN_MAXLEN)
                return TOKEN_ERROR;
        }
    }
    if (c == 'e' || c == 'E')
    {
        type = TOKEN_FLOAT_LIT;
        buf[len++] = c;
        if (len >= TOKEN_MAXLEN)
            return TOKEN_ERROR;
    }
    else
        goto token_getnum_end;

    if (c == '-')
    {
        buf[len++] = c;
        if (len >= TOKEN_MAXLEN)
            return TOKEN_ERROR;
    }

    while (isdigit(c = getc(file)))
    {
        buf[len++] = c;
        if (len >= TOKEN_MAXLEN)
            return TOKEN_ERROR;
    }

token_getnum_end:
#endif
    ungetc(c, file);
    buf[len] = '\0';
    errno = 0;
    double d = strtod(buf, NULL);
    if (errno != 0)
        fatal("failed to convert string \"%s\" into a number: %s", buf,
            strerror(errno));
    term_t t = term_num(d);
    if (val != NULL)
        *val = t;
    return type;
}

/*
 * Convert a token to a string.
 */
static const char *fzn_get_token_name(token_t token)
{
    switch (token)
    {
        case ';':
            return ";";
        case '(':
            return "(";
        case ')':
            return ")";
        case '[':
            return "[";
        case ']':
            return "]";
        case '{':
            return "{";
        case '}':
            return "}";
        case '=':
            return "=";
        case ',':
            return ",";
        case ':':
            return ":";
        case TOKEN_ARRAY:
            return "array";
        case TOKEN_BOOL:
            return "bool";
        case TOKEN_CONSTRAINT:
            return "constraint";
        case TOKEN_FALSE:
            return "false";
        case TOKEN_FLOAT:
            return "float";
        case TOKEN_INT:
            return "int";
        case TOKEN_MAXIMIZE:
            return "maximize";
        case TOKEN_MINIMIZE:
            return "minimize";
        case TOKEN_OF:
            return "of";
        case TOKEN_PREDICATE:
            return "predicate";
        case TOKEN_SATISFY:
            return "satisfy";
        case TOKEN_SET:
            return "set";
        case TOKEN_SOLVE:
            return "solve";
        case TOKEN_TRUE:
            return "true";
        case TOKEN_VAR:
            return "var";
        case TOKEN_DOTDOT:
            return "..";
        case TOKEN_COLONCOLON:
            return "::";
        case TOKEN_INT_LIT:
            return "<INT>";
        case TOKEN_FLOAT_LIT:
            return "<FLOAT>";
        case TOKEN_STRING_LIT:
            return "<STRING>";
        case TOKEN_IDENT:
            return "<IDENT>";
        case TOKEN_EOF:
            return "<EOF>";
        case TOKEN_ERROR:
            return "<ERROR>";
        default:
            return "<UNKNOWN>";
    }
}

