/*
 * show.h
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
#ifndef __SHOW_H
#define __SHOW_H

#include <stdio.h>

#include "term.h"

/****************************************************************************/
/* SHOW API                                                                 */
/****************************************************************************/

extern void show_file(FILE *file, term_t t);

extern char *show(term_t t);
extern char *show_nil(void);
extern char *show_bool(bool_t b);
extern char *show_num(num_t n);
extern char *show_atom(atom_t a);
extern char *show_string(str_t s);
extern char *show_func(func_t f);
extern char *show_var(var_t v);
extern char *show_foreign(foreign_t f);

extern char *show_buf(char *start, char *end, term_t t);
extern char *show_buf_nil(char *start, char *end);
extern char *show_buf_bool(char *start, char *end, bool_t b);
extern char *show_buf_num(char *start, char *end, num_t n);
extern char *show_buf_atom(char *start, char *end, atom_t a);
extern char *show_buf_string(char *start, char *end, str_t s);
extern char *show_buf_func(char *start, char *end, func_t f);
extern char *show_buf_var(char *start, char *end, var_t v);
extern char *show_buf_foreign(char *start, char *end, foreign_t f);

/*
 * Primitive:
 */
extern char *show_buf_char(char *start, char *end, char c);
extern char *show_buf_str(char *start, char *end, const char *str);
extern char *show_buf_esc_str(char *start, char *end, const char *str,
    size_t len);
extern char *show_buf_name(char *start, char *end, const char *name);

#endif      /* __SHOW_H */
