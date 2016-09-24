/*
 * rhash.c	A generic hash-set or -map
 *
 * Copyright (c) 2007-2014  Douglas P Lau
 *
 * Public functions:
 *
 *	cl_rhash_create_set	Create a hash set
 *	cl_rhash_create_map	Create a hash map
 *	cl_rhash_destroy	Destroy a hash set or map
 *	cl_rhash_count		Count the entries in a hash set or map
 *	cl_rhash_contains	Test if a hash contains a key
 *	cl_rhash_peek		Get an arbitrary key
 *	cl_rhash_get		Get a value from a hash map
 *	cl_rhash_add		Add an entry to a hash set
 *	cl_rhash_put		Put a mapping into a hash map
 *	cl_rhash_remove		Remove a key from a hash set or map
 *	cl_rhash_clear		Clear all entries from a hash set or map
 *	cl_rhash_iterator_create Create a hash key iterator
 *	cl_rhash_iterator_destroy Destroy a hash key iterator
 *	cl_rhash_iterator_next	Get the next key from an iterator
 *	cl_rhash_iterator_value	Get the value mapped to most recent key
 */
/** \file
 *
 * The rhash module provides both hash sets and hash maps.
 * A hash map uses twice as much memory as a hash set, but allows an arbitrary
 * value to be mapped to each hash key.
 * A hash table has enough space for one pointer for each key (plus value for
 * maps).
 * NOTE: some functions can be used with either hash sets or maps, but some
 * must only be used with either sets or maps.
 *
 *     ITERATIVE REHASHING
 *
 * When a hash set or map must be resized, it is done iteratively instead of
 * all at once.
 * This is to reduce worst-case performance for add/remove operations, which
 * improves real-time latency.
 *
 * There are two tables associated with each hash: h_lo and h_hi.
 * The high table is double the size of the low table.
 * Both tables are checked for every "get" operation.
 *
 * Any new entries are always inserted to the high table.
 * The new entry is also removed from the low table (to prevent shadowing).
 * An add threshold is calculated as 3/4 the size of the high table.
 * If the number of entries in the high table plus twice the number of entries
 * in the low table is greater or equal to the add threshold, one existing entry
 * is moved from low to high.
 * Then, if the low table is empty and the high table is at the add threshold,
 * the high table becomes the low table and a new high table is allocated at
 * double the size.
 *
 *   LOW    HIGH  EXPAND  SHRINK
 *    --      64      48      --
 *    64     128      96      32
 *   128     256     192      64
 *   256     512     384     128
 *   512    1024     768     256
 *  1024    2048    1536     512
 *
 * When removing an entry, both tables are checked for the entry.
 * After removing an entry, if the high table is not empty, one existing entry
 * is moved to the low table.
 * A remove threshold is calculated as 1/4 the size of the high table.
 * Then, if the high table is empty and the low table is at the remove
 * threshold, the low table becomes the high table and a new low table is
 * allocated at half the size.
 *
 *     ROBIN HOOD HASHING
 *
 * In case of hash collisions, open addressing is used instead of chaining
 * (for memory efficiency and good cache utilization).
 * Linear probing with Robin Hood hashing is used to reduce worst case probe
 * lengths.
 *
 *        0     1     2     3     4     5     6     7
 *      _____ _____ _____ _____ _____ _____ _____ _____
 *     |_____|_____|_____|_____|_____|_____|_____|_____|
 *      _____ _____ _____ _____ _____ _____ _____ _____
 *     |_____|_____|_____|_____|_A:4_|_____|_____|_____|  Add A:4
 *      _____ _____ _____ _____ _____ _____ _____ _____
 *     |_____|_____|_____|_____|_A:4_|_____|_B:6_|_____|  Add B:6
 *      _____ _____ _____ _____ _____ _____ _____ _____
 *     |_____|_____|_____|_____|_C:4_|_A:4_|_B:6_|_____|  Add C:4
 *      _____ _____ _____ _____ _____ _____ _____ _____
 *     |_____|_____|_____|_____|_C:4_|_A:4_|_D:5_|_B:6_|  Add D:5
 *      _____ _____ _____ _____ _____ _____ _____ _____
 *     |_E:6_|_____|_____|_____|_C:4_|_A:4_|_D:5_|_B:6_|  Add E:6
 *      _____ _____ _____ _____ _____ _____ _____ _____
 *     |_____|_____|_____|_____|_C:4_|_A:4_|_B:6_|_E:6_|  Remove D:5
 *      _____ _____ _____ _____ _____ _____ _____ _____
 *     |_____|_____|_____|_____|_C:4_|_____|_B:6_|_E:6_|  Remove A:4
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "clump.h"

//#define VDEBUG

/** Get an integer key from an entry (for debugging).
 */
#ifdef VDEBUG
static int cl_debug_int(void **ent) {
	return (ent) && (*ent) ? *(int *)(*ent) : 0;
}
#endif

/** Minimum hash table order */
static const uint16_t CL_HASH_MIN_ORDER = 6;

/** Maximum hash table order */
static const uint16_t CL_HASH_MAX_ORDER = 31;

/** Hash table structure.
 */
struct cl_rhash_table {
	void			*table;		/**< actual hash table */
	uint32_t		n_entries;	/**< number of entries */
	uint32_t		n_peek;		/**< current peek slot */
	uint16_t		n_bytes;	/**< number of bytes per entry*/
	uint16_t		order;		/**< table size order */
	uint16_t		key_bytes;	/**< key size in bytes */
};

