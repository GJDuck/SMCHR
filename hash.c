/*
 * hash.c
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

#include "solver.h"

#define SEED_X      HASH(0x77C1715111751755ull, 0xEF7B4EBD73F2925Dull)
#define SEED_Y      HASH(0xA77AE5B4597B8D35ull, 0x722304B423E2E4C6ull)
#define SEED_Z      HASH(0xE7AAA8D3D6C8E739ull, 0x5CDE17F4BD075894ull)
#define SEED_W      HASH(0xDF3128AF54643DE6ull, 0x7CA6387B271BE1CBull)
#define SEED_INC    HASH(0x0000000000009FD0ull, 0x000000000000B43Cull)

hash_t seed_x = SEED_X;

/*
 * Prototypes.
 */
static hash_t hash_data(const char *data, size_t len, hash_t key);

/*
 * Reset this module.
 */
extern void hash_reset(void)
{
    hash_t old = SEED_X;
    seed_x = old;
}

/*
 * Get a new random hash value.
 */
extern hash_t hash_new(void)
{
    hash_t hash = seed_x;
    hash_t seed_y = SEED_Y;
    hash = hash_mix(hash, seed_y);
    hash_t inc = SEED_INC;
    hash_t seed_z = SEED_Z;
    hash = hash_mix(hash, seed_z);
    seed_x += inc;
    hash_t seed_w = SEED_W;
    hash = hash_mix(hash, seed_w);
    hash_t zero = HASH(0, 0);
    hash = hash_mix(hash, zero);
    return hash;
}

/*
 * Hash a str_t.
 */
extern hash_t hash_string(str_t s)
{
    size_t len = s->len;
    hash_t key = STR_KEY;
    return hash_data(s->chars, len, key);
}

/*
 * Hash a C string.
 */
extern hash_t hash_cstring(const char *s)
{
    size_t len = strlen(s);
    hash_t key = CSTR_KEY;
    return hash_data(s, len, key);
}

/*
 * Hash generic data.
 */
static hash_t hash_data(const char *data, size_t len, hash_t key)
{
    hash_t hash = HASH(0x8E93668B31ACE316ull, 0xE9270DEF701B0ECFull);
    size_t i;
    for (i = 0; i + sizeof(hash_t) <= len; i += sizeof(hash_t))
    {
        hash_t chunk = *(hash_t *)(data + i);
        hash = hash_join(i, hash, hash_hash(chunk, key));
    }
    char buf[sizeof(hash_t)] = {0};
    memcpy(buf, data+i, len-i);
    const char *buf1 = buf;         // Suppress warning.
    hash_t chunk = *(hash_t *)buf1;
    hash = hash_join(i, hash, hash_hash(chunk, key));
    return hash;
}

/*
 * Hash a func_t.
 */
extern hash_t hash_func(func_t f)
{
    hash_t key = FUNC_KEY;
    hash_t hash = hash_word((word_t)f->atom, key);
    size_t aty = atom_arity(f->atom);
    for (size_t i = 0; i < aty; i++)
        hash = hash_join(i, hash, hash_term(f->args[i]));
    return hash;
}

/*
 * Hash a term.
 */
extern hash_t hash_term(term_t t)
{
    switch (type(t))
    {
        case VAR:
            return hash_var(var(t));
        case NIL:
            return hash_nil();
        case BOOL:
            return hash_bool(boolean(t));
        case NUM:
            return hash_num(num(t));
        case ATOM:
            return hash_atom(atom(t));
        case STR:
            return hash_string(string(t));
        case FOREIGN:
            return hash_foreign(foreign(t));
        case FUNC:
            return hash_func(func(t));
        default:
        {
            hash_t dummy = HASH(0, 0);
            return dummy;
        }
    }
}

/*
 * Calculate the hash value of a constraint w.r.t. a lookup.
 */
extern hash_t hash_lookup(hash_t hash, lookup_t lookup, cons_t c)
{
    while (*lookup >= 0)
    {
        size_t idx = (size_t)*lookup;
        term_t arg = c->args[idx];
        hash = hash_join(idx, hash, hash_term(arg));
        lookup++;
    }
    return hash;
}

/*
 * Similar to above but use new hash value for term x.
 */
extern hash_t hash_update_lookup(hash_t hash, lookup_t lookup, cons_t c,
    hash_t xkey_old, hash_t xkey_new)
{
    while (*lookup >= 0)
    {
        size_t idx = (size_t)*lookup;
        term_t arg = c->args[idx];
        hash_t arg_hash = hash_term(arg);
        if (hash_iseq(arg_hash, xkey_old))
            arg_hash = xkey_new;
        hash = hash_join(idx, hash, arg_hash);
        lookup++;
    }
    return hash;
}

/*
 * Similar to hash_update_lookup, but for entire constraint.
 */
extern hash_t hash_update_cons(hash_t hash, cons_t c, hash_t xkey_old,
    hash_t xkey_new)
{
    size_t aty = c->sym->arity;
    for (size_t i = 0; i < aty; i++)
    {
        term_t arg = c->args[i];
        hash_t arg_hash = hash_term(arg);
        if (hash_iseq(arg_hash, xkey_old))
            arg_hash = xkey_new;
        hash = hash_join(i, hash, arg_hash);
    }
    return hash;
}

