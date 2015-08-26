/*
 * tree.c
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

/*
 * NOTE:
 * - gcc option '-O3' generates slower binary than '-02' for this module.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "gc.h"
#include "tree.h"
#include "word.h"

/*
 * Tree dummy structure (for stronger type checking).
 */
struct tree_s
{
    word_t dummy;
};

/*
 * Tree node definitions.
 */
struct tree2_s
{
    word_t k[1];
    word_t v[1];
    tree_t t[2];
} __attribute__((__packed__));

struct tree3_s
{
    word_t k[2];
    word_t v[2];
    tree_t t[3];
} __attribute__((__packed__));

struct tree4_s
{
    word_t k[3];
    word_t v[3];
    tree_t t[4];
} __attribute__((__packed__));

typedef struct tree2_s *tree2_t;
typedef struct tree3_s *tree3_t;
typedef struct tree4_s *tree4_t;

/*
 * Iterators.
 */
struct treeitr_s
{
    ssize_t stackptr;               // Stack pointer.
    tree_t  stack[];                // Stack.
};

/*
 * Tree node types.
 */
enum treetype_e
{
    TREE_NIL = 2,
    TREE_2   = 1,
    TREE_3   = 3,
    TREE_4   = 4
};
typedef enum treetype_e treetype_t;

/*
 * Given a tree pointer, determine the tree's type.
 */
static inline treetype_t treetype(tree_t t)
{
    return (treetype_t)gc_index(t);
}

static inline tree2_t tree2(tree_t t0, word_t k0, word_t v0, tree_t t1)
{
    tree2_t node = (tree2_t)gc_malloc(sizeof(struct tree2_s));
    node->k[0] = k0;
    node->v[0] = v0;
    node->t[0] = t0;
    node->t[1] = t1;
    return node;
}

static inline tree3_t tree3(tree_t t0, word_t k0, word_t v0, tree_t t1,
    word_t k1, word_t v1, tree_t t2)
{
    tree3_t node = (tree3_t)gc_malloc(sizeof(struct tree3_s));
    node->k[0] = k0;
    node->k[1] = k1;
    node->v[0] = v0;
    node->v[1] = v1;
    node->t[0] = t0;
    node->t[1] = t1;
    node->t[2] = t2;
    return node;
}

static inline tree4_t tree4(tree_t t0, word_t k0, word_t v0, tree_t t1, 
    word_t k1, word_t v1, tree_t t2, word_t k2, word_t v2, tree_t t3)
{
    tree4_t node = (tree4_t)gc_malloc(sizeof(struct tree4_s));
    node->k[0] = k0;
    node->k[1] = k1;
    node->k[2] = k2;
    node->v[0] = v0;
    node->v[1] = v1;
    node->v[2] = v2;
    node->t[0] = t0;
    node->t[1] = t1;
    node->t[2] = t2;
    node->t[3] = t3;
    return node;
}

/*
 * Prototypes.
 */
static tree_t tree2_insert(tree2_t t, word_t k, word_t v, compare_t compare);
static tree_t tree3_insert(tree3_t t, word_t k, word_t v, compare_t compare);
static tree_t tree2_destructive_insert(tree2_t t, word_t k, word_t v,
    compare_t compare);
static tree_t tree3_destructive_insert(tree3_t t, word_t k, word_t v,
    compare_t compare);
static tree_t tree_delete_2(tree_t t, word_t k, word_t *v, compare_t compare,
    bool *reduced);
static tree_t tree_delete_min_2(tree_t t, word_t *k, word_t *v,
    bool *reduced);
static tree_t tree_delete_max_2(tree_t t, word_t *k, word_t *v,
    bool *reduced);
static tree_t tree2_fix_t0(tree_t t0, word_t k0, word_t v0, tree_t t1,
    bool *reduced);
static tree_t tree2_fix_t1(tree_t t0, word_t k0, word_t v0, tree_t t1,
    bool *reduced);
static tree_t tree3_fix_t0(tree_t t0, word_t k0, word_t v0, tree_t t1,
    word_t k1, word_t v1, tree_t t2, bool *reduced);
static tree_t tree3_fix_t1(tree_t t0, word_t k0, word_t v0, tree_t t1,
    word_t k1, word_t v1, tree_t t2, bool *reduced);
static tree_t tree3_fix_t2(tree_t t0, word_t k0, word_t v0, tree_t t1,
    word_t k1, word_t v1, tree_t t2, bool *reduced);
static tree_t tree4_fix_t0(tree_t t0, word_t k0, word_t v0, tree_t t1,
    word_t k1, word_t v1, tree_t t2, word_t k2, word_t v2, tree_t t3,
    bool *reduced);
static tree_t tree4_fix_t1(tree_t t0, word_t k0, word_t v0, tree_t t1,
    word_t k1, word_t v1, tree_t t2, word_t k2, word_t v2, tree_t t3,
    bool *reduced);
static tree_t tree4_fix_t2(tree_t t0, word_t k0, word_t v0, tree_t t1,
    word_t k1, word_t v1, tree_t t2, word_t k2, word_t v2, tree_t t3,
    bool *reduced);
static tree_t tree4_fix_t3(tree_t t0, word_t k0, word_t v0, tree_t t1,
    word_t k1, word_t v1, tree_t t2, word_t k2, word_t v2, tree_t t3,
    bool *reduced);
static tree_t tree_destructive_delete_2(tree_t t, word_t k, word_t *v,
    compare_t compare, bool *reduced);
static tree_t tree_destructive_delete_min_2(tree_t t, word_t *k, word_t *v,
    bool *reduced);
//static tree_t tree_destructive_delete_max_2(tree_t t, word_t *k, word_t *v,
//    bool *reduced);
static tree_t tree2_destructive_fix_t0(tree2_t t, bool *reduced);
static tree_t tree2_destructive_fix_t1(tree2_t t, bool *reduced);
static tree_t tree3_destructive_fix_t0(tree3_t t, bool *reduced);
static tree_t tree3_destructive_fix_t1(tree3_t t, bool *reduced);
static tree_t tree3_destructive_fix_t2(tree3_t t, bool *reduced);
static tree_t tree4_destructive_fix_t0(tree4_t t, bool *reduced);
static tree_t tree4_destructive_fix_t1(tree4_t t, bool *reduced);
static tree_t tree4_destructive_fix_t2(tree4_t t, bool *reduced);
static tree_t tree4_destructive_fix_t3(tree4_t t, bool *reduced);

/*
 * Initialise an empty tree.
 */
extern tree_t tree_init(void)
{
    return TREE_EMPTY;
}

/*
 * Test if a tree is empty.
 */
extern bool tree_isempty(tree_t t)
{
    return (t == TREE_EMPTY);
}

/*
 * Test if a tree is a singleton.
 */
extern bool tree_issingleton(tree_t t)
{
    if (treetype(t) != TREE_2)
        return false;
    tree2_t t2 = (tree2_t)t;
    return (t2->t[0] == TREE_EMPTY);
}

/*
 * Search for the value associated to the given key.
 */
extern bool tree_search(tree_t t, word_t k, word_t *v, compare_t compare)
{
    while (true)
    {
        switch (treetype(t))
        {
            case TREE_NIL:
                return false;
            case TREE_2:
            {
                tree2_t t2 = (tree2_t)t;
                int_t cmp = compare(k, t2->k[0]);
                if (cmp < 0)
                {
                    t = t2->t[0];
                    continue;
                }
                else if (cmp > 0)
                {
                    t = t2->t[1];
                    continue;
                }
                if (v != NULL)
                    *v = t2->v[0];
                return true;
            }
            case TREE_3:
            {
                tree3_t t3 = (tree3_t)t;
                int_t cmp = compare(k, t3->k[0]);
                if (cmp < 0)
                {
                    t = t3->t[0];
                    continue;
                }
                else if (cmp > 0)
                {
                    cmp = compare(k, t3->k[1]);
                    if (cmp < 0)
                    {
                        t = t3->t[1];
                        continue;
                    }
                    else if (cmp > 0)
                    {
                        t = t3->t[2];
                        continue;
                    }
                    if (v != NULL)
                        *v = t3->v[1];
                    return true;
                }
                if (v != NULL)
                    *v = t3->v[0];
                return true;
            }
            case TREE_4:
            {
                tree4_t t4 = (tree4_t)t;
                int_t cmp = compare(k, t4->k[1]);
                if (cmp < 0)
                {
                    cmp = compare(k, t4->k[0]);
                    if (cmp < 0)
                    {
                        t = t4->t[0];
                        continue;
                    }
                    else if (cmp > 0)
                    {
                        t = t4->t[1];
                        continue;
                    }
                    if (v != NULL)
                        *v = t4->v[0];
                    return true;
                }
                else if (cmp > 0)
                {
                    cmp = compare(k, t4->k[2]);
                    if (cmp < 0)
                    {
                        t = t4->t[2];
                        continue;
                    }
                    else if (cmp > 0)
                    {
                        t = t4->t[3];
                        continue;
                    }
                    if (v != NULL)
                        *v = t4->v[2];
                    return true;
                }
                if (v != NULL)
                    *v = t4->v[1];
                return true;
            }
        }
    }
}

/*
 * Search for any element in a tree.
 */
extern bool tree_search_any(tree_t t, word_t *k, word_t *v)
{
    switch (treetype(t))
    {
        case TREE_NIL:
            return false;
        case TREE_2:
        {
            tree2_t t2 = (tree2_t)t;
            if (k != NULL)
                *k = t2->k[0];
            if (v != NULL)
                *v = t2->v[0];
            return true;
        }
        case TREE_3:
        {
            tree3_t t3 = (tree3_t)t;
            if (k != NULL)
                *k = t3->k[0];
            if (v != NULL)
                *v = t3->v[0];
            return true;
        }
        case TREE_4:
        {
            tree4_t t4 = (tree4_t)t;
            if (k != NULL)
                *k = t4->k[1];
            if (v != NULL)
                *v = t4->v[1];
            return true;
        }
        default:
            abort();
    }
}

/*
 * Search for the minimum element in a tree.
 */
extern bool tree_search_min(tree_t t, word_t *k, word_t *v)
{
    while (true)
    {
        switch (treetype(t))
        {
            case TREE_NIL:
                return false;
            case TREE_2:
            {
                tree2_t t2 = (tree2_t)t;
                if (t2->t[0] == TREE_EMPTY)
                {
                    if (k != NULL)
                        *k = t2->k[0];
                    if (v != NULL)
                        *v = t2->v[0];
                    return true;
                }
                else
                {
                    t = t2->t[0];
                    continue;
                }
            }
            case TREE_3:
            {
                tree3_t t3 = (tree3_t)t;
                if (t3->t[0] == TREE_EMPTY)
                {
                    if (k != NULL)
                        *k = t3->k[0];
                    if (v != NULL)
                        *v = t3->v[0];
                    return true;
                }
                else
                {
                    t = t3->t[0];
                    continue;
                }
            }
            case TREE_4:
            {
                tree4_t t4 = (tree4_t)t;
                if (t4->t[0] == TREE_EMPTY)
                {
                    if (k != NULL)
                        *k = t4->k[0];
                    if (v != NULL)
                        *v = t4->v[0];
                    return true;
                }
                else
                {
                    t = t4->t[0];
                    continue;
                }
            }
        }
    }
}

/*
 * Search for the maximum element in a tree.
 */
extern bool tree_search_max(tree_t t, word_t *k, word_t *v)
{
    while (true)
    {
        switch (treetype(t))
        {
            case TREE_NIL:
                return false;
            case TREE_2:
            {
                tree2_t t2 = (tree2_t)t;
                if (t2->t[0] == TREE_EMPTY)
                {
                    if (k != NULL)
                        *k = t2->k[0];
                    if (v != NULL)
                        *v = t2->v[0];
                    return true;
                }
                else
                {
                    t = t2->t[1];
                    continue;
                }
            }
            case TREE_3:
            {
                tree3_t t3 = (tree3_t)t;
                if (t3->t[1] == TREE_EMPTY)
                {
                    if (k != NULL)
                        *k = t3->k[1];
                    if (v != NULL)
                        *v = t3->v[1];
                    return true;
                }
                else
                {
                    t = t3->t[2];
                    continue;
                }
            }
            case TREE_4:
            {
                tree4_t t4 = (tree4_t)t;
                if (t4->t[2] == TREE_EMPTY)
                {
                    if (k != NULL)
                        *k = t4->k[2];
                    if (v != NULL)
                        *v = t4->v[2];
                    return true;
                }
                else
                {
                    t = t4->t[3];
                    continue;
                }
            }
        }
    }
}

/*
 * Search for a K-V pair below k0.
 */