/** Get size of a hash table.
 *
 * @param tbl		pointer to hash table.
 */
static uint32_t cl_rhash_table_size(const struct cl_rhash_table *tbl) {
	return 1 << tbl->order;
}

/** Get the specified hash table entry pointer.
 *
 * @param tbl		Pointer to hash table.
 * @param slot		Slot in the hash table.
 *
 * @return Pointer to hash table entry.
 */
static void **cl_rhash_table_ptr(struct cl_rhash_table *tbl, uint32_t slot) {
	uint8_t *base = tbl->table;
	uint32_t offset = slot * tbl->n_bytes;
	assert(slot < cl_rhash_table_size(tbl));
	void *ptr = base + offset;
	return ptr;
}

/** String hash function.
 *
 * Simple hash function for a C string.
 */
static uint32_t cl_rhash_table_hash_str(const void *key) {
	const uint8_t *str = key;
	uint32_t hash = 5381;
	int c;
	while ((c = *str)) {
		hash = ((hash << 5) + hash) + c;
		str++;
	}
	return hash;
}

/** Fixed-width hash function.
 *
 * Simple hash function for a fixed-width key.
 */
static uint32_t cl_rhash_table_hash_fixed(const void *key, uint16_t key_bytes) {
	const uint8_t *ptr = key;
	uint32_t hash = 5381;
	for (uint16_t i = 0; i < key_bytes; i++) {
		int c = *ptr;
		hash = ((hash << 5) + hash) + c;
		ptr++;
	}
	return hash;
}

/** Calculate a hash code for one entry.
 *
 * @param tbl		Pointer to hash table.
 * @param ent		Pointer to hash entry.
 * @return Hash code for given key.
 */
static uint32_t cl_rhash_table_hash(const struct cl_rhash_table *tbl,
	void **ent)
{
	return (tbl->key_bytes)
	      ? cl_rhash_table_hash_fixed(*ent, tbl->key_bytes)
	      : cl_rhash_table_hash_str(*ent);
}

/** Get masked hash table slot.
 *
 * @param tbl		Pointer to hash table.
 * @param hcode		Hash code.
 * @param pr		Probe length.
 *
 * @return Masked hash table slot.
 */
static uint32_t cl_rhash_table_slot(const struct cl_rhash_table *tbl,
	uint32_t hcode, uint32_t pr)
{
	uint32_t mask = cl_rhash_table_size(tbl) - 1;
	return (hcode + pr) & mask;
}

/** Calculate the cost of probing a hash table entry.
 *
 * @param tbl		Pointer to hash table.
 * @param slot		Slot offset.
 * @param o_slot	Optimal slot (cost 0).
 *
 * @return Cost of probing entry.
 */
static uint32_t cl_rhash_table_cost_slot(const struct cl_rhash_table *tbl,
	uint32_t slot, uint32_t o_slot)
{
	return (slot >= o_slot) ?
	       (slot - o_slot) :
	       (cl_rhash_table_size(tbl) + slot - o_slot);
}

/** Calculate the cost to probe for an entry.
 *
 * @param tbl		Pointer to hash table.
 * @param slot		Slot in hash table.
 *
 * @return Cost to probe for the entry at given slot.
 */
static uint32_t cl_rhash_table_cost(struct cl_rhash_table *tbl, uint32_t slot) {
	void **ent = cl_rhash_table_ptr(tbl, slot);
	uint32_t hcode = cl_rhash_table_hash(tbl, ent);
	uint32_t o_slot = cl_rhash_table_slot(tbl, hcode, 0);
	return cl_rhash_table_cost_slot(tbl, slot, o_slot);
}

/** Debug one entry.
 */
static void cl_rhash_table_debug_entry(struct cl_rhash_table *tbl,
	const char *prefix, void **ent)
{
#ifdef VDEBUG
	printf("%s\t", prefix);
	printf("ptr: %p\t", *ent);
	printf("key: %d\n", cl_debug_int(ent));
#endif
}

/** Debug a hash table.
 */
static void cl_rhash_table_debug(struct cl_rhash_table *tbl, void **ent,
	char ps)
{
#ifdef VDEBUG
	int key = cl_debug_int(ent);
	for (uint32_t i = 0; i < cl_rhash_table_size(tbl); i++) {
		void **e = cl_rhash_table_ptr(tbl, i);
		int k = cl_debug_int(e);
		if (k) {
			printf("%3d", k);
			if (k == key)
				printf("%c", ps);
			else
				printf(" ");
			printf("%d ", cl_rhash_table_cost(tbl, i));
		} else {
			printf("  . ");
			printf("  ");
		}
	}
	printf("(%d%c)\n", cl_rhash_table_size(tbl), ps);
#endif
}

/** Zero out hash table.
 *
 * @param tbl		pointer to hash table.
 */
static void cl_rhash_table_zero(struct cl_rhash_table *tbl) {
	uint32_t n_bytes = cl_rhash_table_size(tbl) * tbl->n_bytes;
	memset(tbl->table, 0, n_bytes);
}

/** Allocate hash table.
 *
 * allocate memory for hash table.
 *
 * @param tbl		pointer to hash table.
 */
static void cl_rhash_table_alloc(struct cl_rhash_table *tbl) {
	tbl->table = calloc(cl_rhash_table_size(tbl), tbl->n_bytes);
	assert(tbl->table);
}

