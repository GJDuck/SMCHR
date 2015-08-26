/*
 * store.c
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

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "misc.h"
#include "solver.h"

#define STORE_GROWTH_FACTOR 3

#define STORE_INIT_SHIFT    12
#define STORE_INIT_LEN      (1 << STORE_INIT_SHIFT)
#define STORE_MAX_SHIFT     28
#define STORE_MAX_LEN       (1 << STORE_MAX_SHIFT)
#define STORE_DELETE_FREQ   16

/*
 * Store entry.
 */
typedef struct entry_s *entry_t;
struct entry_s
{
    hash_t key;             // Entry's key.
    entry_t next;           // Next entry.
    conslist_t cons;        // Entry's constraints.
    conslist_t tail;        // Tail of 'cons'.
};

/*
 * Move info.
 */
struct moveinfo_s
{
    entry_t old;
    entry_t new;
};
typedef struct moveinfo_s *moveinfo_t;

/*
 * Prototypes.
 */
static void store_insert_constraint(hash_t key, bool primary, cons_t c);
static void store_delete_constraint(hash_t key);
static void store_move_entry(hash_t key_old, hash_t key_new);
static void store_unmove_entry(word_t arg);
static void store_grow(void);

/*
 * The global store:
 */
static size_t store_shift;
static size_t store_len;
static size_t store_usage;
static entry_t *store;

/*
 * Initialize/reset the store.
 */
extern void solver_init_store(void)
{
#ifndef VINTAGE_AMD64
    unsigned a, b, c, d;
    asm volatile ("cpuid": "=a"(a), "=b"(b), "=c"(c), "=d"(d): "a"(1));
    if ((c & 0x02000000) == 0)
        fatal("failed to initialize the store; CPU support for the "
            "\"Advanced Encryption Standard (AES) Instruction Set\" is "
            "required");
#endif      /* VINTAGE_AMD64 */

    size_t size = STORE_MAX_LEN*sizeof(entry_t);
    store = buffer_alloc(size);
    store_shift = STORE_INIT_SHIFT;
    store_len = STORE_INIT_LEN;
    store_usage = 0;
    if (!gc_dynamic_root((void **)&store, &store_len, sizeof(entry_t)))
        panic("failed to set GC dynamic root for constraint store: %s",
            strerror(errno));
}
extern void solver_reset_store(void)
{
    buffer_free(store, store_len*sizeof(entry_t));
    store_shift = STORE_INIT_SHIFT;
    store_len = STORE_INIT_LEN;
    store_usage = 0;
}

/*
 * Store primitive operations.
 */
static inline size_t store_mask(void)
{
    return (1 << store_shift) - 1;
}
static inline size_t store_index(hash_t hash)
{
    size_t idx = (size_t)hash[0];
    return idx & store_mask();
}

/*
 * Search for constraints.
 */
extern conslist_t solver_store_search(hash_t key)
{
    debug("!bSEARCH!d [key=%.16llX%.16llX]", key[0], key[1]);

    size_t idx = store_index(key);
    entry_t entry = store[idx];
    while (entry != NULL)
    {
        if (hash_iseq(key, entry->key))
            return entry->cons;
        entry = entry->next;
    }
    return NULL;
}

/*
 * Insert a constraint (primary key only).
 */
extern void solver_store_insert_primary(hash_t key, cons_t c)
{
    store_insert_constraint(key, true, c);
}

/*
 * Insert a constraint.
 */
extern void solver_store_insert(cons_t c)
{
    sym_t sym = c->sym;
    hash_t key_sym = hash_sym(c->sym);

    for (size_t i = 0; i < sym->lookups_len; i++)
    {
        lookup_t lookup = sym->lookups[i];
        hash_t key = hash_lookup(key_sym, lookup, c);
        debug("!bINSERT!d %s [key=%.16llX%.16llX]", show_cons(c), key[0],
            key[1]);
        store_insert_constraint(key, false, c);
    }
}

/*
 * Un-insert an entry from the store.
 */
static void store_uninsert_entry(word_t arg)
{
    store_usage--;
    entry_t entry = (entry_t)arg;
    size_t idx = store_index(entry->key);
    entry_t entry_0 = store[idx];
    if (entry == entry_0)
    {
        store[idx] = entry_0->next;
        return;
    }
    entry_t prev = entry_0;
    entry_0 = entry_0->next;
    while (true)
    {
        if (entry == entry_0)
        {
            prev->next = entry_0->next;
            return;
        }
        prev = entry_0;
        entry_0 = entry_0->next;
    }
}