extern bool tree_search_lt(tree_t t, word_t k0, word_t *k, word_t *v,
    compare_t compare)
{
    switch (treetype(t))
    {
        case TREE_NIL:
            return false;
        case TREE_2:
        {
            tree2_t t2 = (tree2_t)t;
            int_t cmp = compare(k0, t2->k[0]);
            if (cmp > 0)
            {
                if (!tree_search_lt(t2->t[1], k0, k, v, compare))
                {
                    if (k != NULL)
                        *k = t2->k[0];
                    if (v != NULL)
                        *v = t2->v[0];
                    return true;
                }
                return true;
            }
            return tree_search_lt(t2->t[0], k0, k, v, compare);
        }
        case TREE_3:
        {
            tree3_t t3 = (tree3_t)t;
            int_t cmp = compare(k0, t3->k[1]);
            if (cmp > 0)
            {
                if (!tree_search_lt(t3->t[2], k0, k, v, compare))
                {
                    if (k != NULL)
                        *k = t3->k[1];
                    if (v != NULL)
                        *v = t3->v[1];
                    return true;
                }
                return true;
            }
            cmp = compare(k0, t3->k[0]);
            if (cmp > 0)
            {
                if (!tree_search_lt(t3->t[1], k0, k, v, compare))
                {
                    if (k != NULL)
                        *k = t3->k[0];
                    if (v != NULL)
                        *v = t3->v[0];
                    return true;
                }
                return true;
            }
            return tree_search_lt(t3->t[0], k0, k, v, compare);
        }
        case TREE_4:
        {
            tree4_t t4 = (tree4_t)t;
            int_t cmp = compare(k0, t4->k[1]);
            if (cmp > 0)
            {
                cmp = compare(k0, t4->k[2]);
                if (cmp > 0)
                {
                    if (!tree_search_lt(t4->t[3], k0, k, v, compare))
                    {
                        if (k != NULL)
                            *k = t4->k[2];
                        if (v != NULL)
                            *v = t4->v[2];
                        return true;
                    }
                    return true;
                }
                if (!tree_search_lt(t4->t[2], k0, k, v, compare))
                {
                    if (k != NULL)
                        *k = t4->k[1];
                    if (v != NULL)
                        *v = t4->v[1];
                    return true;
                }
                return true;
            }
            cmp = compare(k0, t4->k[0]);
            if (cmp > 0)
            {
                if (!tree_search_lt(t4->t[1], k0, k, v, compare))
                {
                    if (k != NULL)
                        *k = t4->k[0];
                    if (v != NULL)
                        *v = t4->v[0];
                    return true;
                }
                return true;
            }
            return tree_search_lt(t4->t[0], k0, k, v, compare);
        }
        default:
            abort();
    }
}

/*
 * Search for a K-V pair above k0.
 */
extern bool tree_search_gt(tree_t t, word_t k0, word_t *k, word_t *v,
    compare_t compare)
{
    switch (treetype(t))
    {
        case TREE_NIL:
            return false;
        case TREE_2:
        {
            tree2_t t2 = (tree2_t)t;
            int_t cmp = compare(k0, t2->k[0]);
            if (cmp < 0)
            {
                if (!tree_search_gt(t2->t[0], k0, k, v, compare))
                {
                    if (k != NULL)
                        *k = t2->k[0];
                    if (v != NULL)
                        *v = t2->v[0];
                    return true;
                }
                return true;
            }
            return tree_search_gt(t2->t[1], k0, k, v, compare);
        }
        case TREE_3:
        {
            tree3_t t3 = (tree3_t)t;
            int_t cmp = compare(k0, t3->k[0]);
            if (cmp < 0)
            {
                if (!tree_search_gt(t3->t[0], k0, k, v, compare))
                {
                    if (k != NULL)
                        *k = t3->k[0];
                    if (v != NULL)
                        *v = t3->v[0];
                    return true;
                }
                return true;
            }
            cmp = compare(k0, t3->k[1]);
            if (cmp < 0)
            {
                if (!tree_search_gt(t3->t[1], k0, k, v, compare))
                {
                    if (k != NULL)
                        *k = t3->k[1];
                    if (v != NULL)
                        *v = t3->v[1];
                    return true;
                }
                return true;
            }
            return tree_search_gt(t3->t[2], k0, k, v, compare);
        }
        case TREE_4:
        {
            tree4_t t4 = (tree4_t)t;
            int_t cmp = compare(k0, t4->k[1]);
            if (cmp < 0)
            {
                cmp = compare(k0, t4->k[0]);
                if (cmp < 0)
                {
                    if (!tree_search_gt(t4->t[0], k0, k, v, compare))
                    {
                        if (k != NULL)
                            *k = t4->k[0];
                        if (v != NULL)
                            *v = t4->v[0];
                        return true;
                    }
                    return true;
                }
                if (!tree_search_gt(t4->t[1], k0, k, v, compare))
                {
                    if (k != NULL)
                        *k = t4->k[1];
                    if (v != NULL)
                        *v = t4->v[1];
                    return true;
                }
                return true;
            }
            cmp = compare(k0, t4->k[2]);
            if (cmp < 0)
            {
                if (!tree_search_gt(t4->t[2], k0, k, v, compare))
                {
                    if (k != NULL)
                        *k = t4->k[2];
                    if (v != NULL)
                        *v = t4->v[2];
                    return true;
                }
                return true;
            }
            return tree_search_gt(t4->t[3], k0, k, v, compare);
        }
        default:
            abort();
    }
}

/*
 * Insert a K-V pair into a tree.
 */
extern tree_t tree_insert(tree_t t, word_t k, word_t v, compare_t compare)
{
    switch (treetype(t))
    {
        case TREE_NIL:
            return (tree_t)tree2(TREE_EMPTY, k, v, TREE_EMPTY);
        case TREE_2:
            return tree2_insert((tree2_t)t, k, v, compare);
        case TREE_3:
            return tree3_insert((tree3_t)t, k, v, compare);
        case TREE_4:
        {
            tree4_t t4 = (tree4_t)t;
            tree2_t lt = tree2(t4->t[0], t4->k[0], t4->v[0], t4->t[1]);
            tree2_t rt = tree2(t4->t[2], t4->k[2], t4->v[2], t4->t[3]);
            tree2_t nt = tree2((tree_t)lt, t4->k[1], t4->v[1], (tree_t)rt);
            return tree2_insert(nt, k, v, compare);
        }
        default:
            abort();
    }
}

/*
 * Insert a K-V pair into a tree2.
 */
static tree_t tree2_insert(tree2_t t, word_t k, word_t v, compare_t compare)
{
    int_t cmp = compare(k, t->k[0]);
    if (t->t[0] == TREE_EMPTY)
    {
        tree_t nil = TREE_EMPTY;
        if (cmp < 0)
            return (tree_t)tree3(nil, k, v, nil, t->k[0], t->v[0], nil);
        else if (cmp > 0)
            return (tree_t)tree3(nil, t->k[0], t->v[0], nil, k, v, nil);
        else
            return (tree_t)tree2(nil, k, v, nil);
    }
    else
    {
        if (cmp < 0)
        {
            switch (treetype(t->t[0]))
            {
                case TREE_2:
                {
                    tree2_t t2 = (tree2_t)t->t[0];
                    tree_t nt = tree2_insert(t2, k, v, compare);
                    return (tree_t)tree2(nt, t->k[0], t->v[0], t->t[1]);
                }
                case TREE_3:
                {
                    tree3_t t3 = (tree3_t)t->t[0];
                    tree_t nt = tree3_insert(t3, k, v, compare);
                    return (tree_t)tree2(nt, t->k[0], t->v[0], t->t[1]);
                }
                case TREE_4:
                {
                    tree4_t t4 = (tree4_t)t->t[0];
                    tree2_t lt = tree2(t4->t[0], t4->k[0], t4->v[0], t4->t[1]);
                    tree2_t rt = tree2(t4->t[2], t4->k[2], t4->v[2], t4->t[3]);
                    cmp = compare(k, t4->k[1]);
                    if (cmp < 0)
                    {
                        tree_t nt = tree2_insert(lt, k, v, compare);
                        return (tree_t)tree3(nt, t4->k[1], t4->v[1],
                            (tree_t)rt, t->k[0], t->v[0], t->t[1]);
                    }
                    else if (cmp > 0)
                    {
                        tree_t nt = tree2_insert(rt, k, v, compare);
                        return (tree_t)tree3((tree_t)lt, t4->k[1], t4->v[1],
                            nt, t->k[0], t->v[0], t->t[1]);
                    }
                    else
                        return (tree_t)tree3((tree_t)lt, k, v, (tree_t)rt,
                            t->k[0], t->v[0], t->t[1]);
                }
                default:
                    abort();
            }
        }
        else if (cmp > 0)
        {
            switch (treetype(t->t[1]))
            {
                case TREE_2:
                {
                    tree2_t t2 = (tree2_t)t->t[1];
                    tree_t nt = tree2_insert(t2, k, v, compare);
                    return (tree_t)tree2(t->t[0], t->k[0], t->v[0], nt);
                }
                case TREE_3:
                {
                    tree3_t t3 = (tree3_t)t->t[1];
                    tree_t nt = tree3_insert(t3, k, v, compare);
                    return (tree_t)tree2(t->t[0], t->k[0], t->v[0], nt);
                }
                case TREE_4:
                {
                    tree4_t t4 = (tree4_t)t->t[1];
                    tree2_t lt = tree2(t4->t[0], t4->k[0], t4->v[0], t4->t[1]);
                    tree2_t rt = tree2(t4->t[2], t4->k[2], t4->v[2], t4->t[3]);
                    cmp = compare(k, t4->k[1]);
                    if (cmp < 0)
                    {
                        tree_t nt = tree2_insert(lt, k, v, compare);
                        return (tree_t)tree3(t->t[0], t->k[0], t->v[0], nt,
                            t4->k[1], t4->v[1], (tree_t)rt);
                    }
                    else if (cmp > 0)
                    {
                        tree_t nt = tree2_insert(rt, k, v, compare);
                        return (tree_t)tree3(t->t[0], t->k[0], t->v[0],
                            (tree_t)lt, t4->k[1], t4->v[1], nt);
                    }
                    else
                        return (tree_t)tree3(t->t[0], t->k[0], t->v[0],
                            (tree_t)lt, k, v, (tree_t)rt);
                }
                default:
                    abort();
            }
        }
        else
            return (tree_t)tree2(t->t[0], k, v, t->t[1]);
    }
}

/*
 * Insert a K-V pair into a tree3.
 */
static tree_t tree3_insert(tree3_t t, word_t k, word_t v, compare_t compare)
{
    int_t cmp = compare(k, t->k[0]);
    if (t->t[0] == TREE_EMPTY)
    {
        tree_t nil = TREE_EMPTY;
        if (cmp < 0)
            return (tree_t)tree4(nil, k, v, nil, t->k[0], t->v[0], nil,
                t->k[1], t->v[1], nil);
        else if (cmp > 0)
        {
            cmp = compare(k, t->k[1]);
            if (cmp < 0)
                return (tree_t)tree4(nil, t->k[0], t->v[0], nil, k, v, nil,
                    t->k[1], t->v[1], nil);
            else if (cmp > 0)
                return (tree_t)tree4(nil, t->k[0], t->v[0], nil, t->k[1],
                    t->v[1], nil, k, v, nil);
            else
                return (tree_t)tree3(nil, t->k[0], t->v[0], nil, k, v, nil);
        }
        else
            return (tree_t)tree3(nil, k, v, nil, t->k[1], t->v[1], nil);
    }
    else
    {
        if (cmp < 0)
        {
            switch (treetype(t->t[0]))
            {
                case TREE_2:
                {
                    tree2_t t2 = (tree2_t)t->t[0];
                    tree_t nt = tree2_insert(t2, k, v, compare);
                    return (tree_t)tree3(nt, t->k[0], t->v[0], t->t[1],
                        t->k[1], t->v[1], t->t[2]);
                }
                case TREE_3:
                {
                    tree3_t t3 = (tree3_t)t->t[0];
                    tree_t nt = tree3_insert(t3, k, v, compare);
                    return (tree_t)tree3(nt, t->k[0], t->v[0], t->t[1],
                        t->k[1], t->v[1], t->t[2]);
                }
                case TREE_4:
                {
                    tree4_t t4 = (tree4_t)t->t[0];
                    tree2_t lt = tree2(t4->t[0], t4->k[0], t4->v[0], t4->t[1]);
                    tree2_t rt = tree2(t4->t[2], t4->k[2], t4->v[2], t4->t[3]);
                    cmp = compare(k, t4->k[1]);
                    if (cmp < 0)
                    {
                        tree_t nt = tree2_insert(lt, k, v, compare);
                        return (tree_t)tree4(nt, t4->k[1], t4->v[1],
                            (tree_t)rt, t->k[0], t->v[0], t->t[1], t->k[1],
                            t->v[1], t->t[2]);
                    }
                    else if (cmp > 0)
                    {
                        tree_t nt = tree2_insert(rt, k, v, compare);
                        return (tree_t)tree4((tree_t)lt, t4->k[1], t4->v[1],
                            nt, t->k[0], t->v[0], t->t[1], t->k[1], t->v[1],
                            t->t[2]);
                    }
                    else
                        return (tree_t)tree4((tree_t)lt, k, v, (tree_t)rt,
                            t->k[0], t->v[0], t->t[1], t->k[1], t->v[1],
                            t->t[2]);
                }
                default:
                    abort();
            }
        }
        else if (cmp > 0)
        {
            cmp = compare(k, t->k[1]);
            if (cmp < 0)
            {
                switch (treetype(t->t[1]))
                {
                    case TREE_2:
                    {
                        tree2_t t2 = (tree2_t)t->t[1];
                        tree_t nt = tree2_insert(t2, k, v, compare);
                        return (tree_t)tree3(t->t[0], t->k[0], t->v[0], nt,
                            t->k[1], t->v[1], t->t[2]);
                    }
                    case TREE_3:
                    {
                        tree3_t t3 = (tree3_t)t->t[1];
                        tree_t nt = tree3_insert(t3, k, v, compare);
                        return (tree_t)tree3(t->t[0], t->k[0], t->v[0], nt,
                            t->k[1], t->v[1], t->t[2]);
                    }
                    case TREE_4:
                    {
                        tree4_t t4 = (tree4_t)t->t[1];
                        tree2_t lt = tree2(t4->t[0], t4->k[0], t4->v[0],
                            t4->t[1]);
                        tree2_t rt = tree2(t4->t[2], t4->k[2], t4->v[2],
                            t4->t[3]);
                        cmp = compare(k, t4->k[1]);
                        if (cmp < 0)
                        {
                            tree_t nt = tree2_insert(lt, k, v, compare);
                            return (tree_t)tree4(t->t[0], t->k[0], t->v[0],
                                nt, t4->k[1], t4->v[1], (tree_t)rt, t->k[1],
                                t->v[1], t->t[2]);
                        }
                        else if (cmp > 0)
                        {
                            tree_t nt = tree2_insert(rt, k, v, compare);
                            return (tree_t)tree4(t->t[0], t->k[0], t->v[0],
                                (tree_t)lt, t4->k[1], t4->v[1], nt, t->k[1],
                                t->v[1], t->t[2]);
                        }
                        else
                            return (tree_t)tree4(t->t[0], t->k[0], t->v[0],
                                (tree_t)lt, k, v, (tree_t)rt, t->k[1],
                                t->v[1], t->t[2]);
                    }
                    default:
                        abort();
                }
            }
            else if (cmp > 0)
            {
                switch (treetype(t->t[2]))
                {
                    case TREE_2:
                    {
                        tree2_t t2 = (tree2_t)t->t[2];
                        tree_t nt = tree2_insert(t2, k, v, compare);
                        return (tree_t)tree3(t->t[0], t->k[0], t->v[0],
                            t->t[1], t->k[1], t->v[1], nt);
                    }
                    case TREE_3:
                    {
                        tree3_t t3 = (tree3_t)t->t[2];
                        tree_t nt = tree3_insert(t3, k, v, compare);
                        return (tree_t)tree3(t->t[0], t->k[0], t->v[0],
                            t->t[1], t->k[1], t->v[1], nt);
                    }
                    case TREE_4:
                    {
                        tree4_t t4 = (tree4_t)t->t[2];
                        tree2_t lt = tree2(t4->t[0], t4->k[0], t4->v[0],
                            t4->t[1]);
                        tree2_t rt = tree2(t4->t[2], t4->k[2], t4->v[2],
                            t4->t[3]);
                        cmp = compare(k, t4->k[1]);
                        if (cmp < 0)
                        {
                            tree_t nt = tree2_insert(lt, k, v, compare);
                            return (tree_t)tree4(t->t[0], t->k[0], t->v[0],
                                t->t[1], t->k[1], t->v[1], nt, t4->k[1],
                                t4->v[1], (tree_t)rt);
                        }
                        else if (cmp > 0)
                        {
                            tree_t nt = tree2_insert(rt, k, v, compare);
                            return (tree_t)tree4(t->t[0], t->k[0], t->v[0],
                                t->t[1], t->k[1], t->v[1], (tree_t)lt,
                                t4->k[1], t4->v[1], nt);
                        }
                        else
                            return (tree_t)tree4(t->t[0], t->k[0], t->v[0],
                                t->t[1], t->k[1], t->v[1], (tree_t)lt, k, v,
                                (tree_t)rt);
                    }
                    default:
                        abort();
                }
            }
            else
                return (tree_t)tree3(t->t[0], t->k[0], t->v[0], t->t[1], k, v,
                    t->t[2]);
        }
        else
            return (tree_t)tree3(t->t[0], k, v, t->t[1], t->k[1], t->v[1],
                t->t[2]);
    }
}