/** Initialize a hash table.
 *
 * @param tbl		Pointer to hash table.
 * @param key_bytes	Number of bytes in each key (0 for strings).
 * @param n_bytes	Number of bytes per hash table entry.
 */
static void cl_rhash_table_init(struct cl_rhash_table *tbl, uint16_t key_bytes,
	uint16_t n_bytes)
{
	tbl->order = CL_HASH_MIN_ORDER;
	tbl->n_entries = 0;
	tbl->n_peek = cl_rhash_table_size(tbl);
	tbl->key_bytes = key_bytes;
	tbl->n_bytes = n_bytes;
	cl_rhash_table_alloc(tbl);
	cl_rhash_table_debug(tbl, NULL, ' ');
}

/** Destroy a hash table.
 *
 * @param tbl		Pointer to hash table.
 */
static void cl_rhash_table_destroy(struct cl_rhash_table *tbl) {
	free(tbl->table);
#ifndef NDEBUG
	tbl->table = NULL;
	tbl->order = 0;
	tbl->n_entries = 0;
	tbl->n_peek = 0;
	tbl->n_bytes = 0;
	tbl->key_bytes = 0;
#endif
}

/** Clone one table to another.
 *
 * @param tbl		Pointer to hash table.
 */
static void cl_rhash_table_clone(struct cl_rhash_table *tbl,
	struct cl_rhash_table *src)
{
	assert(tbl->n_entries == 0);
	free(tbl->table);
	tbl->table = src->table;
	tbl->n_entries = src->n_entries;
	tbl->n_peek = src->n_peek;
	tbl->n_bytes = src->n_bytes;
	tbl->order = src->order;
	tbl->key_bytes = src->key_bytes;
}

/** Update one entry in a hash table.
 *
 * @param tbl		Pointer to hash table.
 * @param slot		Slot in hash table.
 */
static void cl_rhash_table_entry_update(struct cl_rhash_table *tbl,
	uint32_t slot)
{
	if (slot < tbl->n_peek)
		tbl->n_peek = slot;
}

/** Clear a hash table.
 *
 * @param tbl		Pointer to hash table.
 */
static void cl_rhash_table_clear(struct cl_rhash_table *tbl) {
	tbl->n_entries = 0;
	tbl->n_peek = cl_rhash_table_size(tbl);
	if (tbl->order != CL_HASH_MIN_ORDER) {
		free(tbl->table);
		tbl->order = CL_HASH_MIN_ORDER;
		cl_rhash_table_alloc(tbl);
	} else
		cl_rhash_table_zero(tbl);
}

/** Get the hash size minimum limit.
 *
 * @param tbl		Pointer to hash table.
 *
 * @return Current hash size minimum limit.
 */
static uint32_t cl_rhash_table_slimit(const struct cl_rhash_table *tbl) {
	if (tbl->order > CL_HASH_MIN_ORDER)
		return cl_rhash_table_size(tbl) / 4;
	else
		return 0;
}

/** Get the hash size maximum limit.
 *
 * A hash table should never be more than 3/4 full, so the limit should be
 * checked before adding a new entry.
 *
 * @param tbl		Pointer to hash table.
 *
 * @return Current hash size maximum limit.
 */
static uint32_t cl_rhash_table_limit(const struct cl_rhash_table *tbl) {
	uint32_t n_size = cl_rhash_table_size(tbl);
	return n_size - (n_size / 4);
}

/** Check if a hash table entry exists.
 *
 * @param tbl		Pointer to hash table.
 * @param ent		Pointer to entry.
 */
static bool cl_rhash_table_entry_exists(const struct cl_rhash_table *tbl,
	void **ent)
{
	return *ent;
}

/** Copy a hash table entry.
 */
static void cl_rhash_table_entry_copy(const struct cl_rhash_table *tbl,
	void **dst, void **src)
{
	memcpy(dst, src, tbl->n_bytes);
}

/** Check if two hash table keys are equal.
 */
static bool cl_rhash_table_key_equals(const struct cl_rhash_table *tbl,
	void **key1, void **key2)
{
	return (tbl->key_bytes)
	      ? (memcmp(*key1, *key2, tbl->key_bytes) == 0)
	      : (strcmp(*key1, *key2) == 0);
}

/** Peek the next entry in a hash table.
 *
 * @param tbl		Pointer to hash table.
 * @return Pointer to hash table entry, or NULL if empty.
 */
static void **cl_rhash_table_peek_entry(struct cl_rhash_table *tbl) {
	uint32_t n_size = cl_rhash_table_size(tbl);
	for (uint32_t pr = tbl->n_peek; pr < n_size; pr++) {
		void **e = cl_rhash_table_ptr(tbl, pr);
		if (cl_rhash_table_entry_exists(tbl, e)) {
			tbl->n_peek = pr;
			return e;
		}
	}
	return NULL;
}

/** Get an arbitrary entry from a hash table.
 *
 * @param tbl		Pointer to hash table.
 * @return Pointer to hash table entry, or NULL if empty.
 */
static void **cl_rhash_table_peek(struct cl_rhash_table *tbl) {
	if (tbl->n_entries > 0)
		return cl_rhash_table_peek_entry(tbl);
	else
		return NULL;
}