/*
 * Insert a constraint into an entry; or create a new entry.
 */
static void store_insert_constraint(hash_t key, bool primary, cons_t c)
{
    debug("!bSTORE!d %s [key=%.16llX%.16llX] <%s, ...>", show_cons(c), key[0],
        key[1], show_cons(c));

    conslist_t cons_entry = (conslist_t)gc_malloc(sizeof(struct conslist_s));
    cons_entry->cons = c;

    size_t idx = store_index(key);
    entry_t entry_0 = store[idx];
    entry_t entry = entry_0;
    while (entry != NULL)
    {
        if (hash_iseq(key, entry->key))
        {
            cons_entry->next = entry->cons;
            if (!primary)
                trail(&entry->cons);
            entry->cons = cons_entry;
            return;
        }
        entry = entry->next;
    }

    cons_entry->next = NULL;
    entry = (entry_t)gc_malloc(sizeof(struct entry_s));
    entry->key = key;
    entry->next = entry_0;
    entry->cons = cons_entry;
    entry->tail = cons_entry;
    store[idx] = entry;
    if (!primary)
        trail_func(store_uninsert_entry, (word_t)entry);

    store_usage++;
    if (STORE_GROWTH_FACTOR * store_usage > store_len)
        store_grow();
}

/*
 * Delete a constraint.
 */
extern void solver_store_delete(cons_t c)
{
    static size_t count = 0;
    count++;

    // Amortized deletion:
    if (count % STORE_DELETE_FREQ == 0)
    {
        sym_t sym = c->sym;
        hash_t key_sym = hash_sym(c->sym);

        // Note: We must not delete the default all-T key, since this is
        //       needed by make_cons.
        for (size_t i = 0; i < sym->lookups_len; i++)
        {
            lookup_t lookup = sym->lookups[i];
            hash_t key = hash_lookup(key_sym, lookup, c);
            store_delete_constraint(key);
        }
    }
}

/*
 * Delete a constraint from an entry.  Note: deletes all purged constraints.
 */
static void store_delete_constraint(hash_t key)
{
    // NOTE: Solvers may have references to the conslist.  We must ensure that
    //       any updates we do are "safe".
    // NOTE: It is unsafe to delete the first conslist element, or the entry
    //       itself.

    size_t idx = store_index(key);
    entry_t entry_0 = store[idx];
    entry_t entry = entry_0;
    while (entry != NULL)
    {
        if (hash_iseq(key, entry->key))
            break;
        entry = entry->next;
    }
    if (entry == NULL)
        return;
    conslist_t cs = entry->cons;
    conslist_t prev = cs;

    cs = cs->next;

    while (true)
    {
        while (cs != NULL && ispurged(cs->cons))
            cs = cs->next;
        if (cs == NULL)
        {
            if (entry->tail != prev)
                trail(&entry->tail);
            entry->tail = prev;
            if (prev->next != NULL)
                trail(&prev->next);
            prev->next = NULL;
            return;
        }
        else if (prev->next != cs)
        {
            trail(&prev->next);
            prev->next = cs;
        }
        prev = cs;
        cs = cs->next;
    }
}

/*
 * Move a constraint.
 */
extern void solver_store_move(cons_t c, hash_t xkey_old, hash_t xkey_new)
{
    sym_t sym = c->sym;
    hash_t key_sym = hash_sym(c->sym);

    debug("!bMOVE!d %s", show_cons(c));
    hash_t key_old = hash_cons(c);
    hash_t key_new = hash_update_cons(key_sym, c, xkey_old, xkey_new);
    debug("MOVE [%.16llX%.16llX -> %.16llX%.16llX]", key_old[0],
        key_old[1], key_new[0], key_new[1]);
    store_move_entry(key_old, key_new);

    for (size_t i = 0; i < sym->lookups_len; i++)
    {
        lookup_t lookup = sym->lookups[i];
        hash_t key_old = hash_lookup(key_sym, lookup, c);
        hash_t key_new = hash_update_lookup(key_sym, lookup, c, xkey_old,
            xkey_new);
        if (hash_iseq(key_old, key_new))
            continue;
        debug("MOVE [%.16llX%.16llX -> %.16llX%.16llX]", key_old[0],
            key_old[1], key_new[0], key_new[1]);
        store_move_entry(key_old, key_new);
    }
}

/*
 * Move an entry.
 */