/*
 * Insert a K-V pair into a tree.
 */
extern tree_t tree_destructive_insert(tree_t t, word_t k, word_t v,
    compare_t compare)
{
    switch (treetype(t))
    {
        case TREE_NIL:
            return (tree_t)tree2(TREE_EMPTY, k, v, TREE_EMPTY);
        case TREE_2:
            return tree2_destructive_insert((tree2_t)t, k, v, compare);
        case TREE_3:
            return tree3_destructive_insert((tree3_t)t, k, v, compare);
        case TREE_4:
        {
            tree4_t t4 = (tree4_t)t;
            tree2_t lt = tree2(t4->t[0], t4->k[0], t4->v[0], t4->t[1]);
            tree2_t rt = tree2(t4->t[2], t4->k[2], t4->v[2], t4->t[3]);
            tree2_t nt = tree2((tree_t)lt, t4->k[1], t4->v[1], (tree_t)rt);
            gc_free(t4);
            return tree2_destructive_insert(nt, k, v, compare);
        }
        default:
            abort();
    }
}

/*
 * Insert a K-V pair into a tree2.
 */
static tree_t tree2_destructive_insert(tree2_t t, word_t k, word_t v,
    compare_t compare)
{
    int_t cmp = compare(k, t->k[0]);
    if (t->t[0] == TREE_EMPTY)
    {
        tree_t nil = TREE_EMPTY;
        word_t k0 = t->k[0], v0 = t->v[0];
        gc_free(t);
        if (cmp < 0)
            return (tree_t)tree3(nil, k, v, nil, k0, v0, nil);
        else if (cmp > 0)
            return (tree_t)tree3(nil, k0, v0, nil, k, v, nil);
        else
            return (tree_t)tree2(nil, k, v, nil);
    }
    else
    {
        if (cmp < 0)
        {
            switch (treetype(t->t[0]))
            {
                case TREE_2:
                {
                    tree2_t t2 = (tree2_t)t->t[0];
                    t->t[0] = tree2_destructive_insert(t2, k, v, compare);
                    return (tree_t)t;
                }
                case TREE_3:
                {
                    tree3_t t3 = (tree3_t)t->t[0];
                    t->t[0] = tree3_destructive_insert(t3, k, v, compare);
                    return (tree_t)t;
                }
                case TREE_4:
                {
                    tree4_t t4 = (tree4_t)t->t[0];
                    tree2_t lt = tree2(t4->t[0], t4->k[0], t4->v[0], t4->t[1]);
                    tree2_t rt = tree2(t4->t[2], t4->k[2], t4->v[2], t4->t[3]);
                    word_t k41 = t4->k[1], v41 = t4->v[1];
                    gc_free(t4);
                    cmp = compare(k, k41);
                    if (cmp < 0)
                    {
                        tree_t nt = tree2_destructive_insert(lt, k, v,
                            compare);
                        return (tree_t)tree3(nt, k41, v41, (tree_t)rt,
                            t->k[0], t->v[0], t->t[1]);
                    }
                    else if (cmp > 0)
                    {
                        tree_t nt = tree2_destructive_insert(rt, k, v,
                            compare);
                        return (tree_t)tree3((tree_t)lt, k41, v41, nt,
                            t->k[0], t->v[0], t->t[1]);
                    }
                    else
                        return (tree_t)tree3((tree_t)lt, k, v, (tree_t)rt,
                            t->k[0], t->v[0], t->t[1]);
                }
                default:
                    abort();
            }
        }
        else if (cmp > 0)
        {
            switch (treetype(t->t[1]))
            {
                case TREE_NIL:
                    abort();
                case TREE_2:
                {
                    tree2_t t2 = (tree2_t)t->t[1];
                    t->t[1] = tree2_destructive_insert(t2, k, v, compare);
                    return (tree_t)t;
                }
                case TREE_3:
                {
                    tree3_t t3 = (tree3_t)t->t[1];
                    t->t[1] = tree3_destructive_insert(t3, k, v, compare);
                    return (tree_t)t;
                }
                case TREE_4:
                {
                    tree4_t t4 = (tree4_t)t->t[1];
                    tree2_t lt = tree2(t4->t[0], t4->k[0], t4->v[0], t4->t[1]);
                    tree2_t rt = tree2(t4->t[2], t4->k[2], t4->v[2], t4->t[3]);
                    word_t k41 = t4->k[1], v41 = t4->v[1];
                    gc_free(t4);
                    cmp = compare(k, k41);
                    if (cmp < 0)
                    {
                        tree_t nt = tree2_destructive_insert(lt, k, v,
                            compare);
                        return (tree_t)tree3(t->t[0], t->k[0], t->v[0], nt,
                            k41, v41, (tree_t)rt);
                    }
                    else if (cmp > 0)
                    {
                        tree_t nt = tree2_destructive_insert(rt, k, v,
                            compare);
                        return (tree_t)tree3(t->t[0], t->k[0], t->v[0],
                            (tree_t)lt, k41, v41, nt);
                    }
                    else
                        return (tree_t)tree3(t->t[0], t->k[0], t->v[0],
                            (tree_t)lt, k, v, (tree_t)rt);
                }
                default:
                    abort();
            }
        }
        else
        {
            t->v[0] = v;
            return (tree_t)t;
        }
    }
}

/*
 * Insert a K-V pair into a tree3.
 */
static tree_t tree3_destructive_insert(tree3_t t, word_t k, word_t v,
    compare_t compare)
{
    int_t cmp = compare(k, t->k[0]);
    if (t->t[0] == TREE_EMPTY)
    {
        tree_t nil = TREE_EMPTY;
        word_t k0 = t->k[0], v0 = t->v[0], k1 = t->k[1], v1 = t->v[1];
        gc_free(t);
        if (cmp < 0)
            return (tree_t)tree4(nil, k, v, nil, k0, v0, nil, k1, v1, nil);
        else if (cmp > 0)
        {
            cmp = compare(k, k1);
            if (cmp < 0)
                return (tree_t)tree4(nil, k0, v0, nil, k, v, nil, k1, v1, nil);
            else if (cmp > 0)
                return (tree_t)tree4(nil, k0, v0, nil, k1, v1, nil, k, v, nil);
            else
                return (tree_t)tree3(nil, k0, v0, nil, k, v, nil);
        }
        else
            return (tree_t)tree3(nil, k, v, nil, k1, v1, nil);
    }
    else
    {
        if (cmp < 0)
        {
            switch (treetype(t->t[0]))
            {
                case TREE_2:
                {
                    tree2_t t2 = (tree2_t)t->t[0];
                    t->t[0] = tree2_destructive_insert(t2, k, v, compare);
                    return (tree_t)t;
                }
                case TREE_3:
                {
                    tree3_t t3 = (tree3_t)t->t[0];
                    t->t[0] = tree3_destructive_insert(t3, k, v, compare);
                    return (tree_t)t;
                }
                case TREE_4:
                {
                    tree4_t t4 = (tree4_t)t->t[0];
                    tree2_t lt = tree2(t4->t[0], t4->k[0], t4->v[0], t4->t[1]);
                    tree2_t rt = tree2(t4->t[2], t4->k[2], t4->v[2], t4->t[3]);
                    word_t k41 = t4->k[1], v41 = t4->v[1];
                    gc_free(t4);
                    cmp = compare(k, k41);
                    if (cmp < 0)
                    {
                        tree_t nt = tree2_destructive_insert(lt, k, v,
                            compare);
                        return (tree_t)tree4(nt, k41, v41, (tree_t)rt,
                            t->k[0], t->v[0], t->t[1], t->k[1], t->v[1],
                            t->t[2]);
                    }
                    else if (cmp > 0)
                    {
                        tree_t nt = tree2_destructive_insert(rt, k, v,
                            compare);
                        return (tree_t)tree4((tree_t)lt, k41, v41, nt,
                            t->k[0], t->v[0], t->t[1], t->k[1], t->v[1],
                            t->t[2]);
                    }
                    else
                        return (tree_t)tree4((tree_t)lt, k, v, (tree_t)rt,
                            t->k[0], t->v[0], t->t[1], t->k[1],
                            t->v[1], t->t[2]);
                }
                default:
                    abort();
            }
        }
        else if (cmp > 0)
        {
            cmp = compare(k, t->k[1]);
            if (cmp < 0)
            {
                switch (treetype(t->t[1]))
                {
                    case TREE_2:
                    {
                        tree2_t t2 = (tree2_t)t->t[1];
                        t->t[1] = tree2_destructive_insert(t2, k, v, compare);
                        return (tree_t)t;
                    }
                    case TREE_3:
                    {
                        tree3_t t3 = (tree3_t)t->t[1];
                        t->t[1] = tree3_destructive_insert(t3, k, v, compare);
                        return (tree_t)t;
                    }
                    case TREE_4:
                    {
                        tree4_t t4 = (tree4_t)t->t[1];
                        tree2_t lt = tree2(t4->t[0], t4->k[0], t4->v[0],
                            t4->t[1]);
                        tree2_t rt = tree2(t4->t[2], t4->k[2], t4->v[2],
                            t4->t[3]);
                        word_t k41 = t4->k[1], v41 = t4->v[1];
                        gc_free(t4);
                        cmp = compare(k, k41);
                        if (cmp < 0)
                        {
                            tree_t nt = tree2_destructive_insert(lt, k, v,
                                compare);
                            return (tree_t)tree4(t->t[0], t->k[0], t->v[0],
                                nt, k41, v41, (tree_t)rt, t->k[1], t->v[1],
                                t->t[2]);
                        }
                        else if (cmp > 0)
                        {
                            tree_t nt = tree2_destructive_insert(rt, k, v,
                                compare);
                            return (tree_t)tree4(t->t[0], t->k[0], t->v[0],
                                (tree_t)lt, k41, v41, nt, t->k[1], t->v[1],
                                t->t[2]);
                        }
                        else
                            return (tree_t)tree4(t->t[0], t->k[0], t->v[0],
                                (tree_t)lt, k, v, (tree_t)rt, t->k[1], t->v[1],
                                t->t[2]);
                    }
                    default:
                        abort();
                }
            }
            else if (cmp > 0)
            {
                switch (treetype(t->t[2]))
                {
                    case TREE_2:
                    {
                        tree2_t t2 = (tree2_t)t->t[2];
                        t->t[2] = tree2_destructive_insert(t2, k, v, compare);
                        return (tree_t)t;
                    }
                    case TREE_3:
                    {
                        tree3_t t3 = (tree3_t)t->t[2];
                        t->t[2] = tree3_destructive_insert(t3, k, v, compare);
                        return (tree_t)t;
                    }
                    case TREE_4:
                    {
                        tree4_t t4 = (tree4_t)t->t[2];
                        tree2_t lt = tree2(t4->t[0], t4->k[0], t4->v[0],
                            t4->t[1]);
                        tree2_t rt = tree2(t4->t[2], t4->k[2], t4->v[2],
                            t4->t[3]);
                        word_t k41 = t4->k[1], v41 = t4->v[1];
                        gc_free(t4);
                        cmp = compare(k, k41);
                        if (cmp < 0)
                        {
                            tree_t nt = tree2_destructive_insert(lt, k, v, 
                                compare);
                            return (tree_t)tree4(t->t[0], t->k[0], t->v[0],
                                t->t[1], t->k[1], t->v[1], nt, k41, v41,
                                (tree_t)rt);
                        }
                        else if (cmp > 0)
                        {
                            tree_t nt = tree2_destructive_insert(rt, k, v,
                                compare);
                            return (tree_t)tree4(t->t[0], t->k[0], t->v[0],
                                t->t[1], t->k[1], t->v[1], (tree_t)lt, k41,
                                v41, nt);
                        }
                        else
                            return (tree_t)tree4(t->t[0], t->k[0], t->v[0],
                                t->t[1], t->k[1], t->v[1], (tree_t)lt, k, v,
                                (tree_t)rt);
                    }
                    default:
                        abort();
                }
            }
            else
            {
                t->v[1] = v;
                return (tree_t)t;
            }
        }
        else
        {
            t->v[0] = v;
            return (tree_t)t;
        }
    }
}