/** Lookup an entry in a hash table.
 *
 * Lookup an entry in a hash table for the given key.  Entries in the hash
 * table are checked using linear probing.  Probing stops when there is no entry
 * at a slot, or when the cost of the entry is higher than the probe length.
 * The cost of an entry is the probe distance from the initial slot.
 *
 * @param tbl		Pointer to hash table.
 * @param key		Pointer to key to lookup.
 *
 * @return Pointer to hash table entry, or NULL if not found.
 */
static void **cl_rhash_table_entry(struct cl_rhash_table *tbl, void **key) {
	uint32_t n_size = cl_rhash_table_size(tbl);
	uint32_t hcode = cl_rhash_table_hash(tbl, key);
	for (uint32_t pr = 0; pr < n_size; pr++) {
		uint32_t slot = cl_rhash_table_slot(tbl, hcode, pr);
		void **e = cl_rhash_table_ptr(tbl, slot);
		if (!cl_rhash_table_entry_exists(tbl, e))
			break;
		if (cl_rhash_table_key_equals(tbl, key, e))
			return e;
		if (pr > cl_rhash_table_cost(tbl, slot))
			break;
	}
	return NULL;
}

/** Test if a hash table contains a key.
 *
 * @param tbl		Pointer to hash table.
 * @param key		Key to test for.
 *
 * @return True if hash table contains the key, otherwise false.
 */
static bool cl_rhash_table_contains(struct cl_rhash_table *tbl, void *key) {
	return cl_rhash_table_entry(tbl, &key) != NULL;
}

/** Count the number of slots to the next empty slot in a hash table.
 *
 * @param tbl		Pointer to hash table.
 * @param hcode		Hash code.
 * @param ipr		Probe length.
 */
static uint32_t cl_rhash_table_find_empty(struct cl_rhash_table *tbl,
	uint32_t hcode, uint32_t ipr)
{
	uint32_t n_size = cl_rhash_table_size(tbl);
	for (uint32_t pr = ipr; pr < n_size; pr++) {
		uint32_t slot = cl_rhash_table_slot(tbl, hcode, pr);
		void **ent = cl_rhash_table_ptr(tbl, slot);
		if (!cl_rhash_table_entry_exists(tbl, ent))
			return pr;
	}
	return 0;
}

/** Shift entries forward in a hash table.  Only shift from initial probe length
 * to the first empty entry.
 *
 * @param tbl		Pointer to hash table.
 * @param hcode		Hash code.
 * @param ipr		Probe length.
 */
static void cl_rhash_table_shift_forward(struct cl_rhash_table *tbl,
	uint32_t hcode, uint32_t ipr)
{
	void **p_ent = NULL;
	uint32_t shift = cl_rhash_table_find_empty(tbl, hcode, ipr);
	for (uint32_t pr = shift; pr >= ipr; pr--) {
		uint32_t slot = cl_rhash_table_slot(tbl, hcode, pr);
		void **ent = cl_rhash_table_ptr(tbl, slot);
		if (p_ent)
			cl_rhash_table_entry_copy(tbl, p_ent, ent);
		cl_rhash_table_entry_update(tbl, slot);
		p_ent = ent;
	}
}

/** Insert an entry into a hash table.
 *
 * If there is a hash collision, do a linear probe.  Compare the cost of each
 * entry with the probe length, and insert the new entry when the probe length
 * is more.  Subsequent entries are shifted one slot forward in the table.
 *
 * @param tbl		Pointer to hash table.
 * @param ent		Pointer to hash table entry.  The entry must be
 *                      tbl->n_bytes in size.
 * @return Pointer to previous key equal to entry key (or NULL).
 */
static void *cl_rhash_table_insert(struct cl_rhash_table *tbl, void **ent) {
	cl_rhash_table_debug_entry(tbl, "insert", ent);
	uint32_t n_size = cl_rhash_table_size(tbl);
	uint32_t hcode = cl_rhash_table_hash(tbl, ent);
	assert(tbl->n_entries < n_size);
	for (uint32_t pr = 0; pr < n_size; pr++) {
		uint32_t slot = cl_rhash_table_slot(tbl, hcode, pr);
		void **e = cl_rhash_table_ptr(tbl, slot);
		if (!cl_rhash_table_entry_exists(tbl, e)) {
			cl_rhash_table_entry_copy(tbl, e, ent);
			tbl->n_entries++;
			cl_rhash_table_entry_update(tbl, slot);
			cl_rhash_table_debug(tbl, ent, '+');
			return NULL;
		}
		if (cl_rhash_table_key_equals(tbl, ent, e)) {
			void *key = *e;
			cl_rhash_table_entry_copy(tbl, e, ent);
			cl_rhash_table_entry_update(tbl, slot);
			cl_rhash_table_debug(tbl, ent, '=');
			return key;
		}
		if (pr > cl_rhash_table_cost(tbl, slot)) {
			cl_rhash_table_shift_forward(tbl, hcode, pr);
			cl_rhash_table_entry_copy(tbl, e, ent);
			tbl->n_entries++;
			cl_rhash_table_entry_update(tbl, slot);
			cl_rhash_table_debug(tbl, ent, '+');
			return NULL;
		}
	}
	assert(false);
	cl_rhash_table_debug(tbl, ent, '!');
	return NULL;
}

/** Shift entries backward one slot.  Only shift until an unused or zero cost
 * entry is found.
 *
 * @param tbl		Pointer to hash table.
 * @param ent		Pointer to hash table entry.
 * @param ipr		Probe length.
 */
