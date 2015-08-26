/*
 * set.h
 * Copyright (C) 2011 National University of Singapore
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
#ifndef __SET_H
#define __SET_H

#include "tree.h"
#include "word.h"

/*
 * A set is implemented as a tree_t.  We don't use the tree's value field.
 * In the future we may specialise the implementation of tree_t for set_t.
 */

/*
 * Abstract set type.
 */
typedef tree_t set_t;

/*
 * An empty set.
 */
#define SET_EMPTY           TREE_EMPTY

/*
 * Set functions.
 */
static inline set_t set_init(void)
{
    return tree_init();
}
static inline bool set_isempty(set_t t)
{
    return tree_isempty((tree_t)t);
}
static inline bool set_issingleton(set_t t)
{
    return tree_issingleton((tree_t)t);
}
static inline bool set_element(set_t t, word_t k, compare_t compare)
{
    return tree_search(t, k, NULL, compare);
}
static inline bool set_element_any(set_t t, word_t *k)
{
    return tree_search_any(t, k, NULL);
}
static inline bool set_element_min(set_t t, word_t *k)
{
    return tree_search_min(t, k, NULL);
}
static inline bool set_element_max(set_t t, word_t *k)
{
    return tree_search_max(t, k, NULL);
}
static inline bool set_element_lt(set_t t, word_t k0, word_t *k,
    compare_t compare)
{
    return tree_search_lt(t, k0, k, NULL, compare);
}
static inline bool set_element_gt(set_t t, word_t k0, word_t *k,
    compare_t compare)
{
    return tree_search_gt(t, k0, k, NULL, compare);
}
static inline set_t set_insert(set_t t, word_t k, compare_t compare)
{
    return tree_insert(t, k, (word_t)0, compare);
}
static inline set_t set_delete(set_t t, word_t k, compare_t compare)
{
    return tree_delete(t, k, NULL, compare);
}
static inline set_t set_delete_min(set_t t, word_t *k)
{
    return tree_delete_min(t, k, NULL);
}
static inline set_t set_delete_max(set_t t, word_t *k)
{
    return tree_delete_max(t, k, NULL);
}
extern set_t set_union(set_t a, set_t b);
extern set_t set_intersect(set_t a, set_t b);
extern set_t set_diff(set_t a, set_t b);
static inline size_t set_size(set_t t)
{
    return tree_size(t);
}

/*
 * Set itrators.
 */
typedef treeitr_t setitr_t;
static inline size_t set_itrsize(set_t t)
{
    return tree_itrsize(t);
}
static inline setitr_t set_itrinit(setitr_t i, set_t t)
{
    return tree_itrinit(i, t);
}
static inline setitr_t set_itrinit_geq(setitr_t i, set_t t, word_t k,
    compare_t compare)
{
    return tree_itrinit_geq(i, t, k, compare);
}
static inline bool set_get(setitr_t i, word_t *k)
{
    return tree_get(i, k, NULL);
}
static inline void set_next(setitr_t i)
{
    return tree_next(i);
}
#define setitr(t)           set_itrinit(alloca(set_itrsize(t)), (t))
#define setitr_geq(t, k)    set_itrinit_geq(alloca(set_itrsize(t)), (t), (k))

/*
 * Typed sets.
 */