/*
 * Delete a key from a tree.
 */
extern tree_t tree_delete(tree_t t, word_t k, word_t *v, compare_t compare)
{
    bool reduced = false;
    return tree_delete_2(t, k, v, compare, &reduced);
}

static tree_t tree_delete_2(tree_t t, word_t k, word_t *v, compare_t compare,
    bool *reduced)
{
    switch (treetype(t))
    {
        case TREE_NIL:
            *reduced = false;
            return t;
        case TREE_2:
        {
            tree2_t t2 = (tree2_t)t;
            int_t cmp = compare(k, t2->k[0]);
            if (cmp < 0)
            {
                tree_t nt = tree_delete_2(t2->t[0], k, v, compare, reduced);
                return tree2_fix_t0(nt, t2->k[0], t2->v[0], t2->t[1],
                    reduced);
            }
            else if (cmp > 0)
            {
                tree_t nt = tree_delete_2(t2->t[1], k, v, compare, reduced);
                return tree2_fix_t1(t2->t[0], t2->k[0], t2->v[0], nt,
                    reduced);
            }
            else
            {
                if (v != NULL)
                    *v = t2->v[0];
                if (t2->t[1] == TREE_EMPTY)
                {
                    *reduced = true;
                    return TREE_EMPTY;
                }
                else
                {
                    word_t ks, vs;
                    tree_t nt = tree_delete_min_2(t2->t[1], &ks, &vs, reduced);
                    return tree2_fix_t1(t2->t[0], ks, vs, nt, reduced);
                }
            }
        }
        case TREE_3:
        {
            tree3_t t3 = (tree3_t)t;
            int_t cmp = compare(k, t3->k[0]);
            if (cmp < 0)
            {
                tree_t nt = tree_delete_2(t3->t[0], k, v, compare, reduced);
                return tree3_fix_t0(nt, t3->k[0], t3->v[0], t3->t[1],
                    t3->k[1], t3->v[1], t3->t[2], reduced);
            }
            else if (cmp > 0)
            {
                cmp = compare(k, t3->k[1]);
                if (cmp < 0)
                {
                    tree_t nt = tree_delete_2(t3->t[1], k, v, compare,
                        reduced);
                    return tree3_fix_t1(t3->t[0], t3->k[0], t3->v[0],
                        nt, t3->k[1], t3->v[1], t3->t[2], reduced);
                }
                else if (cmp > 0)
                {
                    tree_t nt = tree_delete_2(t3->t[2], k, v, compare,
                        reduced);
                    return tree3_fix_t2(t3->t[0], t3->k[0], t3->v[0],
                        t3->t[1], t3->k[1], t3->v[1], nt, reduced);
                }
                else
                {
                    if (v != NULL)
                        *v = t3->v[1];
                    if (t3->t[2] == TREE_EMPTY)
                    {
                        tree_t nil = TREE_EMPTY;
                        return (tree_t)tree2(nil, t3->k[0], t3->v[0], nil);
                    }
                    else
                    {
                        word_t ks, vs;
                        tree_t nt = tree_delete_min_2(t3->t[2], &ks, &vs,
                            reduced);
                        return tree3_fix_t2(t3->t[0], t3->k[0], t3->v[0],
                            t3->t[1], ks, vs, nt, reduced);
                    }
                }
            }
            else
            {
                if (v != NULL)
                    *v = t3->v[0];
                if (t3->t[1] == TREE_EMPTY)
                {
                    tree_t nil = TREE_EMPTY;
                    return (tree_t)tree2(nil, t3->k[1], t3->v[1], nil);
                }
                else
                {
                    word_t ks, vs;
                    tree_t nt = tree_delete_min_2(t3->t[1], &ks, &vs,
                        reduced);
                    return tree3_fix_t1(t3->t[0], ks, vs, nt, t3->k[1],
                        t3->v[1], t3->t[2], reduced);
                }
            }
        }
        case TREE_4:
        {
            tree4_t t4 = (tree4_t)t;
            int_t cmp = compare(k, t4->k[1]);
            if (cmp < 0)
            {
                cmp = compare(k, t4->k[0]);
                if (cmp < 0)
                {
                    tree_t nt = tree_delete_2(t4->t[0], k, v, compare,
                        reduced);
                    return tree4_fix_t0(nt, t4->k[0], t4->v[0], t4->t[1],
                        t4->k[1], t4->v[1], t4->t[2], t4->k[2], t4->v[2],
                        t4->t[3], reduced);
                }
                else if (cmp > 0)
                {
                    tree_t nt = tree_delete_2(t4->t[1], k, v, compare,
                        reduced);
                    return tree4_fix_t1(t4->t[0], t4->k[0], t4->v[0], nt,
                        t4->k[1], t4->v[1], t4->t[2], t4->k[2], t4->v[2],
                        t4->t[3], reduced);
                }
                else
                {
                    if (v != NULL)
                        *v = t4->v[0];
                    if (t4->t[1] == TREE_EMPTY)
                    {
                        tree_t nil = TREE_EMPTY;
                        return (tree_t)tree3(nil, t4->k[1], t4->v[1], nil,
                            t4->k[2], t4->v[2], nil);
                    }
                    else
                    {
                        word_t ks, vs;
                        tree_t nt = tree_delete_min_2(t4->t[1], &ks, &vs,
                            reduced);
                        return tree4_fix_t1(t4->t[0], ks, vs, nt, t4->k[1],
                            t4->v[1], t4->t[2], t4->k[2], t4->v[2], t4->t[3],
                            reduced);
                    }
                }
            }
            else if (cmp > 0)
            {
                cmp = compare(k, t4->k[2]);
                if (cmp < 0)
                {
                    tree_t nt = tree_delete_2(t4->t[2], k, v, compare,
                        reduced);
                    return tree4_fix_t2(t4->t[0], t4->k[0], t4->v[0],
                        t4->t[1], t4->k[1], t4->v[1], nt, t4->k[2], t4->v[2],
                        t4->t[3], reduced);
                }
                else if (cmp > 0)
                {
                    tree_t nt = tree_delete_2(t4->t[3], k, v, compare,
                        reduced);
                    return tree4_fix_t3(t4->t[0], t4->k[0], t4->v[0],
                        t4->t[1], t4->k[1], t4->v[1], t4->t[2], t4->k[2],
                        t4->v[2], nt, reduced);
                }
                else
                {
                    if (v != NULL)
                        *v = t4->v[2];
                    if (t4->t[3] == TREE_EMPTY)
                    {
                        tree_t nil = TREE_EMPTY;
                        return (tree_t)tree3(nil, t4->k[0], t4->v[0], nil,
                            t4->k[1], t4->v[1], nil);
                    }
                    else
                    {
                        word_t ks, vs;
                        tree_t nt = tree_delete_min_2(t4->t[3], &ks, &vs,
                            reduced);
                        return tree4_fix_t3(t4->t[0], t4->k[0], t4->v[0],
                            t4->t[1], t4->k[1], t4->v[1], t4->t[2], ks, vs,
                            nt, reduced);
                    }
                }
            }
            else
            {
                if (v != NULL)
                    *v = t4->v[1];
                if (t4->t[2] == TREE_EMPTY)
                {
                    tree_t nil = TREE_EMPTY;
                    return (tree_t)tree3(nil, t4->k[0], t4->v[0], nil,
                        t4->k[2], t4->v[2], nil);
                }
                else
                {
                    word_t ks, vs;
                    tree_t nt = tree_delete_min_2(t4->t[2], &ks, &vs,
                        reduced);
                    return tree4_fix_t2(t4->t[0], t4->k[0], t4->v[0],
                        t4->t[1], ks, vs, nt, t4->k[2], t4->v[2], t4->t[3],
                        reduced);
                }
            }
        }
        default:
            abort();
    }
}

/*
 * Delete the minimum K-V pair from the given tree.
 */
extern tree_t tree_delete_min(tree_t t, word_t *k, word_t *v)
{
    bool reduced = false;
    return tree_delete_min_2(t, k, v, &reduced);
}

static tree_t tree_delete_min_2(tree_t t, word_t *k, word_t *v, bool *reduced)
{
    switch (treetype(t))
    {
        case TREE_NIL:
            *reduced = false;
            return t;
        case TREE_2:
        {
            tree2_t t2 = (tree2_t)t;
            if (t2->t[0] == TREE_EMPTY)
            {
                *reduced = true;
                if (k != NULL)
                    *k = t2->k[0];
                if (v != NULL)
                    *v = t2->v[0];
                return TREE_EMPTY;
            }
            else
            {
                tree_t nt = tree_delete_min_2(t2->t[0], k, v, reduced);
                return tree2_fix_t0(nt, t2->k[0], t2->v[0], t2->t[1],
                    reduced);
            }
        }
        case TREE_3:
        {
            tree3_t t3 = (tree3_t)t;
            if (t3->t[0] == TREE_EMPTY)
            {
                if (k != NULL)
                    *k = t3->k[0];
                if (v != NULL)
                    *v = t3->v[0];
                tree_t nil = TREE_EMPTY;
                return (tree_t)tree2(nil, t3->k[1], t3->v[1], nil);
            }
            else
            {
                tree_t nt = tree_delete_min_2(t3->t[0], k, v, reduced);
                return tree3_fix_t0(nt, t3->k[0], t3->v[0], t3->t[1],
                    t3->k[1], t3->v[1], t3->t[2], reduced);
            }
        }
        case TREE_4:
        {
            tree4_t t4 = (tree4_t)t;
            if (t4->t[0] == TREE_EMPTY)
            {
                if (k != NULL)
                    *k = t4->k[0];
                if (v != NULL)
                    *v = t4->v[0];
                tree_t nil = TREE_EMPTY;
                return (tree_t)tree3(nil, t4->k[1], t4->v[1], nil, t4->k[2],
                    t4->v[2], nil);
            }
            else
            {
                tree_t nt = tree_delete_min_2(t4->t[0], k, v, reduced);
                return tree4_fix_t0(nt, t4->k[0], t4->v[0], t4->t[1],
                    t4->k[1], t4->v[1], t4->t[2], t4->k[2], t4->v[2],
                    t4->t[3], reduced);
            }
        }
        default:
            abort();
    }
}

/*
 * Delete the maximum K-V pair from the given tree.
 */
extern tree_t tree_delete_max(tree_t t, word_t *k, word_t *v)
{
    bool reduced = false;
    return tree_delete_max_2(t, k, v, &reduced);
}