static void cl_rhash_table_shift_backward(struct cl_rhash_table *tbl,
	void **ent, uint32_t ipr)
{
	uint32_t n_size = cl_rhash_table_size(tbl);
	uint32_t hcode = cl_rhash_table_hash(tbl, ent);
	for (uint32_t pr = ipr + 1; pr < n_size; pr++) {
		uint32_t slot = cl_rhash_table_slot(tbl, hcode, pr);
		void **e = cl_rhash_table_ptr(tbl, slot);
		if (!cl_rhash_table_entry_exists(tbl, e))
			break;
		if (cl_rhash_table_cost(tbl, slot) == 0)
			break;
		cl_rhash_table_entry_copy(tbl, ent, e);
		ent = e;
	}
	memset(ent, 0, tbl->n_bytes);
}

/** Remove an entry from a hash table.
 *
 * @param tbl		Pointer to hash table.
 * @param ent		Pointer to hash table entry.  The entry must be
 *                      tbl->n_bytes in size.
 * @return Previous key equal to entry key, or NULL.
 */
static void *cl_rhash_table_remove(struct cl_rhash_table *tbl, void **ent) {
	cl_rhash_table_debug_entry(tbl, "remove", ent);
	if (tbl->n_entries == 0)
		return NULL;
	uint32_t n_size = cl_rhash_table_size(tbl);
	uint32_t hcode = cl_rhash_table_hash(tbl, ent);
	for (uint32_t pr = 0; pr < n_size; pr++) {
		uint32_t slot = cl_rhash_table_slot(tbl, hcode, pr);
		void **e = cl_rhash_table_ptr(tbl, slot);
		if (!cl_rhash_table_entry_exists(tbl, e)) {
			cl_rhash_table_debug(tbl, ent, '!');
			return NULL;
		}
		if (cl_rhash_table_key_equals(tbl, ent, e)) {
			void *key = *e;
			tbl->n_entries--;
			cl_rhash_table_shift_backward(tbl, e, pr);
			cl_rhash_table_entry_update(tbl, slot);
			cl_rhash_table_debug(tbl, ent, '-');
			return key;
		}
	}
	cl_rhash_table_debug(tbl, ent, '!');
	return NULL;
}

/** Table enum (for iterators) */
enum cl_rhash_table_num {
	CL_RHASH_TABLE_LO, CL_RHASH_TABLE_HI, CL_RHASH_TABLE_NONE
};

/** Hash iterator structure.
 */
struct cl_rhash_iterator {
	struct cl_rhash		*hash;		/**< hash struct */
	enum cl_rhash_table_num	n_table;	/**< table number */
	uint32_t		n_slot;		/**< current slot */
#ifndef NDEBUG
	uint32_t		n_edit;		/**< edit version number */
#endif
};

/** Hash set/map structure.
 */
struct cl_rhash {
	struct cl_rhash_table	h_lo;		/**< low table */
	struct cl_rhash_table	h_hi;		/**< high table */
	struct cl_pool		*pool;		/**< hash iterator pool */
	bool			is_map;		/**< flag for mapping */
#ifndef NDEBUG
	uint32_t		n_edit;		/**< edit version number */
#endif
};

/** Create a hash set or map.
 *
 * Create a hash set or map, preparing it to be used.
 *
 * @param key_bytes	Number of bytes in each key (0 for strings).
 * @param is_map	True for map, false for set.
 * @return Pointer to hash set or map.
 */
static struct cl_rhash *cl_rhash_create(uint16_t key_bytes, bool is_map) {
	struct cl_rhash *hash = malloc(sizeof(struct cl_rhash));
	assert(hash);
	uint16_t n_bytes = is_map ?
	                   sizeof(void **) * 2 :
	                   sizeof(void **);
	cl_rhash_table_init(&hash->h_lo, key_bytes, n_bytes);
	cl_rhash_table_init(&hash->h_hi, key_bytes, n_bytes);
	hash->pool = cl_pool_create(sizeof(struct cl_rhash_iterator));
	hash->is_map = is_map;
#ifndef NDEBUG
	hash->n_edit = 0;
#endif
	return hash;
}

/** Create a hash set.
 *
 * Create a hash set, preparing it to be used.
 *
 * @param key_bytes	Number of bytes in each key (0 for strings).
 * @return Pointer to hash set.
 */
struct cl_rhash *cl_rhash_create_set(uint16_t key_bytes) {
	return cl_rhash_create(key_bytes, false);
}

/** Create a hash map.
 *
 * Create a hash map, preparing it to be used.
 *
 * @param key_bytes	Number of bytes in each key (0 for strings).
 * @return Pointer to hash map.
 */
struct cl_rhash *cl_rhash_create_map(uint16_t key_bytes) {
	return cl_rhash_create(key_bytes, true);
}

/** Check if a hash is a map.
 *
 * @param hash Pointer to hash set or map.
 * @return true for hash map, false for hash set.
 */
static bool cl_rhash_is_map(const struct cl_rhash *hash) {
	assert(hash->h_lo.n_bytes == hash->h_hi.n_bytes);
	return hash->is_map;
}

/** Destroy a hash set or map.
 *
 * Destroy a hash set or map, freeing all its resources.
 *
 * @param hash Pointer to hash set or map.
 */
