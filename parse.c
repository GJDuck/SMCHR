/*
 * parse.c
 * Copyright (C) 2012 National University of Singapore
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

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "names.h"
#include "op.h"
#include "parse.h"
#include "term.h"

#define MAX_ARGS        1024

/*
 * Tokens.
 */
enum token_e
{
    TOKEN_END = 1000,
    TOKEN_ERROR,
    TOKEN_NIL,
    TOKEN_BOOLEAN,
    TOKEN_NUMBER,
    TOKEN_ATOM,
    TOKEN_STRING,
    TOKEN_VARIABLE,
    TOKEN_OP,
    TOKEN_LEQ,
    TOKEN_GEQ,
    TOKEN_NEQ,
    TOKEN_AND,
    TOKEN_OR,
    TOKEN_NOT,
    TOKEN_IMPLIES,
    TOKEN_IFF,
    TOKEN_XOR,
};
typedef enum token_e token_t;
#define TOKEN_MAXLEN    256

/*
 * Special name entry.
 */
struct name_s
{
    char *name;
    token_t token;
    term_t val;
};
typedef struct name_s *name_t;
static int name_compare(const void *a, const void *b)
{
    const struct name_s *a1 = (const struct name_s *)a;
    const struct name_s *b1 = (const struct name_s *)b;
    return strcmp(a1->name, b1->name);
}

static struct name_s names[] =
{
    {"!=",    TOKEN_NEQ,     (term_t)NULL},
    {"*",     '*',           (term_t)NULL},
    {"+",     '+',           (term_t)NULL},
    {"-",     '-',           (term_t)NULL},
    {"->",    TOKEN_IMPLIES, (term_t)NULL},
    {"/",     '/',           (term_t)NULL},
    {"/\\",   TOKEN_AND,     (term_t)NULL},
    {"<",     '<',           (term_t)NULL},
    {"<->",   TOKEN_IFF,     (term_t)NULL},
    {"<=",    TOKEN_LEQ,     (term_t)NULL},
    {"=",     '=',           (term_t)NULL},
    {">",     '>',           (term_t)NULL},
    {">=",    TOKEN_GEQ,     (term_t)NULL},
    {"\\/",   TOKEN_OR,      (term_t)NULL},
    {"^",     '^',           (term_t)NULL},
    {"false", TOKEN_BOOLEAN, (term_t)NULL},
    {"inf",   TOKEN_NUMBER,  (term_t)NULL},
    {"nil",   TOKEN_NIL,     (term_t)NULL},
    {"not",   TOKEN_NOT,     (term_t)NULL},
    {"true",  TOKEN_BOOLEAN, (term_t)NULL},
    {"xor",   TOKEN_XOR,     (term_t)NULL},
};

static name_t name_lookup(char *name)
{
    struct name_s key = {name, 0, 0};
    name_t entry = bsearch(&key, names, sizeof(names)/sizeof(struct name_s),
        sizeof(struct name_s), name_compare);
    return entry;
}

/*
 * Parsing context.
 */
#define TOKEN_NONE              (-1)
struct context_s
{
    const char *file;                   // Filename
    size_t line;                        // Line number.
    varset_t vars;                      // Variables.
    opinfo_t opinfo;                    // Extra operators.
    char *str;                          // Input string.
    token_t peeked_token;               // Last peeked token.
    term_t peeked_val;                  // Last peeked token val.
    char peeked_str[TOKEN_MAXLEN+1];    // Last peeked token str.
};
typedef struct context_s *context_t;

/*
 * Prototypes.
 */
static bool parse_term_op(context_t cxt, term_t *val, unsigned priority);
static bool parse_maybe_op(token_t token);
static bool parse_term_head(context_t cxt, term_t *val);
static bool token_expect(context_t cxt, token_t tok);
static token_t token_get(context_t cxt, term_t *val, char *tokstr);
static token_t token_peek(context_t cxt, term_t *val, char *tokstr);
static void token_getcomment(context_t cxt);
static token_t token_getop(context_t cxt);
static token_t token_getname(context_t cxt, term_t *val);
static token_t token_getstring(context_t cxt, term_t *val);
static token_t token_getnum(context_t cxt, term_t *val);
static char *token_readstring(context_t cxt, char end);
static const char *token_getstr(token_t tok);