static tree_t tree_delete_max_2(tree_t t, word_t *k, word_t *v, bool *reduced)
{
    switch (treetype(t))
    {
        case TREE_NIL:
            *reduced = false;
            return t;
        case TREE_2:
        {
            tree2_t t2 = (tree2_t)t;
            if (t2->t[1] == TREE_EMPTY)
            {
                *reduced = true;
                if (k != NULL)
                    *k = t2->k[0];
                if (v != NULL)
                    *v = t2->v[0];
                return TREE_EMPTY;
            }
            else
            {
                tree_t nt = tree_delete_max_2(t2->t[1], k, v, reduced);
                return tree2_fix_t1(t2->t[0], t2->k[0], t2->v[0], nt,
                    reduced);
            }
        }
        case TREE_3:
        {
            tree3_t t3 = (tree3_t)t;
            if (t3->t[2] == TREE_EMPTY)
            {
                if (k != NULL)
                    *k = t3->k[1];
                if (v != NULL)
                    *v = t3->v[1];
                tree_t nil = TREE_EMPTY;
                return (tree_t)tree2(nil, t3->k[0], t3->v[0], nil);
            }
            else
            {
                tree_t nt = tree_delete_max_2(t3->t[2], k, v, reduced);
                return tree3_fix_t2(t3->t[0], t3->k[0], t3->v[0], t3->t[1],
                    t3->k[1], t3->v[1], nt, reduced);
            }
        }
        case TREE_4:
        {
            tree4_t t4 = (tree4_t)t;
            if (t4->t[3] == TREE_EMPTY)
            {
                if (k != NULL)
                    *k = t4->k[2];
                if (v != NULL)
                    *v = t4->v[2];
                tree_t nil = TREE_EMPTY;
                return (tree_t)tree3(nil, t4->k[0], t4->v[0], nil, t4->k[1],
                    t4->v[1], nil);
            }
            else
            {
                tree_t nt = tree_delete_max_2(t4->t[3], k, v, reduced);
                return tree4_fix_t3(t4->t[0], t4->k[0], t4->v[0], t4->t[1],
                    t4->k[1], t4->v[1], t4->t[2], t4->k[2], t4->v[2],
                    nt, reduced);
            }
        }
        default:
            abort();
    }
}

static tree_t tree2_fix_t0(tree_t t0, word_t k0, word_t v0, tree_t t1,
    bool *reduced)
{
    if (*reduced)
    {
        switch (treetype(t1))
        {
            case TREE_2:
            {
                tree2_t t12 = (tree2_t)t1;
                return (tree_t)tree3(t0, k0, v0, t12->t[0], t12->k[0],
                    t12->v[0], t12->t[1]);
            }
            case TREE_3:
            {
                *reduced = false;
                tree3_t t13 = (tree3_t)t1;
                tree2_t nt1 = tree2(t13->t[1], t13->k[1], t13->v[1],
                    t13->t[2]);
                tree2_t nt0 = tree2(t0, k0, v0, t13->t[0]);
                return (tree_t)tree2((tree_t)nt0, t13->k[0], t13->v[0],
                    (tree_t)nt1);
            }
            case TREE_4:
            {
                *reduced = false;
                tree4_t t14 = (tree4_t)t1;
                tree3_t nt1 = tree3(t14->t[1], t14->k[1], t14->v[1], t14->t[2],
                    t14->k[2], t14->v[2], t14->t[3]);
                tree2_t nt0 = tree2(t0, k0, v0, t14->t[0]);
                return (tree_t)tree2((tree_t)nt0, t14->k[0], t14->v[0],
                    (tree_t)nt1);
            }
            default:
                abort();
        }
    }
    else
    {
        return (tree_t)tree2(t0, k0, v0, t1);
    }
}

static tree_t tree2_fix_t1(tree_t t0, word_t k0, word_t v0, tree_t t1,
    bool *reduced)
{
    if (*reduced)
    {
        switch (treetype(t0))
        {
            case TREE_2:
            {
                tree2_t t02 = (tree2_t)t0;
                return (tree_t)tree3(t02->t[0], t02->k[0], t02->v[0],
                    t02->t[1], k0, v0, t1);
            }
            case TREE_3:
            {
                *reduced = false;
                tree3_t t03 = (tree3_t)t0;
                tree2_t nt0 = tree2(t03->t[0], t03->k[0], t03->v[0],
                    t03->t[1]);
                tree2_t nt1 = tree2(t03->t[2], k0, v0, t1);
                return (tree_t)tree2((tree_t)nt0, t03->k[1], t03->v[1],
                    (tree_t)nt1);
            }
            case TREE_4:
            {
                *reduced = false;
                tree4_t t04 = (tree4_t)t0;
                tree3_t nt0 = tree3(t04->t[0], t04->k[0], t04->v[0], t04->t[1],
                    t04->k[1], t04->v[1], t04->t[2]);
                tree2_t nt1 = tree2(t04->t[3], k0, v0, t1);
                return (tree_t)tree2((tree_t)nt0, t04->k[2], t04->v[2],
                    (tree_t)nt1);
            }
            default:
                abort();
        }
    }
    else
    {
        return (tree_t)tree2(t0, k0, v0, t1);
    }
}

static tree_t tree3_fix_t0(tree_t t0, word_t k0, word_t v0, tree_t t1,
    word_t k1, word_t v1, tree_t t2, bool *reduced)
{
    if (*reduced)
    {
        *reduced = false;
        switch (treetype(t1))
        {
            case TREE_2:
            {
                tree2_t t12 = (tree2_t)t1;
                tree3_t nt1 = tree3(t0, k0, v0, t12->t[0], t12->k[0],
                    t12->v[0], t12->t[1]);
                return (tree_t)tree2((tree_t)nt1, k1, v1, t2);
            }
            case TREE_3:
            {
                tree3_t t13 = (tree3_t)t1;
                tree2_t nt1 = tree2(t13->t[1], t13->k[1], t13->v[1],
                    t13->t[2]);
                tree2_t nt0 = tree2(t0, k0, v0, t13->t[0]);
                return (tree_t)tree3((tree_t)nt0, t13->k[0], t13->v[0],
                    (tree_t)nt1, k1, v1, t2);
            }
            case TREE_4:
            {
                tree4_t t14 = (tree4_t)t1;
                tree3_t nt1 = tree3(t14->t[1], t14->k[1], t14->v[1], t14->t[2],
                    t14->k[2], t14->v[2], t14->t[3]);
                tree2_t nt0 = tree2(t0, k0, v0, t14->t[0]);
                return (tree_t)tree3((tree_t)nt0, t14->k[0], t14->v[0],
                    (tree_t)nt1, k1, v1, t2);
            }
            default:
                abort();
        }
    }
    else
    {
        return (tree_t)tree3(t0, k0, v0, t1, k1, v1, t2);
    }
}

static tree_t tree3_fix_t1(tree_t t0, word_t k0, word_t v0, tree_t t1,
    word_t k1, word_t v1, tree_t t2, bool *reduced)
{
    if (*reduced)
    {
        *reduced = false;
        switch (treetype(t0))
        {
            case TREE_2:
            {
                tree2_t t02 = (tree2_t)t0;
                tree3_t nt0 = tree3(t02->t[0], t02->k[0], t02->v[0], t02->t[1],
                    k0, v0, t1);
                return (tree_t)tree2((tree_t)nt0, k1, v1, t2);
            }
            case TREE_3:
            {
                tree3_t t03 = (tree3_t)t0;
                tree2_t nt0 = tree2(t03->t[0], t03->k[0], t03->v[0],
                    t03->t[1]);
                tree2_t nt1 = tree2(t03->t[2], k0, v0, t1);
                return (tree_t)tree3((tree_t)nt0, t03->k[1], t03->v[1],
                    (tree_t)nt1, k1, v1, t2);
            }
            case TREE_4:
            {
                tree4_t t04 = (tree4_t)t0;
                tree3_t nt0 = tree3(t04->t[0], t04->k[0], t04->v[0], t04->t[1],
                    t04->k[1], t04->v[1], t04->t[2]);
                tree2_t nt1 = tree2(t04->t[3], k0, v0, t1);
                return (tree_t)tree3((tree_t)nt0, t04->k[2], t04->v[2],
                    (tree_t)nt1, k1, v1, t2);
            }
            default:
                abort();
        }
    }
    else
    {
        return (tree_t)tree3(t0, k0, v0, t1, k1, v1, t2);
    }
}

static tree_t tree3_fix_t2(tree_t t0, word_t k0, word_t v0, tree_t t1,
    word_t k1, word_t v1, tree_t t2, bool *reduced)
{
    if (*reduced)
    {
        *reduced = false;
        switch (treetype(t1))
        {
            case TREE_2:
            {
                tree2_t t12 = (tree2_t)t1;
                tree3_t nt1 = tree3(t12->t[0], t12->k[0], t12->v[0], t12->t[1],
                    k1, v1, t2);
                return (tree_t)tree2(t0, k0, v0, (tree_t)nt1);
            }
            case TREE_3:
            {
                tree3_t t13 = (tree3_t)t1;
                tree2_t nt1 = tree2(t13->t[0], t13->k[0], t13->v[0],
                    t13->t[1]);
                tree2_t nt2 = tree2(t13->t[2], k1, v1, t2);
                return (tree_t)tree3(t0, k0, v0, (tree_t)nt1, t13->k[1],
                    t13->v[1], (tree_t)nt2);
            }
            case TREE_4:
            {
                tree4_t t14 = (tree4_t)t1;
                tree3_t nt1 = tree3(t14->t[0], t14->k[0], t14->v[0], t14->t[1],
                    t14->k[1], t14->v[1], t14->t[2]);
                tree2_t nt2 = tree2(t14->t[3], k1, v1, t2);
                return (tree_t)tree3(t0, k0, v0, (tree_t)nt1, t14->k[2],
                    t14->v[2], (tree_t)nt2);
            }
            default:
                abort();
        }
    }
    else
    {
        return (tree_t)tree3(t0, k0, v0, t1, k1, v1, t2);
    }
}

static tree_t tree4_fix_t0(tree_t t0, word_t k0, word_t v0, tree_t t1,
    word_t k1, word_t v1, tree_t t2, word_t k2, word_t v2, tree_t t3,
    bool *reduced)
{
    if (*reduced)
    {
        *reduced = false;
        switch (treetype(t1))
        {
            case TREE_2:
            {
                tree2_t t12 = (tree2_t)t1;
                tree3_t nt1 = tree3(t0, k0, v0, t12->t[0], t12->k[0],
                    t12->v[0], t12->t[1]);
                return (tree_t)tree3((tree_t)nt1, k1, v1, t2, k2, v2, t3);
            }
            case TREE_3:
            {
                tree3_t t13 = (tree3_t)t1;
                tree2_t nt1 = tree2(t13->t[1], t13->k[1], t13->v[1],
                    t13->t[2]);
                tree2_t nt0 = tree2(t0, k0, v0, t13->t[0]);
                return (tree_t)tree4((tree_t)nt0, t13->k[0], t13->v[0],
                    (tree_t)nt1, k1, v1, t2, k2, v2, t3);
            }
            case TREE_4:
            {
                tree4_t t14 = (tree4_t)t1;
                tree3_t nt1 = tree3(t14->t[1], t14->k[1], t14->v[1], t14->t[2],
                    t14->k[2], t14->v[2], t14->t[3]);
                tree2_t nt0 = tree2(t0, k0, v0, t14->t[0]);
                return (tree_t)tree4((tree_t)nt0, t14->k[0], t14->v[0],
                    (tree_t)nt1, k1, v1, t2, k2, v2, t3);
            }
            default:
                abort();
        }
    }
    else
    {
        return (tree_t)tree4(t0, k0, v0, t1, k1, v1, t2, k2, v2, t3);
    }
}

static tree_t tree4_fix_t1(tree_t t0, word_t k0, word_t v0, tree_t t1,
    word_t k1, word_t v1, tree_t t2, word_t k2, word_t v2, tree_t t3,
    bool *reduced)
{
    if (*reduced)
    {
        *reduced = false;
        switch (treetype(t2))
        {
            case TREE_2:
            {
                tree2_t t22 = (tree2_t)t2;
                tree3_t nt2 = tree3(t1, k1, v1, t22->t[0], t22->k[0],
                    t22->v[0], t22->t[1]);
                return (tree_t)tree3(t0, k0, v0, (tree_t)nt2, k2, v2, t3);
            }
            case TREE_3:
            {
                tree3_t t23 = (tree3_t)t2;
                tree2_t nt2 = tree2(t23->t[1], t23->k[1], t23->v[1],
                    t23->t[2]);
                tree2_t nt1 = tree2(t1, k1, v1, t23->t[0]);
                return (tree_t)tree4(t0, k0, v0, (tree_t)nt1, t23->k[0],
                    t23->v[0], (tree_t)nt2, k2, v2, t3);
            }
            case TREE_4:
            {
                tree4_t t24 = (tree4_t)t2;
                tree3_t nt2 = tree3(t24->t[1], t24->k[1], t24->v[1], t24->t[2],
                    t24->k[2], t24->v[2], t24->t[3]);
                tree2_t nt1 = tree2(t1, k1, v1, t24->t[0]);
                return (tree_t)tree4(t0, k0, v0, (tree_t)nt1, t24->k[0],
                    t24->v[0], (tree_t)nt2, k2, v2, t3);
            }
            default:
                abort();
        }
    }
    else
    {
        return (tree_t)tree4(t0, k0, v0, t1, k1, v1, t2, k2, v2, t3);
    }
}

static tree_t tree4_fix_t2(tree_t t0, word_t k0, word_t v0, tree_t t1,
    word_t k1, word_t v1, tree_t t2, word_t k2, word_t v2, tree_t t3,
    bool *reduced)
{
    if (*reduced)
    {
        *reduced = false;
        switch (treetype(t3))
        {
            case TREE_2:
            {
                tree2_t t32 = (tree2_t)t3;
                tree3_t nt3 = tree3(t2, k2, v2, t32->t[0], t32->k[0],
                    t32->v[0], t32->t[1]);
                return (tree_t)tree3(t0, k0, v0, t1, k1, v1, (tree_t)nt3);
            }
            case TREE_3:
            {
                tree3_t t33 = (tree3_t)t3;
                tree2_t nt3 = tree2(t33->t[1], t33->k[1], t33->v[1],
                    t33->t[2]);
                tree2_t nt2 = tree2(t2, k2, v2, t33->t[0]);
                return (tree_t)tree4(t0, k0, v0, t1, k1, v1, (tree_t)nt2,
                    t33->k[0], t33->v[0], (tree_t)nt3);
            }
            case TREE_4:
            {
                tree4_t t34 = (tree4_t)t3;
                tree3_t nt3 = tree3(t34->t[1], t34->k[1], t34->v[1], t34->t[2],
                    t34->k[2], t34->v[2], t34->t[3]);
                tree2_t nt2 = tree2(t2, k2, v2, t34->t[0]);
                return (tree_t)tree4(t0, k0, v0, t1, k1, v1, (tree_t)nt2,
                    t34->k[0], t34->v[0], (tree_t)nt3);
            }
            default:
                abort();
        }
    }
    else
    {
        return (tree_t)tree4(t0, k0, v0, t1, k1, v1, t2, k2, v2, t3);
    }
}