void cl_rhash_destroy(struct cl_rhash *hash) {
	assert(hash);
	cl_rhash_table_destroy(&hash->h_lo);
	cl_rhash_table_destroy(&hash->h_hi);
	cl_pool_destroy(hash->pool);
#ifndef NDEBUG
	hash->pool = NULL;
	hash->n_edit = 0;
#endif
	free(hash);
}

/** Update hash edit version.
 */
static void cl_rhash_edit(struct cl_rhash *hash) {
#ifndef NDEBUG
	hash->n_edit++;
#endif
}

/** Get the count of entries.
 *
 * @param hash Pointer to hash set or map.
 * @return Count of entries currently in the hash set or map.
 */
uint32_t cl_rhash_count(const struct cl_rhash *hash) {
	return hash->h_lo.n_entries + hash->h_hi.n_entries;
}

/** Test if a hash contains a key.
 *
 * Test if a hash (set or map) contains the specified key.
 *
 * @param hash Pointer to hash set or map.
 * @param key Key to test for.
 * @return True if hash contains the key, otherwise false.
 */
bool cl_rhash_contains(struct cl_rhash *hash, const void *key) {
	void *vkey = (void *)key;	/* cast away const */
	return cl_rhash_table_contains(&hash->h_lo, vkey) ||
	       cl_rhash_table_contains(&hash->h_hi, vkey);
}

/** Get an arbitrary key.
 *
 * Get an arbitrary key from a hash set or map.
 *
 * @param hash		Pointer to hash set or map.
 * @return Pointer to hash table entry, or NULL if empty.
 */
void *cl_rhash_peek(struct cl_rhash *hash) {
	void **ent = cl_rhash_table_peek(&hash->h_lo);
	if (!ent)
		ent = cl_rhash_table_peek(&hash->h_hi);
	return (ent) ? (*ent) : NULL;
}

/** Get a value from a hash map.
 *
 * Get a value assocated with the given key from a hash map.  NOTE: do not use
 * this function for hash sets; use cl_rhash_contains instead.
 *
 * @param hash		Pointer to hash map.
 * @param key		Key to look up.
 * @return value Associated with key, or NULL if not found.
 */
const void *cl_rhash_get(struct cl_rhash *hash, const void *key) {
	if (!cl_rhash_is_map(hash))
		return NULL;
	void *vkey = (void *)key;	/* cast away const */
	void **ent = cl_rhash_table_entry(&hash->h_lo, &vkey);
	if (ent)
		return *(ent + 1);
	else {
		ent = cl_rhash_table_entry(&hash->h_hi, &vkey);
		if (ent)
			return *(ent + 1);
	}
	return NULL;
}

/** Check if one entry from a hash set or map should be moved higher (from
 * the low table to the high table).
 *
 * @param hash Pointer to hash set or map.
 */
static bool cl_rhash_should_move_higher(const struct cl_rhash *hash) {
	int n_lo = hash->h_lo.n_entries;
	int n_hi = hash->h_hi.n_entries;
	int n_thresh = cl_rhash_table_limit(&hash->h_hi);
	return (n_lo > 0) && (n_hi >= n_thresh - n_lo * 2);
}

/** Move one entry in a hash set or map from low to high table.
 *
 * @param hash		Pointer to hash set or map.
 */
static void cl_rhash_move_higher(struct cl_rhash *hash) {
	void **ent = cl_rhash_table_peek(&hash->h_lo);
	if (ent) {
		void *tent[2];		/* temporary entry */
		cl_rhash_table_entry_copy(&hash->h_lo, tent, ent);
		void *key = cl_rhash_table_remove(&hash->h_lo, ent);
		if (key)
			cl_rhash_table_insert(&hash->h_hi, tent);
	}
}

/** Move one entry to higher table in a hash set or map if necessary.
 *
 * @param hash		Pointer to hash set or map.
 */
static void cl_rhash_check_move_higher(struct cl_rhash *hash) {
	if (cl_rhash_should_move_higher(hash))
		cl_rhash_move_higher(hash);
}

/** Check if a hash set or map should expand.
 *
 * @param hash Pointer to hash set or map.
 */
static bool cl_rhash_should_expand(const struct cl_rhash *hash) {
	return (hash->h_hi.order < CL_HASH_MAX_ORDER) &&
	       (hash->h_lo.n_entries == 0) &&
	       (hash->h_hi.n_entries >= cl_rhash_table_limit(&hash->h_hi));
}

/** Expand a hash set or map.
 *
 * The low table must be empty to expand.  The old high table becomes the low
 * table and a new high table is allocated at double the size.
 *
 * @param hash Pointer to hash set or map.
 */
static void cl_rhash_expand(struct cl_rhash *hash) {
	cl_rhash_table_clone(&hash->h_lo, &hash->h_hi);
	hash->h_hi.order++;
	hash->h_hi.n_entries = 0;
	hash->h_hi.n_peek = cl_rhash_table_size(&hash->h_hi);
	cl_rhash_table_alloc(&hash->h_hi);
}

/** Expand a hash set or map if necessary.
 *
 * @param hash		Pointer to hash set or map.
 */
static void cl_rhash_check_expand(struct cl_rhash *hash) {
	if (cl_rhash_should_expand(hash))
		cl_rhash_expand(hash);
}

/** Insert an entry into a hash set or map.
 *
 * @param hash		Pointer to hash set or map.
 * @param ent		Pointer to hash table entry.  The entry must be
 *                      tbl->n_bytes in size.
 * @return Pointer to existing key, or NULL.
 */
