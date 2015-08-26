/*
 * tree.h
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
#ifndef __TREE_H
#define __TREE_H

#include "word.h"

/*
 * Abstract tree type.
 */
typedef struct tree_s *tree_t;

/*
 * Order function type.
 */
typedef int_t (*compare_t)(word_t a, word_t b);

/*
 * Value mapping.
 */
typedef word_t (*valmap_t)(word_t arg, word_t k, word_t v);

/*
 * An empty tree.
 */
#define TREE_EMPTY          (gc_index_region(2))

/*
 * Tree functions.
 */
extern tree_t tree_init(void);
extern bool tree_isempty(tree_t t);
extern bool tree_issingleton(tree_t t);
extern bool tree_search(tree_t t, word_t k, word_t *v, compare_t compare);
extern bool tree_search_any(tree_t t, word_t *k, word_t *v);
extern bool tree_search_min(tree_t t, word_t *k, word_t *v);
extern bool tree_search_max(tree_t t, word_t *k, word_t *v);
extern bool tree_search_lt(tree_t t, word_t k0, word_t *k, word_t *v,
    compare_t compare);
extern bool tree_search_gt(tree_t t, word_t k0, word_t *k, word_t *v,
    compare_t compare);
extern tree_t tree_insert(tree_t t, word_t k, word_t v, compare_t compare);
extern tree_t tree_destructive_insert(tree_t t, word_t k, word_t v,
    compare_t compare);
extern tree_t tree_delete(tree_t t, word_t k, word_t *v, compare_t compare);
extern tree_t tree_destructive_delete(tree_t t, word_t k, word_t *v,
    compare_t compare);
extern tree_t tree_delete_min(tree_t t, word_t *k, word_t *v);
extern tree_t tree_delete_max(tree_t t, word_t *k, word_t *v);
extern size_t tree_size(tree_t t);
extern size_t tree_depth(tree_t t);
extern tree_t tree_map(tree_t t, word_t arg, valmap_t map);

/*
 * Tree interators.
 */
typedef struct treeitr_s *treeitr_t;
extern size_t tree_itrsize(tree_t t);
extern treeitr_t tree_itrinit(treeitr_t i, tree_t t);
extern treeitr_t tree_itrinit_geq(treeitr_t i, tree_t t, word_t k0,
    compare_t compare);
extern bool tree_get(treeitr_t i, word_t *k, word_t *v);
extern void tree_next(treeitr_t i);

#endif      /* __TREE_H */