static tree_t tree4_fix_t3(tree_t t0, word_t k0, word_t v0, tree_t t1,
    word_t k1, word_t v1, tree_t t2, word_t k2, word_t v2, tree_t t3,
    bool *reduced)
{
    if (*reduced)
    {
        *reduced = false;
        switch (treetype(t2))
        {
            case TREE_2:
            {
                tree2_t t22 = (tree2_t)t2;
                tree3_t nt2 = tree3(t22->t[0], t22->k[0], t22->v[0], t22->t[1],
                    k2, v2, t3);
                return (tree_t)tree3(t0, k0, v0, t1, k1, v1, (tree_t)nt2);
            }
            case TREE_3:
            {
                tree3_t t23 = (tree3_t)t2;
                tree2_t nt2 = tree2(t23->t[0], t23->k[0], t23->v[0],
                    t23->t[1]);
                tree2_t nt3 = tree2(t23->t[2], k2, v2, t3);
                return (tree_t)tree4(t0, k0, v0, t1, k1, v1, (tree_t)nt2,
                    t23->k[1], t23->v[1], (tree_t)nt3);
            }
            case TREE_4:
            {
                tree4_t t24 = (tree4_t)t2;
                tree3_t nt2 = tree3(t24->t[0], t24->k[0], t24->v[0], t24->t[1],
                    t24->k[1], t24->v[1], t24->t[2]);
                tree2_t nt3 = tree2(t24->t[3], k2, v2, t3);
                return (tree_t)tree4(t0, k0, v0, t1, k1, v1, (tree_t)nt2,
                    t24->k[2], t24->v[2], (tree_t)nt3);
            }
            default:
                abort();
        }
    }
    else
    {
        return (tree_t)tree4(t0, k0, v0, t1, k1, v1, t2, k2, v2, t3);
    }
}

/*
 * Delete a key from a tree.
 */
extern tree_t tree_destructive_delete(tree_t t, word_t k, word_t *v,
    compare_t compare)
{
    bool reduced = false;
    return tree_destructive_delete_2(t, k, v, compare, &reduced);
}

static tree_t tree_destructive_delete_2(tree_t t, word_t k, word_t *v,
    compare_t compare, bool *reduced)
{
    switch (treetype(t))
    {
        case TREE_NIL:
            *reduced = false;
            return t;
        case TREE_2:
        {
            tree2_t t2 = (tree2_t)t;
            int_t cmp = compare(k, t2->k[0]);
            if (cmp < 0)
            {
                t2->t[0] = tree_destructive_delete_2(t2->t[0], k, v, compare,
                    reduced);
                return tree2_destructive_fix_t0(t2, reduced);
            }
            else if (cmp > 0)
            {
                t2->t[1] = tree_destructive_delete_2(t2->t[1], k, v, compare,
                    reduced);
                return tree2_destructive_fix_t1(t2, reduced);
            }
            else
            {
                if (v != NULL)
                    *v = t2->v[0];
                if (t2->t[1] == TREE_EMPTY)
                {
                    *reduced = true;
                    gc_free(t2);
                    return TREE_EMPTY;
                }
                else
                {
                    t2->t[1] = tree_destructive_delete_min_2(t2->t[1],
                        &t2->k[0], &t2->v[0], reduced);
                    return tree2_destructive_fix_t1(t2, reduced);
                }
            }
        }
        case TREE_3:
        {
            tree3_t t3 = (tree3_t)t;
            int_t cmp = compare(k, t3->k[0]);
            if (cmp < 0)
            {
                t3->t[0] = tree_destructive_delete_2(t3->t[0], k, v, compare,
                    reduced);
                return tree3_destructive_fix_t0(t3, reduced);
            }
            else if (cmp > 0)
            {
                cmp = compare(k, t3->k[1]);
                if (cmp < 0)
                {
                    t3->t[1] = tree_destructive_delete_2(t3->t[1], k, v,
                        compare, reduced);
                    return tree3_destructive_fix_t1(t3, reduced);
                }
                else if (cmp > 0)
                {
                    t3->t[2] = tree_destructive_delete_2(t3->t[2], k, v,
                        compare, reduced);
                    return tree3_destructive_fix_t2(t3, reduced);
                }
                else
                {
                    if (v != NULL)
                        *v = t3->v[1];
                    if (t3->t[2] == TREE_EMPTY)
                    {
                        tree_t nil = TREE_EMPTY;
                        word_t k30 = t3->k[0], v30 = t3->v[0];
                        gc_free(t3);
                        return (tree_t)tree2(nil, k30, v30, nil);
                    }
                    else
                    {
                        t3->t[2] = tree_destructive_delete_min_2(t3->t[2],
                            &t3->k[1], &t3->v[1], reduced);
                        return tree3_destructive_fix_t2(t3, reduced);
                    }
                }
            }
            else
            {
                if (v != NULL)
                    *v = t3->v[0];
                if (t3->t[1] == TREE_EMPTY)
                {
                    tree_t nil = TREE_EMPTY;
                    word_t k31 = t3->k[1], v31 = t3->v[1];
                    gc_free(t3);
                    return (tree_t)tree2(nil, k31, v31, nil);
                }
                else
                {
                    t3->t[1] = tree_destructive_delete_min_2(t3->t[1],
                        &t3->k[0], &t3->v[0], reduced);
                    return tree3_destructive_fix_t1(t3, reduced);
                }
            }
        }
        case TREE_4:
        {
            tree4_t t4 = (tree4_t)t;
            int_t cmp = compare(k, t4->k[1]);
            if (cmp < 0)
            {
                cmp = compare(k, t4->k[0]);
                if (cmp < 0)
                {
                    t4->t[0] = tree_destructive_delete_2(t4->t[0], k, v,
                        compare, reduced);
                    return tree4_destructive_fix_t0(t4, reduced);
                }
                else if (cmp > 0)
                {
                    t4->t[1] = tree_destructive_delete_2(t4->t[1], k, v,
                        compare, reduced);
                    return tree4_destructive_fix_t1(t4, reduced);
                }
                else
                {
                    if (v != NULL)
                        *v = t4->v[0];
                    if (t4->t[1] == TREE_EMPTY)
                    {
                        tree_t nil = TREE_EMPTY;
                        word_t k41 = t4->k[1], v41 = t4->v[1], k42 = t4->k[2],
                            v42 = t4->v[2];
                        gc_free(t4);
                        return (tree_t)tree3(nil, k41, v41, nil, k42, v42,
                            nil);
                    }
                    else
                    {
                        t4->t[1] = tree_destructive_delete_min_2(t4->t[1],
                            &t4->k[0], &t4->v[0], reduced);
                        return tree4_destructive_fix_t1(t4, reduced);
                    }
                }
            }
            else if (cmp > 0)
            {
                cmp = compare(k, t4->k[2]);
                if (cmp < 0)
                {
                    t4->t[2] = tree_destructive_delete_2(t4->t[2], k, v,
                        compare, reduced);
                    return tree4_destructive_fix_t2(t4, reduced);
                }
                else if (cmp > 0)
                {
                    t4->t[3] = tree_destructive_delete_2(t4->t[3], k, v,
                        compare, reduced);
                    return tree4_destructive_fix_t3(t4, reduced);
                }
                else
                {
                    if (v != NULL)
                        *v = t4->v[2];
                    if (t4->t[3] == TREE_EMPTY)
                    {
                        tree_t nil = TREE_EMPTY;
                        word_t k40 = t4->k[0], v40 = t4->v[0], k41 = t4->k[1],
                            v41 = t4->v[1];
                        gc_free(t4);
                        return (tree_t)tree3(nil, k40, v40, nil, k41, v41,
                            nil);
                    }
                    else
                    {
                        t4->t[3] = tree_destructive_delete_min_2(t4->t[3],
                            &t4->k[2], &t4->v[2], reduced);
                        return tree4_destructive_fix_t3(t4, reduced);
                    }
                }
            }
            else
            {
                if (v != NULL)
                    *v = t4->v[1];
                if (t4->t[2] == TREE_EMPTY)
                {
                    tree_t nil = TREE_EMPTY;
                    word_t k40 = t4->k[0], v40 = t4->v[0], k42 = t4->k[2],
                        v42 = t4->v[2];
                    gc_free(t4);
                    return (tree_t)tree3(nil, k40, v40, nil, k42, v42, nil);
                }
                else
                {
                    t4->t[2] = tree_destructive_delete_min_2(t4->t[2],
                        &t4->k[1], &t4->v[1], reduced);
                    return tree4_destructive_fix_t2(t4, reduced);
                }
            }
        }
        default:
            abort();
    }
}

/*
 * Delete the minimum K-V pair from the given tree.
 */
extern tree_t tree_destructive_delete_min(tree_t t, word_t *k, word_t *v)
{
    bool reduced = false;
    return tree_destructive_delete_min_2(t, k, v, &reduced);
}

static tree_t tree_destructive_delete_min_2(tree_t t, word_t *k, word_t *v,
    bool *reduced)
{
    switch (treetype(t))
    {
        case TREE_NIL:
            *reduced = false;
            return t;
        case TREE_2:
        {
            tree2_t t2 = (tree2_t)t;
            if (t2->t[0] == TREE_EMPTY)
            {
                *reduced = true;
                if (k != NULL)
                    *k = t2->k[0];
                if (v != NULL)
                    *v = t2->v[0];
                gc_free(t2);
                return TREE_EMPTY;
            }
            else
            {
                t2->t[0] = tree_destructive_delete_min_2(t2->t[0], k, v,
                    reduced);
                return tree2_destructive_fix_t0(t2, reduced);
            }
        }
        case TREE_3:
        {
            tree3_t t3 = (tree3_t)t;
            if (t3->t[0] == TREE_EMPTY)
            {
                if (k != NULL)
                    *k = t3->k[0];
                if (v != NULL)
                    *v = t3->v[0];
                tree_t nil = TREE_EMPTY;
                word_t k31 = t3->k[1], v31 = t3->v[1];
                gc_free(t3);
                return (tree_t)tree2(nil, k31, v31, nil);
            }
            else
            {
                t3->t[0] = tree_destructive_delete_min_2(t3->t[0], k, v,
                    reduced);
                return tree3_destructive_fix_t0(t3, reduced);
            }
        }
        case TREE_4:
        {
            tree4_t t4 = (tree4_t)t;
            if (t4->t[0] == TREE_EMPTY)
            {
                if (k != NULL)
                    *k = t4->k[0];
                if (v != NULL)
                    *v = t4->v[0];
                tree_t nil = TREE_EMPTY;
                word_t k41 = t4->k[1], v41 = t4->v[1], k42 = t4->k[2],
                    v42 = t4->v[2];
                gc_free(t4);
                return (tree_t)tree3(nil, k41, v41, nil, k42, v42, nil);
            }
            else
            {
                t4->t[0] = tree_destructive_delete_min_2(t4->t[0], k, v,
                    reduced);
                return tree4_destructive_fix_t0(t4, reduced);
            }
        }
        default:
            abort();
    }
}

static tree_t tree2_destructive_fix_t0(tree2_t t, bool *reduced)
{
    if (*reduced)
    {
        tree_t t0 = t->t[0], t1 = t->t[1];
        word_t k0 = t->k[0], v0 = t->v[0];
        gc_free(t);
        switch (treetype(t1))
        {
            case TREE_2:
            {
                tree2_t t12 = (tree2_t)t1;
                tree_t rt = (tree_t)tree3(t0, k0, v0, t12->t[0], t12->k[0],
                    t12->v[0], t12->t[1]);
                gc_free(t12);
                return rt;
            }
            case TREE_3:
            {
                *reduced = false;
                tree3_t t13 = (tree3_t)t1;
                tree2_t nt1 = tree2(t13->t[1], t13->k[1], t13->v[1],
                    t13->t[2]);
                tree2_t nt0 = tree2(t0, k0, v0, t13->t[0]);
                tree_t rt = (tree_t)tree2((tree_t)nt0, t13->k[0], t13->v[0],
                    (tree_t)nt1);
                gc_free(t13);
                return rt;
            }
            case TREE_4:
            {
                *reduced = false;
                tree4_t t14 = (tree4_t)t1;
                tree3_t nt1 = tree3(t14->t[1], t14->k[1], t14->v[1], t14->t[2],
                    t14->k[2], t14->v[2], t14->t[3]);
                tree2_t nt0 = tree2(t0, k0, v0, t14->t[0]);
                tree_t rt = (tree_t)tree2((tree_t)nt0, t14->k[0], t14->v[0],
                    (tree_t)nt1);
                gc_free(t14);
                return rt;
            }
            default:
                abort();
        }
    }
    else
        return (tree_t)t;
}