static void *cl_rhash_insert(struct cl_rhash *hash, void **ent) {
	void *key = cl_rhash_table_insert(&hash->h_hi, ent);
	if (!key) {
		/* Don't allow entry in low table to shadow high table */
		key = cl_rhash_table_remove(&hash->h_lo, ent);
		if (!key) {
			cl_rhash_check_move_higher(hash);
			cl_rhash_check_expand(hash);
		}
	}
	cl_rhash_edit(hash);
	return key;
}

/** Add a new entry to a hash set.
 *
 * Add a new entry into a hash set.  The hash table will be expanded if
 * necessary.  NOTE: do not use this function for hash maps; use cl_rhash_put
 * instead.
 *
 * @param hash		Pointer to hash set.
 * @param key		Pointer to key to add.
 * @return Previous equal key, or NULL if key added.
 */
const void *cl_rhash_add(struct cl_rhash *hash, const void *key) {
	if (cl_rhash_is_map(hash))
		return key;
	void *vkey = (void *)key;	/* cast away const */
	return cl_rhash_insert(hash, &vkey);
}

/** Add a new mapping to a hash map.
 *
 * Add a new mapping into a hash map.  The hash table will be expanded if
 * necessary.  NOTE: do not use this function for hash sets; use cl_rhash_add
 * instead.
 *
 * @param hash		Pointer to hash map.
 * @param key		Key to put into hash map.
 * @param value		Value to associate with key.
 * @return Previous equal key, or NULL.
 */
const void *cl_rhash_put(struct cl_rhash *hash, const void *key,
	const void *value)
{
	if (!cl_rhash_is_map(hash))
		return NULL;
	void *tent[2];			/* temporary entry */
	tent[0] = (void *)key;		/* cast away const */
	tent[1] = (void *)value;	/* cast away const */
	return cl_rhash_insert(hash, tent);
}

/** Check if one entry from a hash set or map should be moved lower (from
 * the high table to the low table).
 *
 * @param hash Pointer to hash set or map.
 */
static bool cl_rhash_should_move_lower(const struct cl_rhash *hash) {
	int n_hi = hash->h_hi.n_entries;
	int n_thresh = cl_rhash_table_slimit(&hash->h_hi);
	return (n_hi > 0) && (n_thresh > 0);
}

/** Move one entry in a hash set or map from high to low table.
 *
 * @param hash		Pointer to hash set or map.
 */
static void cl_rhash_move_lower(struct cl_rhash *hash) {
	void **ent = cl_rhash_table_peek(&hash->h_hi);
	if (ent) {
		void *tent[2];		/* temporary entry */
		cl_rhash_table_entry_copy(&hash->h_hi, tent, ent);
		void *key = cl_rhash_table_remove(&hash->h_hi, ent);
		if (key)
			cl_rhash_table_insert(&hash->h_lo, tent);
	}
}

/** Move one entry to lower table in a hash set or map if necessary.
 *
 * @param hash		Pointer to hash set or map.
 */
static void cl_rhash_check_move_lower(struct cl_rhash *hash) {
	if (cl_rhash_should_move_lower(hash))
		cl_rhash_move_lower(hash);
}

/** Check if a hash set or map should shrink.
 *
 * @param hash Pointer to hash set or map.
 */
static bool cl_rhash_should_shrink(const struct cl_rhash *hash) {
	return (hash->h_hi.order > CL_HASH_MIN_ORDER) &&
	       (hash->h_hi.n_entries == 0) &&
	       (hash->h_lo.n_entries <= cl_rhash_table_slimit(&hash->h_hi));
}

/** Shrink a hash set or map.
 *
 * The high table must be empty to shrink.  The old low table becomes the high
 * table and a new low table is allocated at half the size.
 *
 * @param hash Pointer to hash set or map.
 */
static void cl_rhash_shrink(struct cl_rhash *hash) {
	cl_rhash_table_clone(&hash->h_hi, &hash->h_lo);
	hash->h_lo.order--;
	hash->h_lo.n_entries = 0;
	hash->h_lo.n_peek = cl_rhash_table_size(&hash->h_lo);
	cl_rhash_table_alloc(&hash->h_lo);
}

/** Shrink a hash set or map if necessary.
 *
 * @param hash		Pointer to hash set or map.
 */
static void cl_rhash_check_shrink(struct cl_rhash *hash) {
	if (cl_rhash_should_shrink(hash))
		cl_rhash_shrink(hash);
}

/** Remove an entry from a hash set or map.
 *
 * Remove the specified entry from a hash set or map.
 *
 * @param hash Pointer to hash table.
 * @param key Key to be removed.
 * @return Key removed, or NULL if not found.
 */
const void *cl_rhash_remove(struct cl_rhash *hash, const void *key) {
	void *tent[2];		/* temporary entry */
	tent[0] = (void *)key;	/* cast away const */
	tent[1] = NULL;
	void *pkey = cl_rhash_table_remove(&hash->h_lo, tent);
	if (!pkey)
		pkey = cl_rhash_table_remove(&hash->h_hi, tent);
	if (pkey) {
		cl_rhash_check_move_lower(hash);
		cl_rhash_check_shrink(hash);
		cl_rhash_edit(hash);
		return pkey;
	} else
		return NULL;
}

/** Clear a hash set or map.
 *
 * Remove all entries from a hash set or map.
 *
 * @param hash Pointer to hash set or map.
 */
