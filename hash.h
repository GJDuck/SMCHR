/*
 * hash.h
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
#ifndef __HASH_H
#define __HASH_H

#include "log.h"
#include "word.h"
#include "show.h"

/*
 * We use 128-bit hash values & never check for collisions.  Even with the
 * birthday attack, it is extremely unlikely a collision will occur.
 */

typedef long long int hash_t __attribute__ ((vector_size (16)));
#define HASH(x, y)      {(long long int)(x), (long long int)(y)}

/*
 * For modern CPUs, we use the AESENC instruction.  This instruction is
 * very good at mixing bits, and only takes a few cycles, so is ideal for
 * (non-cryptographic) hashing.  For older CPUs we fall back on a more
 * traditional hash function.
 */
#ifdef VINTAGE_AMD64
static inline hash_t ALWAYS_INLINE hash_mix(hash_t hash, hash_t key)
{
    uint64_t v0 = (uint64_t)hash[0];
    uint64_t v1 = (uint64_t)hash[1];
    uint64_t k0 = 0x91C8B2CD5DC7CF97ull;    // Prime
    uint64_t k1 = 0x3FF859190F2E7EECull;
    uint64_t k2 = 0xF51FA505A0D86887ull;    // Prime
    uint64_t k3 = 0x4B2B488BEF392CC5ull;
    v0 += v1 * k0 + k1;
    v1 += v0 * k2 + k3;
    v0 += v1;
    hash_t r = HASH(v1, v0);
    r ^= key;
    return r;
}
#else
#define hash_mix        __builtin_ia32_aesenc128
#endif      /* VINTAGE_AMD64 */

#define NUM_KEY         HASH(0xB3D8254F80AB0402ull, 0x0F13352369144280ull)
#define ATOM_KEY        HASH(0x42BDABAE662AEF5Dull, 0x7974F08C72E942C4ull)
#define STR_KEY         HASH(0x5BE647996DA04082ull, 0x866834E8AAA4C2F5ull)
#define FUNC_KEY        HASH(0x8765288DF593610Dull, 0x0D2025A95A7EADCEull)
#define FOREIGN_KEY     HASH(0x9EF20E120A829063ull, 0x03CC39C7C5FD04ECull)
#define JOIN_KEY        HASH(0x959CB2258A36855Aull, 0x6CFE3F874645E9BDull)
#define CSTR_KEY        HASH(0xB5F8EA97652EF6A9ull, 0x85E31FF138F8D75Dull)

#define HASH_KEY_0      HASH(0x0B809EFD1A8B8B91ull, 0x3CDC048E249E7390ull);
#define HASH_KEY_1      HASH(0x63257718E051C1BBull, 0x4AC893F04E510C80ull)

/*
 * Basic hash function.
 */
static inline hash_t ALWAYS_INLINE hash_hash(hash_t hash, hash_t key)
{
    hash_t key_0 = HASH_KEY_0;
    hash ^= key_0;
    hash = hash_mix(hash, key);
    hash_t key_1 = HASH_KEY_1;
    hash = hash_mix(hash, key_1);
    return hash;
}
static inline hash_t ALWAYS_INLINE hash_word(word_t x, hash_t key)
{
    hash_t hash = HASH(x, x);
    return hash_hash(hash, key);
}

/*
 * Hash basic types.
 */
static inline hash_t ALWAYS_INLINE hash_sym(sym_t sym)
{
    return sym->hash;
}
static inline hash_t ALWAYS_INLINE hash_var_0(var_t x)
{
    return ((svar_t)x)->hash;
}
static inline var_t ALWAYS_INLINE solver_deref_var(var_t x0);
static inline hash_t ALWAYS_INLINE hash_var(var_t x)
{
    x = solver_deref_var(x);
    return hash_var_0(x);
}
static inline hash_t ALWAYS_INLINE hash_nil(void)
{
    hash_t hash = HASH(0xC1EF21539659BF63ull, 0x8C25BF6D0A5A2908ull);
    return hash;
}
static inline hash_t ALWAYS_INLINE hash_bool(bool_t b)
{
    if (b)
    {
        hash_t hash = HASH(0xF362919990EDAAD9ull, 0x54A137222D422EC8ull);
        return hash;
    }
    else
    {
        hash_t hash = HASH(0x7A9E139232CD6212ull, 0x4735C9B31D25D85Aull);
        return hash;
    }
}
static inline hash_t ALWAYS_INLINE hash_atom(atom_t a)
{
    hash_t key = ATOM_KEY;
    return hash_word((word_t)a, key);
}
static inline hash_t ALWAYS_INLINE hash_foreign(foreign_t f)
{
    hash_t key = FOREIGN_KEY;
    return hash_word((word_t)f, key);
}
static inline hash_t ALWAYS_INLINE hash_num(num_t n)
{
    hash_t key = NUM_KEY;
    return hash_word(word_makedouble(n), key);
}
extern hash_t PURE hash_string(str_t s);
extern hash_t PURE hash_func(func_t f);
extern hash_t PURE hash_cstring(const char *s);

/*
 * Join hash values.
 */
static inline hash_t ALWAYS_INLINE hash_join(size_t idx, hash_t x, hash_t y)
{
    hash_t key_idx = HASH(idx, idx);
    hash_t key = JOIN_KEY;
    hash_t hash = (x ^ key) + key_idx;
    hash = hash_mix(hash, y);
    hash_t zero = HASH(0, 0);
    hash = hash_mix(hash, zero);
    return hash;
}

/*
 * Hash a term.
 */
extern hash_t PURE hash_term(term_t t);

/*
 * Create a new hash value.
 */
extern hash_t hash_new(void);

/*
 * Hash a constraint.
 */
static inline hash_t ALWAYS_INLINE hash_cons(cons_t c)
{
    hash_t hash = hash_sym(c->sym);
    for (size_t i = 0; i < c->sym->arity; i++)
        hash = hash_join(i, hash, hash_term(c->args[i]));
    return hash;
}

/*
 * Hash lookups.
 */
extern hash_t hash_lookup(hash_t hash, lookup_t lookup, cons_t c);
extern hash_t hash_update_lookup(hash_t hash, lookup_t lookup, cons_t c,
    hash_t xkey_old, hash_t xkey_new);
extern hash_t hash_update_cons(hash_t hash, cons_t c, hash_t xkey_old,
    hash_t xkey_new);

/*
 * Test if two hash values are equal.
 */
static inline bool ALWAYS_INLINE hash_iseq(hash_t x, hash_t y)
{
#ifdef VINTAGE_AMD64
    return (x[0] == y[0] && x[1] == y[1]);
#else
    return (bool)__builtin_ia32_ptestc128(x, y);
#endif      /* VINTAGE_AMD64 */
}

/*
 * Reset this module.
 */
extern void hash_reset(void);

#endif      /* __HASH_H */