static tree_t tree2_destructive_fix_t1(tree2_t t, bool *reduced)
{
    if (*reduced)
    {
        tree_t t0 = t->t[0], t1 = t->t[1];
        word_t k0 = t->k[0], v0 = t->v[0];
        gc_free(t);
        switch (treetype(t0))
        {
            case TREE_2:
            {
                tree2_t t02 = (tree2_t)t0;
                tree_t rt = (tree_t)tree3(t02->t[0], t02->k[0], t02->v[0],
                    t02->t[1], k0, v0, t1);
                gc_free(t02);
                return rt;
            }
            case TREE_3:
            {
                *reduced = false;
                tree3_t t03 = (tree3_t)t0;
                tree2_t nt0 = tree2(t03->t[0], t03->k[0], t03->v[0],
                    t03->t[1]);
                tree2_t nt1 = tree2(t03->t[2], k0, v0, t1);
                tree_t rt = (tree_t)tree2((tree_t)nt0, t03->k[1], t03->v[1],
                    (tree_t)nt1);
                gc_free(t03);
                return rt;
            }
            case TREE_4:
            {
                *reduced = false;
                tree4_t t04 = (tree4_t)t0;
                tree3_t nt0 = tree3(t04->t[0], t04->k[0], t04->v[0], t04->t[1],
                    t04->k[1], t04->v[1], t04->t[2]);
                tree2_t nt1 = tree2(t04->t[3], k0, v0, t1);
                tree_t rt = (tree_t)tree2((tree_t)nt0, t04->k[2], t04->v[2],
                    (tree_t)nt1);
                gc_free(t04);
                return rt;
            }
            default:
                abort();
        }
    }
    else
        return (tree_t)t;
}

static tree_t tree3_destructive_fix_t0(tree3_t t, bool *reduced)
{
    if (*reduced)
    {
        *reduced = false;
        tree_t t0 = t->t[0], t1 = t->t[1], t2 = t->t[2];
        word_t k0 = t->k[0], v0 = t->v[0], k1 = t->k[1], v1 = t->v[1];
        gc_free(t);
        switch (treetype(t1))
        {
            case TREE_2:
            {
                tree2_t t12 = (tree2_t)t1;
                tree3_t nt1 = tree3(t0, k0, v0, t12->t[0], t12->k[0],
                    t12->v[0], t12->t[1]);
                gc_free(t12);
                return (tree_t)tree2((tree_t)nt1, k1, v1, t2);
            } 
            case TREE_3:
            {
                tree3_t t13 = (tree3_t)t1;
                tree2_t nt1 = tree2(t13->t[1], t13->k[1], t13->v[1],
                    t13->t[2]);
                tree2_t nt0 = tree2(t0, k0, v0, t13->t[0]);
                tree_t rt = (tree_t)tree3((tree_t)nt0, t13->k[0], t13->v[0],
                    (tree_t)nt1, k1, v1, t2);
                gc_free(t13);
                return rt;
            }
            case TREE_4:
            {
                tree4_t t14 = (tree4_t)t1;
                tree3_t nt1 = tree3(t14->t[1], t14->k[1], t14->v[1], t14->t[2],
                    t14->k[2], t14->v[2], t14->t[3]);
                tree2_t nt0 = tree2(t0, k0, v0, t14->t[0]);
                tree_t rt = (tree_t)tree3((tree_t)nt0, t14->k[0], t14->v[0],
                    (tree_t)nt1, k1, v1, t2);
                gc_free(t14);
                return rt;
            }
            default:
                abort();
        }
    }
    else
        return (tree_t)t;
}

static tree_t tree3_destructive_fix_t1(tree3_t t, bool *reduced)
{
    if (*reduced)
    {
        *reduced = false;
        tree_t t0 = t->t[0], t1 = t->t[1], t2 = t->t[2];
        word_t k0 = t->k[0], v0 = t->v[0], k1 = t->k[1], v1 = t->v[1];
        gc_free(t);
        switch (treetype(t0))
        {
            case TREE_2:
            {
                tree2_t t02 = (tree2_t)t0;
                tree3_t nt0 = tree3(t02->t[0], t02->k[0], t02->v[0], t02->t[1],
                    k0, v0, t1);
                gc_free(t02);
                return (tree_t)tree2((tree_t)nt0, k1, v1, t2);
            }
            case TREE_3:
            {
                tree3_t t03 = (tree3_t)t0;
                tree2_t nt0 = tree2(t03->t[0], t03->k[0], t03->v[0],
                    t03->t[1]);
                tree2_t nt1 = tree2(t03->t[2], k0, v0, t1);
                tree_t rt = (tree_t)tree3((tree_t)nt0, t03->k[1], t03->v[1],
                    (tree_t)nt1, k1, v1, t2);
                gc_free(t03);
                return rt;
            }
            case TREE_4:
            {
                tree4_t t04 = (tree4_t)t0;
                tree3_t nt0 = tree3(t04->t[0], t04->k[0], t04->v[0], t04->t[1],
                    t04->k[1], t04->v[1], t04->t[2]);
                tree2_t nt1 = tree2(t04->t[3], k0, v0, t1);
                tree_t rt = (tree_t)tree3((tree_t)nt0, t04->k[2], t04->v[2],
                    (tree_t)nt1, k1, v1, t2);
                gc_free(t04);
                return rt;
            }
            default:
                abort();
        }
    }
    else
        return (tree_t)t;
}

static tree_t tree3_destructive_fix_t2(tree3_t t, bool *reduced)
{
    if (*reduced)
    {
        *reduced = false;
        tree_t t0 = t->t[0], t1 = t->t[1], t2 = t->t[2];
        word_t k0 = t->k[0], v0 = t->v[0], k1 = t->k[1], v1 = t->v[1];
        gc_free(t);
        switch (treetype(t1))
        {
            case TREE_2:
            {
                tree2_t t12 = (tree2_t)t1;
                tree3_t nt1 = tree3(t12->t[0], t12->k[0], t12->v[0], t12->t[1],
                    k1, v1, t2);
                gc_free(t12);
                return (tree_t)tree2(t0, k0, v0, (tree_t)nt1);
            }
            case TREE_3:
            {
                tree3_t t13 = (tree3_t)t1;
                tree2_t nt1 = tree2(t13->t[0], t13->k[0], t13->v[0],
                    t13->t[1]);
                tree2_t nt2 = tree2(t13->t[2], k1, v1, t2);
                tree_t rt = (tree_t)tree3(t0, k0, v0, (tree_t)nt1, t13->k[1],
                    t13->v[1], (tree_t)nt2);
                gc_free(t13);
                return rt;
            }
            case TREE_4:
            {
                tree4_t t14 = (tree4_t)t1;
                tree3_t nt1 = tree3(t14->t[0], t14->k[0], t14->v[0], t14->t[1],
                    t14->k[1], t14->v[1], t14->t[2]);
                tree2_t nt2 = tree2(t14->t[3], k1, v1, t2);
                tree_t rt = (tree_t)tree3(t0, k0, v0, (tree_t)nt1, t14->k[2],
                    t14->v[2], (tree_t)nt2);
                gc_free(t14);
                return rt;
            }
            default:
                abort();
        }
    }
    else
        return (tree_t)t;
}

static tree_t tree4_destructive_fix_t0(tree4_t t, bool *reduced)
{
    if (*reduced)
    {
        *reduced = false;
        tree_t t0 = t->t[0], t1 = t->t[1], t2 = t->t[2], t3 = t->t[3];
        word_t k0 = t->k[0], v0 = t->v[0], k1 = t->k[1], v1 = t->v[1],
            k2 = t->k[2], v2 = t->v[2];
        gc_free(t);
        switch (treetype(t1))
        {
            case TREE_2:
            {
                tree2_t t12 = (tree2_t)t1;
                tree3_t nt1 = tree3(t0, k0, v0, t12->t[0], t12->k[0],
                    t12->v[0], t12->t[1]);
                gc_free(t12);
                return (tree_t)tree3((tree_t)nt1, k1, v1, t2, k2, v2, t3);
            }
            case TREE_3:
            {
                tree3_t t13 = (tree3_t)t1;
                tree2_t nt1 = tree2(t13->t[1], t13->k[1], t13->v[1],
                    t13->t[2]);
                tree2_t nt0 = tree2(t0, k0, v0, t13->t[0]);
                tree_t rt = (tree_t)tree4((tree_t)nt0, t13->k[0], t13->v[0],
                    (tree_t)nt1, k1, v1, t2, k2, v2, t3);
                gc_free(t13);
                return rt;
            }
            case TREE_4:
            {
                tree4_t t14 = (tree4_t)t1;
                tree3_t nt1 = tree3(t14->t[1], t14->k[1], t14->v[1], t14->t[2],
                    t14->k[2], t14->v[2], t14->t[3]);
                tree2_t nt0 = tree2(t0, k0, v0, t14->t[0]);
                tree_t rt = (tree_t)tree4((tree_t)nt0, t14->k[0], t14->v[0],
                    (tree_t)nt1, k1, v1, t2, k2, v2, t3);
                gc_free(t14);
                return rt;
            }
            default:
                abort();
        }
    }
    else
        return (tree_t)t;
}

static tree_t tree4_destructive_fix_t1(tree4_t t, bool *reduced)
{
    if (*reduced)
    {
        *reduced = false;
        tree_t t0 = t->t[0], t1 = t->t[1], t2 = t->t[2], t3 = t->t[3];
        word_t k0 = t->k[0], v0 = t->v[0], k1 = t->k[1], v1 = t->v[1],
            k2 = t->k[2], v2 = t->v[2];
        gc_free(t);
        switch (treetype(t2))
        {
            case TREE_2:
            {
                tree2_t t22 = (tree2_t)t2;
                tree3_t nt2 = tree3(t1, k1, v1, t22->t[0], t22->k[0],
                    t22->v[0], t22->t[1]);
                gc_free(t22);
                return (tree_t)tree3(t0, k0, v0, (tree_t)nt2, k2, v2, t3);
            }
            case TREE_3:
            {
                tree3_t t23 = (tree3_t)t2;
                tree2_t nt2 = tree2(t23->t[1], t23->k[1], t23->v[1],
                    t23->t[2]);
                tree2_t nt1 = tree2(t1, k1, v1, t23->t[0]);
                tree_t rt = (tree_t)tree4(t0, k0, v0, (tree_t)nt1, t23->k[0],
                    t23->v[0], (tree_t)nt2, k2, v2, t3);
                gc_free(t23);
                return rt;
            }
            case TREE_4:
            {
                tree4_t t24 = (tree4_t)t2;
                tree3_t nt2 = tree3(t24->t[1], t24->k[1], t24->v[1], t24->t[2],
                    t24->k[2], t24->v[2], t24->t[3]);
                tree2_t nt1 = tree2(t1, k1, v1, t24->t[0]);
                tree_t rt = (tree_t)tree4(t0, k0, v0, (tree_t)nt1, t24->k[0],
                    t24->v[0], (tree_t)nt2, k2, v2, t3);
                gc_free(t24);
                return rt;
            }
            default:
                abort();
        }
    }
    else
        return (tree_t)t;
}

static tree_t tree4_destructive_fix_t2(tree4_t t, bool *reduced)
{
    if (*reduced)
    {
        *reduced = false;
        tree_t t0 = t->t[0], t1 = t->t[1], t2 = t->t[2], t3 = t->t[3];
        word_t k0 = t->k[0], v0 = t->v[0], k1 = t->k[1], v1 = t->v[1],
            k2 = t->k[2], v2 = t->v[2];
        gc_free(t);
        switch (treetype(t3))
        {
            case TREE_2:
            {
                tree2_t t32 = (tree2_t)t3;
                tree3_t nt3 = tree3(t2, k2, v2, t32->t[0], t32->k[0],
                    t32->v[0], t32->t[1]);
                gc_free(t32);
                return (tree_t)tree3(t0, k0, v0, t1, k1, v1, (tree_t)nt3);
            }
            case TREE_3:
            {
                tree3_t t33 = (tree3_t)t3;
                tree2_t nt3 = tree2(t33->t[1], t33->k[1], t33->v[1],
                    t33->t[2]);
                tree2_t nt2 = tree2(t2, k2, v2, t33->t[0]);
                tree_t rt = (tree_t)tree4(t0, k0, v0, t1, k1, v1, (tree_t)nt2,
                    t33->k[0], t33->v[0], (tree_t)nt3);
                gc_free(t33);
                return rt;
            }
            case TREE_4:
            {
                tree4_t t34 = (tree4_t)t3;
                tree3_t nt3 = tree3(t34->t[1], t34->k[1], t34->v[1], t34->t[2],
                    t34->k[2], t34->v[2], t34->t[3]);
                tree2_t nt2 = tree2(t2, k2, v2, t34->t[0]);
                tree_t rt = (tree_t)tree4(t0, k0, v0, t1, k1, v1, (tree_t)nt2,
                    t34->k[0], t34->v[0], (tree_t)nt3);
                gc_free(t34);
                return rt;
            }
            default:
                abort();
        }
    }
    else
        return (tree_t)t;
}