void cl_rhash_clear(struct cl_rhash *hash) {
	assert(hash);
	cl_rhash_table_clear(&hash->h_lo);
	cl_rhash_table_clear(&hash->h_hi);
	cl_pool_clear(hash->pool);
	cl_rhash_edit(hash);
}

/** Create a hash iterator.
 *
 * @param hash Pointer to hash set or map.
 * @return The iterator.
 */
struct cl_rhash_iterator *cl_rhash_iterator_create(struct cl_rhash *hash) {
	struct cl_rhash_iterator *it = cl_pool_alloc(hash->pool);
	assert(hash);
	it->hash = hash;
	it->n_table = CL_RHASH_TABLE_LO;
	it->n_slot = hash->h_lo.n_peek - 1;
#ifndef NDEBUG
	it->n_edit = hash->n_edit;
#endif
	return it;
}

/** Destroy a hash iterator.
 *
 * @param it The iterator.
 */
void cl_rhash_iterator_destroy(struct cl_rhash_iterator *it) {
	assert(it);
	cl_pool_release(it->hash->pool, it);
#ifndef NDEBUG
	it->hash = NULL;
	it->n_table = CL_RHASH_TABLE_NONE;
	it->n_slot = -1;
	it->n_edit = 0;
#endif
}

/** Check hash iterator for validity.
 *
 * @param it The iterator.
 */
static void cl_rhash_iterator_check(struct cl_rhash_iterator *it) {
#ifndef NDEBUG
	struct cl_rhash *hash = it->hash;
	assert(hash);
	assert(hash->n_edit == it->n_edit);
#endif
}

/** Check if hash iterator is done.
 *
 * @param it The iterator.
 * @return true if iterator is done.
 */
static bool cl_rhash_iterator_done(const struct cl_rhash_iterator *it) {
	return it->n_table >= CL_RHASH_TABLE_NONE;
}

/** Get the current hash table for an iterator.
 *
 * @param it The iterator.
 * @return Current hash table, or NULL if done.
 */
static struct cl_rhash_table *cl_rhash_iterator_table(
	const struct cl_rhash_iterator *it)
{
	struct cl_rhash *hash = it->hash;
	assert(hash);
	switch(it->n_table) {
	case CL_RHASH_TABLE_LO:
		return &hash->h_lo;
	case CL_RHASH_TABLE_HI:
		return &hash->h_hi;
	default:
		return NULL;
	}
}

/** Check if iterator is done with current table.
 *
 * @param it The iterator.
 * @return true if table is done.
 */
static bool cl_rhash_iterator_table_done(const struct cl_rhash_iterator *it) {
	const struct cl_rhash_table *tbl = cl_rhash_iterator_table(it);
	return (tbl) && (it->n_slot >= cl_rhash_table_size(tbl));
}

/** Advance to next table in hash iterator.
 *
 * @param it The iterator.
 */
static void cl_rhash_iterator_next_table(struct cl_rhash_iterator *it) {
	if (it->n_table == CL_RHASH_TABLE_LO) {
		it->n_table = CL_RHASH_TABLE_HI;
		it->n_slot = it->hash->h_hi.n_peek;
	} else {
		it->n_table = CL_RHASH_TABLE_NONE;
		it->n_slot = 0;
	}
}

/** Advance to next slot in hash iterator.
 *
 * @param it The iterator.
 */
static void cl_rhash_iterator_next_slot(struct cl_rhash_iterator *it) {
	it->n_slot++;
	while (cl_rhash_iterator_table_done(it))
		cl_rhash_iterator_next_table(it);
}

/** Get the current existing entry from a hash iterator.
 *
 * @param it The iterator.
 * @return Current entry in hash iterator, or NULL if entry is empty.
 */
static void **cl_rhash_iterator_entry(struct cl_rhash_iterator *it) {
	struct cl_rhash_table *tbl = cl_rhash_iterator_table(it);
	if (tbl) {
		void **ent = cl_rhash_table_ptr(tbl, it->n_slot);
		if (cl_rhash_table_entry_exists(tbl, ent))
			return ent;
	}
	return NULL;
}

/** Get the next key from a hash iterator.
 *
 * @param it The iterator.
 * @return Next key in hash iterator, or NULL if done.
 */
const void *cl_rhash_iterator_next(struct cl_rhash_iterator *it) {
	cl_rhash_iterator_check(it);
	while (!cl_rhash_iterator_done(it)) {
		cl_rhash_iterator_next_slot(it);
		void **ent = cl_rhash_iterator_entry(it);
		if (ent)
			return *ent;
	}
	return NULL;
}

/** Get the current entry pointer from a hash iterator.
 *
 * @param it The iterator.
 * @return Current entry in hash iterator, or NULL if done.
 */
static void **cl_rhash_iterator_ptr(struct cl_rhash_iterator *it) {
	struct cl_rhash_table *tbl = cl_rhash_iterator_table(it);
	return (tbl) ? cl_rhash_table_ptr(tbl, it->n_slot) : NULL;
}

/** Get the value associated with most recent key from a hash iterator.
 *
 * @param it The iterator.
 * @return Value associated with most recent key returned, or NULL.
 */
const void *cl_rhash_iterator_value(struct cl_rhash_iterator *it) {
	cl_rhash_iterator_check(it);
	if (cl_rhash_is_map(it->hash)) {
		void **ent = cl_rhash_iterator_ptr(it);
		if (ent)
			return *(ent + 1);
	}
	return NULL;
}