static void store_move_entry(hash_t key_old, hash_t key_new)
{
    // Delete from old position:
    size_t idx = store_index(key_old);
    entry_t entry = store[idx];
    entry_t prev = NULL;
    while (entry != NULL)
    {
        if (hash_iseq(key_old, entry->key))
        {
            if (prev == NULL)
                store[idx] = entry->next;
            else
                prev->next = entry->next;
            break;
        }
        prev = entry;
        entry = entry->next;
    }
    if (entry == NULL)
        return;

    entry->next = NULL;

    // Insert into new position:
    entry_t entry_old = entry;
    idx = store_index(key_new);
    entry = store[idx];
    while (entry != NULL)
    {
        if (hash_iseq(key_new, entry->key))
        {
            // Append to existing entry:
            conslist_t tail = entry->tail;
            tail->next = entry_old->cons;
            entry->tail = entry_old->tail;
            goto store_move_entry_exit;
        }
        entry = entry->next;
    }

    // Copy without appending:
    entry = (entry_t)gc_malloc(sizeof(struct entry_s));
    entry->key  = key_new;
    entry->next = store[idx];
    entry->cons = entry_old->cons;
    entry->tail = entry_old->tail;
    store[idx] = entry;

store_move_entry_exit: {}
    
    moveinfo_t info = (moveinfo_t)gc_malloc(sizeof(struct moveinfo_s));
    info->old = entry_old;
    info->new = entry;
    trail_func((trailfunc_t)store_unmove_entry, (word_t)info);
}

/*
 * Unmove an entry.
 */
static void store_unmove_entry(word_t arg)
{
    moveinfo_t info = (moveinfo_t)arg;
    
    // Move the old entry back to it's original position:
    entry_t entry_old = info->old;
    hash_t key_old = entry_old->key;
    size_t idx_old = store_index(key_old);
    entry_old->next = store[idx_old];
    store[idx_old] = entry_old;

    // Restore the conslist for the new entry:
    entry_t entry_new = info->new;
    hash_t key_new = entry_new->key;
    size_t idx_new = store_index(key_new);
    if (entry_new->cons == entry_old->cons)
    {
        entry_t entry = store[idx_new];
        if (entry == entry_new)
            store[idx_new] = entry->next;
        else
        {
            entry_t prev = entry;
            entry = entry->next;
            while (entry != NULL)
            {
                if (entry == entry_new)
                {
                    prev->next = entry->next;
                    return;
                }
                prev = entry;
                entry = entry->next;
            }
            panic("delete failed");
        }
        return;
    }

    debug("!bUNMOVE!d %p", entry_old->cons);
    
    conslist_t cs = entry_new->cons, prev = cs;
    cs = cs->next;
    while (cs != entry_old->cons)
    {
        check(cs != NULL);
        prev = cs;
        cs = cs->next;
    }
    prev->next = NULL;
    entry_new->tail = prev;
}

/*
 * In-place store growth.
 */
static void store_grow(void)
{
    store_shift++;
    if (store_shift >= STORE_MAX_SHIFT)
        panic("constraint store too big (%zu constraints)", stat_constraints);
    size_t old_store_len = store_len;
    store_len *= 2;

    /*
     * Move entries.  On average only 1/2 of entries need to be moved.
     */
    size_t count = 0;
    for (size_t i = 0; i < old_store_len; i++)
    {
        entry_t entry = store[i], stay = NULL, moved = NULL;
        while (entry != NULL)
        {
            count++;
            entry_t next = entry->next;
            if (store_index(entry->key) != i)
            {
                debug("!bGROW!d MOVE [key=%.16llX%.16llX]", entry->key[0],
                    entry->key[1]);
                entry->next = moved;
                moved = entry;
            }
            else
            {
                entry->next = stay;
                stay = entry;
            }
            entry = next;
        }
        store[i] = stay;
        store[old_store_len + i] = moved;
    }
    store_usage = count;

#ifndef NODEBUG
    count = 0;
    for (size_t i = 0; i < store_len; i++)
    {
        entry_t entry = store[i];
        while (entry != NULL)
        {
            count++;
            if (store_index(entry->key) != i)
            {
                panic("bad hash %.16llX%.16llX at wrong index "
                    "(expected %zu, got %zu)", entry->key[0], entry->key[1],
                    i, store_index(entry->key));
                abort();
            }
            entry = entry->next;
        }
    }
    if (count != store_usage)
        panic("bad count (expected %zu, got %zu)", store_usage, count);
#endif
}