#define parse_error(cxt, format, ...)                                   \
    error("(%s: %zu) parse error: " format, (cxt)->file, (cxt)->line,   \
        ## __VA_ARGS__)

/*
 * Is operator character?
 */
static bool isop(char c)
{
    switch (c)
    {
        case '~': case '!': case '#': case '$': case '%': case '^': case '&':
        case '*': case '-': case '+': case '=': case '/': case '?': case '|':
        case '<': case '>': case '\\': case ':':
            return true;
        default:
            return false;
    }
}

/*
 * Initialise the parser.
 */
extern void parse_init(void)
{
    name_t entry;
    entry = name_lookup("false");
    entry->val = term_boolean(make_boolean(false));
    entry = name_lookup("inf");
    entry->val = term_num(make_num(1.0/0.0));
    entry = name_lookup("nil");
    entry->val = term_nil(make_nil());
    entry = name_lookup("true");
    entry->val = term_boolean(make_boolean(true));
}

/*
 * Parse a term.
 */
extern term_t parse_term(const char *filename, size_t *line, opinfo_t opinfo,
    const char *str, const char **end, varset_t *vars)
{
    struct context_s cxt_0;
    context_t cxt = &cxt_0;
    cxt->file = filename;
    cxt->line = *line;
    if (vars == NULL)
        cxt->vars = varset_init();
    else
        cxt->vars = *vars;
    cxt->opinfo = opinfo;
    cxt->str = (char *)str;
    cxt->peeked_token = TOKEN_NONE;
    cxt->peeked_val = TERM_NIL;

    term_t val;
    const char *end0;
    if (end == NULL)
        end = &end0;
    if (token_peek(cxt, NULL, NULL) == TOKEN_END)
        val = (term_t)NULL;
    else if (!parse_term_op(cxt, &val, UINT32_MAX))
        val = (term_t)NULL;
    else if (!token_expect(cxt, TOKEN_END))
        val = (term_t)NULL;
    else
    {
        if (vars != NULL)
            *vars = cxt->vars;
    }
    *end = cxt->str;
    *line = cxt->line;
    return val;
}

/*
 * Parse a term.
 */
static bool parse_term_op(context_t cxt, term_t *val, unsigned priority)
{
    term_t lval;
    if (!parse_term_head(cxt, &lval))
        return false;

    do
    {
        char *op_name = (char *)gc_malloc(TOKEN_MAXLEN+1);
        token_t tok = token_peek(cxt, NULL, op_name);
        
        unsigned op_priority;
        assoc_t op_assoc;

        if (!parse_maybe_op(tok) ||
            !binop_lookup(cxt->opinfo, op_name, &op_assoc, &op_priority, NULL,
                NULL) ||
            op_priority > priority)
        {
            *val = lval;
            gc_free(op_name);
            return true;
        }

        if (op_priority == priority)
        {
            switch (op_assoc)
            {
                case YFX:
                    *val = lval;
                    return true;
                case XFY:
                    break;
                case XFX:
                    parse_error(cxt, "operator `!y%s!d' associativity error",
                        op_name);
                    gc_free(op_name);
                    return false;
            }
        }

        if (!token_expect(cxt, tok))
        {
            gc_free(op_name);
            return false;
        }
        term_t rval;
        if (!parse_term_op(cxt, &rval, op_priority))
        {
            gc_free(op_name);
            return false;
        }

        atom_t atom = make_atom(gc_strdup(op_name), 2);
        gc_free(op_name);
        func_t f = make_func(atom, lval, rval);
        lval = term_func(f);
    }
    while (true);
}

/*
 * Tests if a token *might* be an operator.
 */
static bool parse_maybe_op(token_t token)
{
    switch ((int)token)
    {
        case '+': case '-': case '*': case '/': case '<': case '>': case '=':
        case '^': case TOKEN_NEQ: case TOKEN_IMPLIES: case TOKEN_AND:
        case TOKEN_IFF: case TOKEN_LEQ: case TOKEN_GEQ: case TOKEN_OR:
        case TOKEN_XOR: case TOKEN_OP: case TOKEN_NOT: case TOKEN_VARIABLE:
            return true;
        default:
            return false;
    }
}

/*
 * Parse a term (no operators).
 */
static bool parse_term_head(context_t cxt, term_t *val)
{
    char *tokstr = (char *)gc_malloc(TOKEN_MAXLEN+1);
    term_t tokval;
    token_t tok = token_get(cxt, &tokval, tokstr);

    unsigned priority;
    if (parse_maybe_op(tok) &&
        unop_lookup(cxt->opinfo, tokstr, &priority, NULL))
    {
        term_t lval;
        if (!parse_term_op(cxt, &lval, priority))
        {
            gc_free(tokstr);
            return false;
        }
        atom_t atom = make_atom(gc_strdup(tokstr), 1);
        func_t f = make_func(atom, lval);
        tokval = term_func(f);
        *val = tokval;
        gc_free(tokstr);
        return true;
    }

    bool ok = true;
    term_t *args = NULL;
    switch ((int)tok)
    {
        case '(':
            if (!parse_term_op(cxt, val, UINT32_MAX) || !token_expect(cxt, ')'))
                ok = false;
            break;
        case TOKEN_NIL: case TOKEN_BOOLEAN: case TOKEN_ATOM: case TOKEN_STRING:
        case TOKEN_NUMBER:
            *val = tokval;
            break;
        case TOKEN_VARIABLE:
        {
            if (token_peek(cxt, NULL, NULL) != '(')
            {
                // Really is a variable:
                *val = tokval;
                break;
            }
            // Otherwise this is a functor:
            var_t x = var(tokval);
            atom_t atom = make_atom(x->name, 0);
            if (!token_expect(cxt, '('))
            {
                ok = false;
                break;
            }
            args = (term_t *)gc_malloc(MAX_ARGS * sizeof(term_t));
            uint_t a = 0;
            if (token_peek(cxt, NULL, NULL) == ')')
                token_get(cxt, NULL, NULL);
            else
            {
                while (true)
                {
                    ok = parse_term_op(cxt, &tokval, UINT32_MAX);
                    if (!ok)
                        break;
                    if (a >= MAX_ARGS)
                    {
                        parse_error(cxt, "too many arguments; maximum is %zu",
                            MAX_ARGS);
                        ok = false;
                        break;
                    }
                    args[a++] = tokval;
                    tok = token_get(cxt, NULL, tokstr);
                    if (tok == ',')
                        continue;
                    if (tok == ')')
                        break;
                    parse_error(cxt, "expected token `,' or `)'; got token "
                        "`%s'", tokstr);
                    ok = false;
                    break;
                }
                if (!ok)
                    break;
            }
            atom = atom_set_arity(atom, a);
            func_t f = make_func_a(atom, args);
            tokval = term_func(f);
            *val = tokval;
            break;
        }
        default:
            if (tok != TOKEN_ERROR)
                parse_error(cxt, "unexpected token `%s'", tokstr);
            ok = false;
            break;
    }
    gc_free(tokstr);
    gc_free(args);
    return ok;
}

/*
 * Get an expected token.
 */
static bool token_expect(context_t cxt, token_t tok)
{
    char tokstr[TOKEN_MAXLEN+1];
    token_t tok1 = token_get(cxt, NULL, tokstr);
    if (tok1 != tok)
    {
        if (tok1 != TOKEN_ERROR)
            parse_error(cxt, "expected token `%s'; got token `%s'",
                token_getstr(tok), tokstr);
        return false;
    }
    return true;
}

/*
 * Get a token from a string.
 */
static token_t token_get(context_t cxt, term_t *val, char *tokstr)
{
    term_t dummy;
    if (val == NULL)
        val = &dummy;
    
    if (cxt->peeked_token != TOKEN_NONE)
    {
        token_t token = cxt->peeked_token;
        *val = cxt->peeked_val;
        cxt->peeked_token = TOKEN_NONE;
        if (tokstr != NULL)
            strcpy(tokstr, cxt->peeked_str);
        return token;
    }

token_get_start: {}
    token_t token;
    char *start = cxt->str;
    char c = *cxt->str;
    switch (c)
    {
        case '\0':
            token = TOKEN_END;
            goto token_get_exit;
        case ';':
            cxt->str++;
            token = TOKEN_END;
            goto token_get_exit;
        case '/':
            if (cxt->str[1] == '/')
            {
                cxt->str += 2;
                while ((c = *cxt->str) != '\n' && c != '\0')
                    cxt->str++;
                goto token_get_start;
            }
            if (cxt->str[1] == '*')
            {
                cxt->str += 2;
                token_getcomment(cxt);
                goto token_get_start;
            }
            break;
        case '\n':
            cxt->line++;
            // Fallthrough
        case ' ': case '\t': case '\r':
            cxt->str++;
            goto token_get_start;
        case '(': case ')': case ',':
            cxt->str++;
            token = (token_t)c;
            goto token_get_exit;
        case '_': case '\'':
            token = token_getname(cxt, val);
            goto token_get_exit;
        case '@':
            cxt->str++;
            token = token_getname(cxt, val);
            if (token != TOKEN_VARIABLE)
            {
                token = TOKEN_ERROR;
                goto token_get_exit;
            }
            atom_t atom = make_atom(var(*val)->name, 0);
            *val = term_atom(atom);
            goto token_get_exit;
        case '"':
            token = token_getstring(cxt, val);
            goto token_get_exit;
        default:
            break;
    }

    if (isdigit(c))
        token = token_getnum(cxt, val);
    else if (isalpha(c))
        token = token_getname(cxt, val);
    else if (isop(c))
        token = token_getop(cxt);
    else
    {
        if (isprint(c))
            parse_error(cxt, "unexpected character `!y%c!d'", c);
        else
            parse_error(cxt, "unexpected character (!y0x%.2X!d)", c);
        token = TOKEN_ERROR;
    }

token_get_exit: {}

    size_t len = cxt->str - start;
    if (tokstr != NULL)
    {
        len = (len > TOKEN_MAXLEN? TOKEN_MAXLEN: len);
        strncpy(tokstr, start, len);
        tokstr[len] = '\0';
    }
    if (token == TOKEN_ERROR)
    {
        if (len == 0)
        {
            len++;
            if (tokstr != NULL)
            {
                tokstr[0] = cxt->str[0];
                tokstr[1] = '\0';
            }
        }
        parse_error(cxt, "bad token `%.*s'", len, start);
    }
    if (token == TOKEN_END && tokstr != NULL)
        strcpy(tokstr, "<END>");

    return token;
}

/*
 * Peek at a token.
 */
static token_t token_peek(context_t cxt, term_t *val, char *tokstr)
{
    if (cxt->peeked_token != TOKEN_NONE)
    {
        if (val != NULL)
            *val = cxt->peeked_val;
        if (tokstr != NULL)
            strcpy(tokstr, cxt->peeked_str);
        return cxt->peeked_token;
    }
    cxt->peeked_token = token_get(cxt, &cxt->peeked_val, cxt->peeked_str);
    if (val != NULL)
        *val = cxt->peeked_val;
    if (tokstr != NULL)
        strcpy(tokstr, cxt->peeked_str);
    return cxt->peeked_token;
}

/*
 * Get (consume) a comment.
 */
static void token_getcomment(context_t cxt)
{
    size_t depth = 0;
    size_t line = cxt->line;
    while (true)
    {
        char c = *cxt->str++;
        switch (c)
        {
            case '\0':
                parse_error(cxt, "unclosed `/* .. */' comment; starting on "
                    "line %zu", line);
                return;
            case '\n':
                cxt->line++;
                break;
            case '*':
                if (*cxt->str == '/')
                {
                    cxt->str++;
                    if (depth == 0)
                        return;
                    depth--;
                }
                break;
            case '/':
                if (*cxt->str == '*')
                {
                    cxt->str++;
                    depth++;
                }
                break;
            default:
                break;
        }
    }
}

/*
 * Get an operator token.
 */
static token_t token_getop(context_t cxt)
{
    char buf0[TOKEN_MAXLEN+1];
    char *buf = buf0, *str = cxt->str;
    size_t len = 0;
    token_t token = TOKEN_ERROR;

    while (isop(*str))
    {
        buf[len++] = *str++;
        if (len >= TOKEN_MAXLEN)
            return TOKEN_ERROR;
        buf[len] = '\0';
        name_t entry = name_lookup(buf);
        if (entry != NULL)
        {
            token = entry->token;
            cxt->str = str;
        }
        else if (binop_lookup(cxt->opinfo, buf, NULL, NULL, NULL, NULL))
        {
            token = TOKEN_OP;
            cxt->str = str;
        }
    }
    return token;
}

/*
 * Get a name token.
 */
static token_t token_getname(context_t cxt, term_t *val)
{
    char buf0[TOKEN_MAXLEN+1];
    char *buf = buf0;
    size_t len = 0;

    if (*cxt->str == '\'')
    {
        cxt->str++;
        buf = token_readstring(cxt, '\'');
        if (buf == NULL)
            return TOKEN_ERROR;
    }
    else
    {
        while (isalnum(*cxt->str) || *cxt->str == '_')
        {
            buf[len++] = *cxt->str++;
            if (len >= TOKEN_MAXLEN)
                return TOKEN_ERROR;
        }
        buf[len] = '\0';
    }

    // Check for anonymous variable:
    if (buf[0] == '_' && buf[1] == '\0')
    {
        var_t v = make_var(NULL);
        term_t t = term_var(v);
        *val = t;
        return TOKEN_VARIABLE;
    }

    // Check for special name:
    name_t entry = name_lookup(buf);
    if (entry != NULL)
    {
        *val = entry->val;
        return entry->token;
    }

    // Check for variable:
    term_t t;
    if (varset_search(cxt->vars, buf, &t))
    {
        *val = t;
        return TOKEN_VARIABLE;
    }
    
    // By default, create a new variable:
    char *name;
    if (buf == buf0)
    {
        name = (char *)gc_malloc(len+1);
        strcpy(name, buf);
    }
    else
        name = buf;
    register_name(name);
    var_t v = make_var(name);
    t = term_var(v);
    cxt->vars = varset_insert(cxt->vars, name, t);
    *val = t;
    return TOKEN_VARIABLE;
}

/*
 * Get a string token.
 */
static token_t token_getstring(context_t cxt, term_t *val)
{
    if (*cxt->str++ != '"')
        return TOKEN_ERROR;
    char *buf = token_readstring(cxt, '"');
    if (buf == NULL)
        return TOKEN_ERROR;
    size_t len = strlen(buf);
    str_t string = (str_t)gc_malloc(sizeof(struct str_s)+len+1);
    string->len = len;
    strcpy(string->chars, buf);
    term_t t = term_string(string);
    *val = t;
    return TOKEN_STRING;
}

/*
 * Get a number token.
 */
static token_t token_getnum(context_t cxt, term_t *val)
{
    char buf[TOKEN_MAXLEN+1];
    size_t len = 0;

    while (isdigit(*cxt->str))
    {
        buf[len++] = *cxt->str++;
        if (len >= TOKEN_MAXLEN)
            return TOKEN_ERROR;
    }

    if (*cxt->str == '.')
    {
        buf[len++] = *cxt->str++;
        if (len >= TOKEN_MAXLEN)
            return TOKEN_ERROR;
        while (isdigit(*cxt->str))
        {
            buf[len++] = *cxt->str++;
            if (len >= TOKEN_MAXLEN)
                return TOKEN_ERROR;
        }
    }

    if (*cxt->str == 'e' || *cxt->str == 'E')
    {
        buf[len++] = *cxt->str++;
        if (len >= TOKEN_MAXLEN)
            return TOKEN_ERROR;
    }
    else
        goto token_getnum_end;

    if (*cxt->str == '-')
    {
        buf[len++] = *cxt->str++;
        if (len >= TOKEN_MAXLEN)
            return TOKEN_ERROR;
    }

    while (isdigit(*cxt->str))
    {
        buf[len++] = *cxt->str++;
        if (len >= TOKEN_MAXLEN)
            return TOKEN_ERROR;
    }

token_getnum_end:

    buf[len] = '\0';
    errno = 0;
    double d = strtod(buf, NULL);
    if (errno != 0)
        return TOKEN_ERROR;
    term_t t = term_num(d);
    *val = t;
    return TOKEN_NUMBER;
}

/*
 * Read a string.
 */
static char *token_readstring(context_t cxt, char end)
{
    size_t size = 128;
    size_t len = 0;
    char *buf = (char *)gc_malloc(size+1);
    char c;

    while (*cxt->str != end)
    {
        if (!isprint(*cxt->str))
            return NULL;
        if (len >= size)
        {
            size *= 2;
            buf = (char *)gc_realloc(buf, size);
        }
        c = *cxt->str;
        buf[len++] = *cxt->str++;
        if (c == '\\')
        {
            c = *cxt->str++;
            switch (c)
            {
                case '\0':
                    return NULL;
                case '0':
                    buf[len-1] = '\0';
                    break;
                case 'n':
                    buf[len-1] = '\n';
                    break;
                case 'r':
                    buf[len-1] = '\r';
                    break;
                case 't':
                    buf[len-1] = '\r';
                    break;
                case 'a':
                    buf[len-1] = '\a';
                    break;
                case 'b':
                    buf[len-1] = '\b';
                    break;
                case 'f':
                    buf[len-1] = '\f';
                    break;
                case 'x':
                {
                    char tmp[3];
                    tmp[0] = *cxt->str++;
                    if (!isxdigit(tmp[0]))
                        return NULL;
                    tmp[1] = *cxt->str++;
                    if (!isxdigit(tmp[1]))
                        return NULL;
                    tmp[2] = '\0';
                    unsigned x;
                    if (sscanf(tmp, "%x", &x) != 1)
                        return NULL;
                    buf[len-1] = (char)x;
                    break;
                }
                default:
                    if (c == '\n')
                        cxt->line++;
                    buf[len-1] = c;
                    break;
            }
        }
    }

    buf[len] = '\0';
    buf = gc_realloc(buf, len+1);
    cxt->str++;
    return buf;
}

/*
 * Convert a token into a string.
 */
static const char *token_getstr(token_t tok)
{
    switch ((int)tok)
    {
        case '*':
            return "*";
        case '+':
            return "+";
        case '-':
            return "-";
        case '/':
            return "/";
        case '<':
            return "<";
        case '=':
            return "=";
        case '>':
            return ">";
        case '^':
            return "^";
        case '(':
            return "(";
        case ')':
            return ")";
        case ',':
            return ",";
        case TOKEN_END:
            return "<END>";
        case TOKEN_ERROR:
            return "<ERROR>";
        case TOKEN_NIL:
            return "nil";
        case TOKEN_BOOLEAN:
            return "<BOOLEAN>";
        case TOKEN_NUMBER:
            return "<NUMBER>";
        case TOKEN_STRING:
            return "<STRING>";
        case TOKEN_VARIABLE:
            return "<VARIABLE>";
        case TOKEN_LEQ:
            return "<=";
        case TOKEN_GEQ:
            return ">=";
        case TOKEN_NEQ:
            return "!=";
        case TOKEN_AND:
            return "/\\";
        case TOKEN_OR:
            return "\\/";
        case TOKEN_IMPLIES:
            return "->";
        case TOKEN_IFF:
            return "<->";
        case TOKEN_XOR:
            return "xor";
        default:
            return "<UNKNOWN>";
    }
}

