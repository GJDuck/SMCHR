/*
 * names.c
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

#include <stdio.h>
#include <stdlib.h>

#include "solver.h"
#include "hash.h"
#include "misc.h"
#include "log.h"

#define TABLE_INIT_SHIFT                    8
#define TABLE_INIT_LEN                      (1 << TABLE_INIT_SHIFT);

static hash_t *table;                       // Hash table.
static size_t table_len;                    // Hash table length.
static size_t table_shift;                  // Hash table shift.
static size_t table_usage;                  // Hash table usage.

size_t counter;                             // Default counter.

/*
 * Prototypes.
 */
static size_t table_index(hash_t key);
static bool table_lookup(hash_t key);
static void table_do_insert(hash_t key, size_t idx);
static void table_insert(hash_t key);
static void table_grow(void);

/*
 * Initialize/reset this module.
 */
extern void names_init(void)
{
    table = buffer_alloc(0x40000000);       // 1GB
    table_len = TABLE_INIT_LEN;             // 1 page.
    table_shift = TABLE_INIT_SHIFT;
    table_usage = 0;
    counter = 0;
}
extern void names_reset(void)
{
    buffer_free(table, table_len * sizeof(hash_t));
    table_len = TABLE_INIT_LEN;
    table_shift = TABLE_INIT_SHIFT;
    table_usage = 0;
    counter = 0;
}

/*
 * Hash table operations.
 */
static size_t table_index(hash_t key)
{
    size_t idx = (size_t)key[0];
    size_t mask = (1 << table_shift) - 1;
    return idx & mask;
}
static bool table_lookup(hash_t key)
{
    size_t idx = table_index(key);
    hash_t zero = HASH(0, 0);
    while (true)
    {
        if (hash_iseq(table[idx], key))
            return true;
        if (hash_iseq(table[idx], zero))
            return false;
        idx++;
    }
}
static void table_do_insert(hash_t key, size_t idx)
{
    hash_t zero = HASH(0, 0);
    while (true)
    {
        if (hash_iseq(table[idx], zero))
        {
            table[idx] = key;
            return;
        }
        idx++;
    }
}
static void table_insert(hash_t key)
{
    table_usage++;
    if (table_usage * 2 > table_len)
        table_grow();
    size_t idx = table_index(key);
    table_do_insert(key, idx);
}

/*
 * Grow the hash table.
 */
static void table_grow(void)
{
    table_shift++;
    size_t old_table_len = table_len;
    table_len *= 2;

    // More entries.
    hash_t zero = HASH(0, 0);
    for (size_t i = 0; i < old_table_len; i++)
    {
        if (hash_iseq(table[i], zero))
            continue;
        hash_t key = table[i];
        size_t idx = table_index(key);
        if (idx == i)
            continue;
        table[i] = zero;
        table_do_insert(key, idx);
    }
}

/*
 * Generate a unique name.
 */
extern char *unique_name(const char *base, size_t *counter_ptr)
{
    // size = '_' base counter safety
    size_t size = 1 + strlen(base) + 20 + 8;
    char buf[size+1];

    hash_t key;
    do
    {
        size_t id;
        if (counter_ptr == NULL)
            id = counter++;
        else
        {
            id = *counter_ptr;
            *counter_ptr = id + 1;
        }
        int r = snprintf(buf, size, "_%s%zu", base, id);
        if (r <= 0 || r >= size)
            panic("failed to create unique name; snprintf failed");
        key = hash_cstring(buf);
    }
    while (table_lookup(key));

    table_insert(key);
    return gc_strdup(buf);
}

/*
 * Register a name that may clash with the unique name generator.
 */
extern void register_name(const char *name)
{
    if (name[0] != '_')
        return;
    hash_t key = hash_cstring(name);
    table_insert(key);
}