#define SET_DECL(n, tk, cmp)                                                \
    union n##_k_cast_u                                                      \
    {                                                                       \
        word_t w;                                                           \
        tk k;                                                               \
    };                                                                      \
    static inline word_t n##_cast_k(tk k)                                   \
    {                                                                       \
        union n##_k_cast_u u;                                               \
        u.k = k;                                                            \
        return u.w;                                                         \
    }                                                                       \
    struct n##_s                                                            \
    {                                                                       \
        word_t dummy;                                                       \
    };                                                                      \
    typedef struct n##_s *n##_t;                                            \
    typedef setitr_t n##itr_t;                                              \
    static inline n##_t n##_init(void)                                      \
    {                                                                       \
        return (n##_t)set_init();                                           \
    }                                                                       \
    static inline bool n##_isempty(n##_t t)                                 \
    {                                                                       \
        return set_isempty((set_t)t);                                       \
    }                                                                       \
    static inline bool n##_issingleton(n##_t t)                             \
    {                                                                       \
        return set_issingleton((set_t)t);                                   \
    }                                                                       \
    static inline bool n##_element(n##_t t, tk k)                           \
    {                                                                       \
        return set_element((set_t)t, n##_cast_k(k), (compare_t)cmp);        \
    }                                                                       \
    static inline bool n##_element_any(n##_t t, tk *k)                      \
    {                                                                       \
        return set_element_any((set_t)t, (word_t *)k);                      \
    }                                                                       \
    static inline bool n##_element_min(n##_t t, tk *k)                      \
    {                                                                       \
        return set_element_min((set_t)t, (word_t *)k);                      \
    }                                                                       \
    static inline bool n##_element_max(n##_t t, tk *k)                      \
    {                                                                       \
        return set_element_max((set_t)t, (word_t *)k);                      \
    }                                                                       \
    static inline bool n##_element_lt(n##_t t, tk k0, tk *k)                \
    {                                                                       \
        return set_element_lt((set_t)t, n##_cast_k(k0), (word_t *)k,        \
            (compare_t)cmp);                                                \
    }                                                                       \
    static inline bool n##_element_gt(n##_t t, tk k0, tk *k)                \
    {                                                                       \
        return set_element_gt((set_t)t, n##_cast_k(k0), (word_t *)k,        \
            (compare_t)cmp);                                                \
    }                                                                       \
    static inline n##_t n##_insert(n##_t t, tk k)                           \
    {                                                                       \
        return (n##_t)set_insert((set_t)t, n##_cast_k(k), (compare_t)cmp);  \
    }                                                                       \
    static inline n##_t n##_delete(n##_t t, tk k)                           \
    {                                                                       \
        return (n##_t)set_delete((set_t)t, n##_cast_k(k), (compare_t)cmp);  \
    }                                                                       \
    static inline n##_t n##_delete_min(n##_t t, tk *k)                      \
    {                                                                       \
        return (n##_t)set_delete_min((set_t)t, (word_t *)k);                \
    }                                                                       \
    static inline n##_t n##_delete_max(n##_t t, tk *k)                      \
    {                                                                       \
        return (n##_t)set_delete_max((set_t)t, (word_t *)k);                \
    }                                                                       \
    static inline n##_t n##_union(n##_t a, n##_t b)                         \
    {                                                                       \
        return (n##_t)set_union((set_t)a, (set_t)b);                        \
    }                                                                       \
    static inline n##_t n##_intersect(n##_t a, n##_t b)                     \
    {                                                                       \
        return (n##_t)set_intersect((set_t)a, (set_t)b);                    \
    }                                                                       \
    static inline n##_t n##_diff(n##_t a, n##_t b)                          \
    {                                                                       \
        return (n##_t)set_diff((set_t)a, (set_t)b);                         \
    }                                                                       \
    static inline size_t n##_size(n##_t t)                                  \
    {                                                                       \
        return set_size((set_t)t);                                          \
    }                                                                       \
    static inline size_t n##_itrsize(n##_t t)                               \
    {                                                                       \
        return set_itrsize((set_t)t);                                       \
    }                                                                       \
    static inline n##itr_t n##_itrinit(n##itr_t i, n##_t t)                 \
    {                                                                       \
        return set_itrinit(i, (set_t)t);                                    \
    }                                                                       \
    static inline n##itr_t n##_itrinit_geq(n##itr_t i, n##_t t, tk k)       \
    {                                                                       \
        return set_itrinit_geq(i, (set_t)t, n##_cast_k(k), (compare_t)cmp); \
    }                                                                       \
    static inline bool n##_get(n##itr_t i, tk *k)                           \
    {                                                                       \
        return set_get(i, (word_t *)k);                                     \
    }                                                                       \
    static inline void n##_next(n##itr_t i)                                 \
    {                                                                       \
        set_next(i);                                                        \
    }                                                                       \
    static inline n##itr_t n##itr(n##_t t)                                  \
    {                                                                       \
        return n##_itrinit(gc_malloc(n##_itrsize(t)), t);                   \
    }                                                                       \
    static inline n##itr_t n##itr_geq(n##_t t, tk k)                        \
    {                                                                       \
        return n##_itrinit_geq(gc_malloc(n##_itrsize(t)), t, k);            \
    }                                                                       \
    typedef int n##_semicolon_dummy_t

#endif      /* __SET_H */
