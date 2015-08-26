/*
 * map.h
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
#ifndef __MAP_H
#define __MAP_H

/*
 * This is merely tree.h but rebranded with a generic name.
 */

#include "tree.h"

/*
 * Abstract map type.
 */
typedef tree_t map_t;

/*
 * An empty map.
 */
#define MAP_EMPTY           TREE_EMPTY

/*
 * Map functions.
 */
static inline map_t map_init(void)
{
    return tree_init();
}
static inline bool map_isempty(map_t t)
{
    return tree_isempty((tree_t)t);
}
static inline bool map_issingleton(map_t t)
{
    return tree_issingleton((tree_t)t);
}
static inline bool map_search(map_t t, word_t k, word_t *v, compare_t compare)
{
    return tree_search((tree_t)t, k, v, compare);
}
static inline bool map_search_any(map_t t, word_t *k, word_t *v)
{
    return tree_search_any((tree_t)t, k, v);
}
static inline bool map_search_min(map_t t, word_t *k, word_t *v)
{
    return tree_search_min((tree_t)t, k, v);
}
static inline bool map_search_max(map_t t, word_t *k, word_t *v)
{
    return tree_search_max((tree_t)t, k, v);
}
static inline bool map_search_lt(map_t t, word_t k0, word_t *k, word_t *v,
    compare_t compare)
{
    return tree_search_lt((tree_t)t, k0, k, v, compare);
}
static inline bool map_search_gt(map_t t, word_t k0, word_t *k, word_t *v,
    compare_t compare)
{
    return tree_search_gt((tree_t)t, k0, k, v, compare);
}
static inline map_t map_insert(map_t t, word_t k, word_t v, compare_t compare)
{
    return tree_insert((tree_t)t, k, v, compare);
}
static inline map_t map_destructive_insert(map_t t, word_t k, word_t v,
    compare_t compare)
{
    return tree_destructive_insert((tree_t)t, k, v, compare);
}
static inline map_t map_delete(map_t t, word_t k, word_t *v, compare_t compare)
{
    return tree_delete((tree_t)t, k, v, compare);
}
static inline map_t map_destructive_delete(map_t t, word_t k, word_t *v,
    compare_t compare)
{
    return tree_destructive_delete((tree_t)t, k, v, compare);
}
static inline map_t map_delete_min(map_t t, word_t *k, word_t *v)
{
    return tree_delete_min((tree_t)t, k, v);
}
static inline map_t map_delete_max(map_t t, word_t *k, word_t *v)
{
    return tree_delete_max((tree_t)t, k, v);
}
static inline size_t map_size(map_t t)
{
    return tree_size((tree_t)t);
}
static inline size_t map_depth(map_t t)
{
    return tree_depth((tree_t)t);
}
static inline map_t map_map(map_t t, word_t arg, valmap_t map)
{
    return tree_map((tree_t)t, arg, map);
}

/*
 * Map itrators.
 */
typedef treeitr_t mapitr_t;
static inline size_t map_itrsize(map_t t)
{
    return tree_itrsize((tree_t)t);
}
static inline mapitr_t map_itrinit(mapitr_t i, map_t t)
{
    return (mapitr_t)tree_itrinit(i, (tree_t)t);
}
static inline mapitr_t map_itrinit_geq(mapitr_t i, map_t t, word_t k0,
    compare_t compare)
{
    return (mapitr_t)tree_itrinit_geq(i, (tree_t)t, k0, compare);
}
static inline bool map_get(mapitr_t i, word_t *k, word_t *v)
{
    return tree_get(i, k, v);
}
static inline void map_next(mapitr_t i)
{
    return tree_next(i);
}

/*
 * Typed maps.
 */