static tree_t tree4_destructive_fix_t3(tree4_t t, bool *reduced)
{
    if (*reduced)
    {
        *reduced = false;
        tree_t t0 = t->t[0], t1 = t->t[1], t2 = t->t[2], t3 = t->t[3];
        word_t k0 = t->k[0], v0 = t->v[0], k1 = t->k[1], v1 = t->v[1],
            k2 = t->k[2], v2 = t->v[2];
        switch (treetype(t2))
        {
            case TREE_2:
            {
                tree2_t t22 = (tree2_t)t2;
                tree3_t nt2 = tree3(t22->t[0], t22->k[0], t22->v[0], t22->t[1],
                    k2, v2, t3);
                gc_free(t22);
                return (tree_t)tree3(t0, k0, v0, t1, k1, v1, (tree_t)nt2);
            }
            case TREE_3:
            {
                tree3_t t23 = (tree3_t)t2;
                tree2_t nt2 = tree2(t23->t[0], t23->k[0], t23->v[0],
                    t23->t[1]);
                tree2_t nt3 = tree2(t23->t[2], k2, v2, t3);
                tree_t rt = (tree_t)tree4(t0, k0, v0, t1, k1, v1, (tree_t)nt2,
                    t23->k[1], t23->v[1], (tree_t)nt3);
                gc_free(t23);
                return rt;
            }
            case TREE_4:
            {
                tree4_t t24 = (tree4_t)t2;
                tree3_t nt2 = tree3(t24->t[0], t24->k[0], t24->v[0], t24->t[1],
                    t24->k[1], t24->v[1], t24->t[2]);
                tree2_t nt3 = tree2(t24->t[3], k2, v2, t3);
                tree_t rt = (tree_t)tree4(t0, k0, v0, t1, k1, v1, (tree_t)nt2,
                    t24->k[2], t24->v[2], (tree_t)nt3);
                gc_free(t24);
                return rt;
            }
            default:
                abort();
        }
    }
    else
        return (tree_t)t;
}

/*
 * Returns the size of a tree.
 */
extern size_t tree_size(tree_t t)
{
    switch (treetype(t))
    {
        case TREE_NIL:
            return 0;
        case TREE_2:
        {
            tree2_t t2 = (tree2_t)t;
            return 1 + tree_size(t2->t[0]) + tree_size(t2->t[1]);
        }
        case TREE_3:
        {
            tree3_t t3 = (tree3_t)t;
            return 2 + tree_size(t3->t[0]) + tree_size(t3->t[1]) +
                tree_size(t3->t[2]);
        }
        case TREE_4:
        {
            tree4_t t4 = (tree4_t)t;
            return 3 + tree_size(t4->t[0]) + tree_size(t4->t[1]) +
                tree_size(t4->t[2]) + tree_size(t4->t[3]);
        }
        default:
            abort();
    }
}

/*
 * Returns the depth of a tree.  This can be used to estimate tree_size
 */
extern size_t tree_depth(tree_t t)
{
    size_t depth = 0;
    while (true)
    {
        switch (treetype(t))
        {
            case TREE_NIL:
                return depth;
            case TREE_2:
            {
                depth++;
                tree2_t t2 = (tree2_t)t;
                t = t2->t[0];
                continue;
            }
            case TREE_3:
            {
                depth++;
                tree3_t t3 = (tree3_t)t;
                t = t3->t[0];
                continue;
            }
            case TREE_4:
            {
                depth++;
                tree4_t t4 = (tree4_t)t;
                t = t4->t[0];
                continue;
            }
        }
    }
}

#include <stdio.h>  // XXX

/*
 * Calculate the required size of a treeitr.
 */
extern size_t tree_itrsize(tree_t t)
{
    size_t size = 0;
    while (true)
    {
        switch (treetype(t))
        {
            case TREE_NIL:
                return size + sizeof(struct treeitr_s);
            case TREE_2:
            {
                tree2_t t2 = (tree2_t)t;
                size += sizeof(tree_t);
                t = t2->t[0];
                continue;
            }
            case TREE_3:
            {
                tree3_t t3 = (tree3_t)t;
                size += sizeof(tree_t);
                t = t3->t[0];
                continue;
            }
            case TREE_4:
            {
                tree4_t t4 = (tree4_t)t;
                size += sizeof(tree_t);
                t = t4->t[0];
                continue;
            }
        }
    }
}

/*
 * Initialise a treeitr.
 */
extern treeitr_t tree_itrinit(treeitr_t i, tree_t t)
{
    ssize_t idx = 0;
    while (true)
    {
        switch (treetype(t))
        {
            case TREE_NIL:
                i->stackptr = idx-1;
                return i;
            case TREE_2:
            {
                tree2_t t2 = (tree2_t)t;
                i->stack[idx++] = t;
                t = t2->t[0];
                continue;
            }
            case TREE_3:
            {
                tree3_t t3 = (tree3_t)t;
                i->stack[idx++] = t;
                t = t3->t[0];
                continue;
            }
            case TREE_4:
            {
                tree4_t t4 = (tree4_t)t;
                i->stack[idx++] = t;
                t = t4->t[0];
                continue;
            }
        }
    }
}

/*
 * Search for the next K-V pair above k0.
 */
extern treeitr_t tree_itrinit_geq(treeitr_t i, tree_t t, word_t k0,
    compare_t compare)
{
    ssize_t idx = 0;
    while (true)
    {
        switch (treetype(t))
        {
            case TREE_NIL:
                i->stackptr = idx-1;
                return i;
            case TREE_2:
            {
                tree2_t t2 = (tree2_t)t;
                int_t cmp = compare(k0, t2->k[0]);
                if (cmp < 0)
                {
                    i->stack[idx++] = t;
                    t = t2->t[0];
                    continue;
                }
                else if (cmp == 0)
                {
                    i->stack[idx] = t;
                    i->stackptr = idx;
                    return i;
                }
                else
                {
                    t = t2->t[1];
                    continue;
                }
            }
            case TREE_3:
            {
                tree3_t t3 = (tree3_t)t;
                int_t cmp = compare(k0, t3->k[0]);
                if (cmp < 0)
                {
                    i->stack[idx++] = t;
                    t = t3->t[0];
                    continue;
                }
                else if (cmp == 0)
                {
                    i->stack[idx] = t;
                    i->stackptr = idx;
                    return i;
                }
                cmp = compare(k0, t3->k[1]);
                if (cmp < 0)
                {
                    i->stack[idx++] = (tree_t)gc_settag(t, 1);
                    t = t3->t[1];
                    continue;
                }
                else if (cmp == 0)
                {
                    i->stack[idx] = (tree_t)gc_settag(t, 1);
                    i->stackptr = idx;
                    return i;
                }
                else
                {
                    t = t3->t[2];
                    continue;
                }
            }
            case TREE_4:
            {
                tree4_t t4 = (tree4_t)t;
                int_t cmp = compare(k0, t4->k[1]);
                if (cmp < 0)
                {
                    cmp = compare(k0, t4->k[0]);
                    if (cmp < 0)
                    {
                        i->stack[idx++] = t;
                        t = t4->t[0];
                        continue;
                    }
                    else if (cmp == 0)
                    {
                        i->stack[idx] = t;
                        i->stackptr = idx;
                        return i;
                    }
                    else
                    {
                        i->stack[idx++] = (tree_t)gc_settag(t, 1);
                        t = t4->t[1];
                        continue;
                    }
                }
                else if (cmp == 0)
                {
                    i->stack[idx] = (tree_t)gc_settag(t, 1);
                    i->stackptr = idx;
                    return i;
                }
                else
                {
                    cmp = compare(k0, t4->k[2]);
                    if (cmp < 0)
                    {
                        i->stack[idx++] = (tree_t)gc_settag(t, 2);
                        t = t4->t[2];
                        continue;
                    }
                    else if (cmp == 0)
                    {
                        i->stack[idx] = (tree_t)gc_settag(t, 2);
                        i->stackptr = idx;
                        return i;
                    }
                    else
                    {
                        t = t4->t[3];
                        continue;
                    }
                }
            }
        }
    }
}

/*
 * Get the current K-V from a treeitr.
 */
extern bool tree_get(treeitr_t i, word_t *k, word_t *v)
{
    ssize_t idx = i->stackptr;
    if (idx < 0)
        return false;
    tree_t t = i->stack[idx];
    switch (treetype(t))
    {
        case TREE_2:
        {
            tree2_t t2 = (tree2_t)t;
            if (k != NULL)
                *k = t2->k[0];
            if (v != NULL)
                *v = t2->v[0];
            return true;
        }
        case TREE_3:
        {
            size_t offset = gc_gettag(t);
            tree3_t t3 = (tree3_t)gc_deltag(t, offset);
            if (k != NULL)
                *k = t3->k[offset];
            if (v != NULL)
                *v = t3->v[offset];
            return true;
        }
        case TREE_4:
        {
            size_t offset = gc_gettag(t);
            tree4_t t4 = (tree4_t)gc_deltag(t, offset);
            if (k != NULL)
                *k = t4->k[offset];
            if (v != NULL)
                *v = t4->v[offset];
            return true;
        }
        default:
            abort();
    }
    return false;
}

/*
 * Advance the iterator.
 */
extern void tree_next(treeitr_t i)
{
    ssize_t idx = i->stackptr;
    if (idx < 0)
        return;
    tree_t t = i->stack[idx];
    switch (treetype(t))
    {
        case TREE_2:
        {
            tree2_t t2 = (tree2_t)t;
            t = t2->t[1];
            break;
        }
        case TREE_3:
        {
            size_t offset = gc_gettag(t);
            tree3_t t3 = (tree3_t)gc_deltag(t, offset);
            if (offset > 0)
                t = t3->t[2];
            else
            {
                i->stack[idx] = (tree_t)gc_settag(t3, 1);
                t = t3->t[1];
                idx++;
            }
            break;
        }
        case TREE_4:
        {
            size_t offset = gc_gettag(t);
            tree4_t t4 = (tree4_t)gc_deltag(t, offset);
            if (offset == 2)
                t = t4->t[3];
            else
            {
                offset++;
                i->stack[idx] = (tree_t)gc_settag(t4, offset);
                t = t4->t[offset];
                idx++;
            }
            break;
        }
        default:
            abort();
    }

    while (true)
    {
        switch (treetype(t))
        {
            case TREE_NIL:
                i->stackptr = idx-1;
                return;
            case TREE_2:
            {
                tree2_t t2 = (tree2_t)t;
                i->stack[idx++] = t;
                t = t2->t[0];
                continue;
            }
            case TREE_3:
            {
                tree3_t t3 = (tree3_t)t;
                i->stack[idx++] = t;
                t = t3->t[0];
                continue;
            }
            case TREE_4:
            {
                tree4_t t4 = (tree4_t)t;
                i->stack[idx++] = t;
                t = t4->t[0];
                continue;
            }
        }
    }
}

/*
 * Tree map.
 */
extern tree_t tree_map(tree_t t, word_t arg, valmap_t map)
{
    switch (treetype(t))
    {
        case TREE_NIL:
            return TREE_EMPTY;
        case TREE_2:
        {
            tree2_t t2 = (tree2_t)t;
            tree_t t0 = tree_map(t2->t[0], arg, map);
            word_t v0 = map(arg, t2->k[0], t2->v[0]);
            tree_t t1 = tree_map(t2->t[1], arg, map);
            return (tree_t)tree2(t0, t2->k[0], v0, t1);
        }
        case TREE_3:
        {
            tree3_t t3 = (tree3_t)t;
            tree_t t0 = tree_map(t3->t[0], arg, map);
            word_t v0 = map(arg, t3->k[0], t3->v[0]);
            tree_t t1 = tree_map(t3->t[1], arg, map);
            word_t v1 = map(arg, t3->k[1], t3->v[1]);
            tree_t t2 = tree_map(t3->t[2], arg, map);
            return (tree_t)tree3(t0, t3->k[0], v0, t1, t3->k[1], v1, t2);
        }
        case TREE_4:
        {
            tree4_t t4 = (tree4_t)t;
            tree_t t0 = tree_map(t4->t[0], arg, map);
            word_t v0 = map(arg, t4->k[0], t4->v[0]);
            tree_t t1 = tree_map(t4->t[1], arg, map);
            word_t v1 = map(arg, t4->k[1], t4->v[1]);
            tree_t t2 = tree_map(t4->t[2], arg, map);
            word_t v2 = map(arg, t4->k[2], t4->v[2]);
            tree_t t3 = tree_map(t4->t[3], arg, map);
            return (tree_t)tree4(t0, t4->k[0], v0, t1, t4->k[1], v1, t2,
                t4->k[2], v2, t3);
        }
        default:
            abort();
    }
}

/*
 * Tree map.
 */
extern void tree_destructive_map(tree_t t, word_t arg, valmap_t map)
{
    switch (treetype(t))
    {
        case TREE_NIL:
            return;
        case TREE_2:
        {
            tree2_t t2 = (tree2_t)t;
            tree_destructive_map(t2->t[0], arg, map);
            t2->v[0] = map(arg, t2->k[0], t2->v[0]);
            tree_destructive_map(t2->t[1], arg, map);
            return;
        }
        case TREE_3:
        {
            tree3_t t3 = (tree3_t)t;
            tree_destructive_map(t3->t[0], arg, map);
            t3->v[0] = map(arg, t3->k[0], t3->v[0]);
            tree_destructive_map(t3->t[1], arg, map);
            t3->v[1] = map(arg, t3->k[1], t3->v[1]);
            tree_destructive_map(t3->t[2], arg, map);
            return;
        }
        case TREE_4:
        {
            tree4_t t4 = (tree4_t)t;
            tree_destructive_map(t4->t[0], arg, map);
            t4->v[0] = map(arg, t4->k[0], t4->v[0]);
            tree_destructive_map(t4->t[1], arg, map);
            t4->v[1] = map(arg, t4->k[1], t4->v[1]);
            tree_destructive_map(t4->t[2], arg, map);
            t4->v[2] = map(arg, t4->k[2], t4->v[2]);
            return;
        }
    }
}

