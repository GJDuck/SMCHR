/*
 * cons.c
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

#include "show.h"
#include "solver.h"

/*
 * Prototypes.
 */
static char *show_buf_var_op_var(char *start, char *end, var_t x, char *op,
    var_t y);
static char *show_buf_var_op_num(char *start, char *end, var_t x, char *op,
    num_t c);

/*
 * Write a constraint.
 */
extern char *solver_show_buf_cons(char *start, char *end, cons_t c)
{
    switch (c->sym->type)
    {
        case X_CMP_Y:
        {
            var_t x = var(c->args[X]);
            var_t y = var(c->args[Y]);
            char *cmp = (c->sym == EQ? "=": ">");
            return show_buf_var_op_var(start, end, x, cmp, y);
        }
        case X_CMP_C:
        {
            var_t x = var(c->args[X]);
            num_t k = num(c->args[Y]);
            char *cmp = (c->sym == EQ_C? "=": ">");
            return show_buf_var_op_num(start, end, x, cmp, k);
        }
        case X_EQ_Y_OP_Z:
        {
            var_t x = var(c->args[X]);
            var_t y = var(c->args[Y]);
            var_t z = var(c->args[Z]);
            char *op = (c->sym == EQ_PLUS? "+": "*");
            start = show_buf_var(start, end, x);
            start = show_buf_str(start, end, " = ");
            return show_buf_var_op_var(start, end, y, op, z);         
        }
        case X_EQ_Y_OP_C:
        {
            var_t x = var(c->args[X]);
            var_t y = var(c->args[Y]);
            num_t k = num(c->args[Z]);
            char *op = (c->sym == EQ_PLUS_C? "+": "*");
            start = show_buf_var(start, end, x);
            start = show_buf_str(start, end, " = ");
            return show_buf_var_op_num(start, end, y, op, k);
        }
        default:
        {
            sym_t sym = c->sym;
            start = show_buf_name(start, end, sym->name);
            start = show_buf_char(start, end, '(');
            for (size_t i = 0; i < sym->arity; i++)
            {
                start = show_buf(start, end, c->args[i]);
                if (i < sym->arity-1)
                    start = show_buf_char(start, end, ',');
            }
            start = show_buf_char(start, end, ')');
            return start;
        }
    }
}

/*
 * Write x OP y
 */
static char *show_buf_var_op_var(char *start, char *end, var_t x, char *op,
    var_t y)
{
    start = show_buf_var(start, end, x);
    start = show_buf_char(start, end, ' ');
    start = show_buf_str(start, end, op);
    start = show_buf_char(start, end, ' ');
    start = show_buf_var(start, end, y);
    return start;
}

/*
 * Write x OP c
 */
static char *show_buf_var_op_num(char *start, char *end, var_t x, char *op,
    num_t c)
{
    start = show_buf_var(start, end, x);
    start = show_buf_char(start, end, ' ');
    start = show_buf_str(start, end, op);
    start = show_buf_char(start, end, ' ');
    start = show_buf_num(start, end, c);
    return start;
}

/*
 * Show a constraint.
 */
extern char *solver_show_cons(cons_t c)
{
    char *end = show_buf_cons(NULL, NULL, c);
    size_t len = end - (char *)NULL;
    char *str = (char *)gc_malloc(len+1);
    show_buf_cons(str, str+len+1, c);
    return str;
}


/*
 * Convert a constraint to a term.
 */
extern term_t solver_convert_cons(cons_t c)
{
    switch (c->sym->type)
    {
        case X_CMP_Y:
        {
            atom_t atom =
                (c->sym == EQ? ATOM_EQ: ATOM_GT);
            return term_func(make_func(atom, c->args[X], c->args[Y]));
        }
        case X_CMP_C:
        {
            atom_t atom =
                (c->sym == EQ_C? ATOM_EQ: ATOM_GT);
            return term_func(make_func(atom, c->args[X], c->args[Y]));
        
        }
        case X_EQ_Y_OP_Z:
        {
            atom_t atom =
                (c->sym == EQ_PLUS? ATOM_ADD: ATOM_MUL);
            term_t t = term_func(make_func(atom, c->args[Y], c->args[Z]));
            atom = ATOM_EQ;
            return term_func(make_func(atom, c->args[X], t));
        }
        case X_EQ_Y_OP_C:
        {
            term_t t;
            if (c->sym == EQ_PLUS_C)
                t = term_func(make_func(ATOM_ADD, c->args[Y], c->args[Z]));
            else
                t = term_func(make_func(ATOM_MUL, c->args[Z], c->args[Y]));
            atom_t atom = ATOM_EQ;
            return term_func(make_func(atom, c->args[X], t));
        }
        default:
        {
            atom_t atom = make_atom(c->sym->name, c->sym->arity);
            return term_func(make_func_a(atom, c->args));
        }
    }
}