#define MAP_DECL(n, tk, tv, cmp)                                            \
    union n##_k_cast_u                                                      \
    {                                                                       \
        word_t w;                                                           \
        tk k;                                                               \
    };                                                                      \
    union n##_v_cast_u                                                      \
    {                                                                       \
        word_t w;                                                           \
        tv v;                                                               \
    };                                                                      \
    static inline word_t n##_cast_k(tk k)                                   \
    {                                                                       \
        union n##_k_cast_u u;                                               \
        u.k = k;                                                            \
        return u.w;                                                         \
    }                                                                       \
    static inline word_t n##_cast_v(tv v)                                   \
    {                                                                       \
        union n##_v_cast_u u;                                               \
        u.v = v;                                                            \
        return u.w;                                                         \
    }                                                                       \
    struct n##_s                                                            \
    {                                                                       \
        word_t dummy;                                                       \
    };                                                                      \
    typedef struct n##_s *n##_t;                                            \
    typedef mapitr_t n##itr_t;                                              \
    static inline n##_t n##_init(void)                                      \
    {                                                                       \
        return (n##_t)map_init();                                           \
    }                                                                       \
    static inline bool n##_isempty(n##_t t)                                 \
    {                                                                       \
        return map_isempty((map_t)t);                                       \
    }                                                                       \
    static inline bool n##_issingleton(n##_t t)                             \
    {                                                                       \
        return map_issingleton((map_t)t);                                   \
    }                                                                       \
    static inline bool n##_search(n##_t t, tk k, tv *v)                     \
    {                                                                       \
        return map_search((map_t)t, n##_cast_k(k), (word_t *)v,             \
            (compare_t)cmp);                                                \
    }                                                                       \
    static inline bool n##_search_any(n##_t t, tk *k, tv *v)                \
    {                                                                       \
        return map_search_any((map_t)t, (word_t *)k, (word_t *)v);          \
    }                                                                       \
    static inline bool n##_search_min(n##_t t, tk *k, tv *v)                \
    {                                                                       \
        return map_search_min((map_t)t, (word_t *)k, (word_t *)v);          \
    }                                                                       \
    static inline bool n##_search_max(n##_t t, tk *k, tv *v)                \
    {                                                                       \
        return map_search_max((map_t)t, (word_t *)k, (word_t *)v);          \
    }                                                                       \
    static inline bool n##_search_lt(n##_t t, tk k0, tk *k, tv *v)          \
    {                                                                       \
        return map_search_lt((map_t)t, n##_cast_k(k0), (word_t *)k,         \
            (word_t *)v, (compare_t)cmp);                                   \
    }                                                                       \
    static inline bool n##_search_gt(n##_t t, tk k0, tk *k, tv *v)          \
    {                                                                       \
        return map_search_gt((map_t)t, n##_cast_k(k0), (word_t *)k,         \
            (word_t *)v, (compare_t)cmp);                                   \
    }                                                                       \
    static inline n##_t n##_insert(n##_t t, tk k, tv v)                     \
    {                                                                       \
        return (n##_t)map_insert((map_t)t, n##_cast_k(k), n##_cast_v(v),    \
            (compare_t)cmp);                                                \
    }                                                                       \
    static inline n##_t n##_destructive_insert(n##_t t, tk k, tv v)         \
    {                                                                       \
        return (n##_t)map_destructive_insert((map_t)t, n##_cast_k(k),       \
            n##_cast_v(v), (compare_t)cmp);                                 \
    }                                                                       \
    static inline n##_t n##_delete(n##_t t, tk k, tv *v)                    \
    {                                                                       \
        return (n##_t)map_delete((map_t)t, n##_cast_k(k), (word_t *)v,      \
            (compare_t)cmp);                                                \
    }                                                                       \
    static inline n##_t n##_destructive_delete(n##_t t, tk k, tv *v)        \
    {                                                                       \
        return (n##_t)map_destructive_delete((map_t)t, n##_cast_k(k),       \
            (word_t *)v, (compare_t)cmp);                                   \
    }                                                                       \
    static inline n##_t n##_delete_min(n##_t t, tk *k, tv *v)               \
    {                                                                       \
        return (n##_t)map_delete_min((map_t)t, (word_t *)k, (word_t *)v);   \
    }                                                                       \
    static inline n##_t n##_delete_max(n##_t t, tk *k, tv *v)               \
    {                                                                       \
        return (n##_t)map_delete_max((map_t)t, (word_t *)k, (word_t *)v);   \
    }                                                                       \
    static inline size_t n##_size(n##_t t)                                  \
    {                                                                       \
        return map_size((map_t)t);                                          \
    }                                                                       \
    static inline size_t n##_depth(n##_t t)                                 \
    {                                                                       \
        return map_depth((map_t)t);                                         \
    }                                                                       \
    static inline n##_t n##_map(n##_t t, word_t arg, valmap_t map)          \
    {                                                                       \
        return (n##_t)map_map((map_t)t, arg, map);                          \
    }                                                                       \
    static inline size_t n##_itrsize(n##_t t)                               \
    {                                                                       \
        return map_itrsize((map_t)t);                                       \
    }                                                                       \
    static inline n##itr_t n##_itrinit(n##itr_t i, n##_t t)                 \
    {                                                                       \
        return (n##itr_t)map_itrinit((mapitr_t)i, (map_t)t);                \
    }                                                                       \
    static inline n##itr_t n##_itrinit_geq(n##itr_t i, n##_t t, tk k)       \
    {                                                                       \
        return (n##itr_t)map_itrinit_geq((mapitr_t)i, (map_t)t,             \
            n##_cast_k(k), (compare_t)cmp);                                 \
    }                                                                       \
    static inline bool n##_get(n##itr_t i, tk *k, tv *v)                    \
    {                                                                       \
        return map_get(i, (word_t *)k, (word_t *)v);                        \
    }                                                                       \
    static inline void n##_next(n##itr_t i)                                 \
    {                                                                       \
        map_next(i);                                                        \
    }                                                                       \
    static inline n##itr_t n##itr(n##_t t)                                  \
    {                                                                       \
        return n##_itrinit((n##itr_t)gc_malloc(n##_itrsize(t)), t);         \
    }                                                                       \
    static inline n##itr_t n##itr_geq(n##_t t, tk k)                        \
    {                                                                       \
        return n##_itrinit_geq((n##itr_t)gc_malloc(n##_itrsize(t)), t, k);  \
    }                                                                       \
    typedef int n##_semicolon_dummy_t

#endif      /* __MAP_H */
