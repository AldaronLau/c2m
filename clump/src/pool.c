/*
 * pool.c	A memory pool
 *
 * Copyright (c) 2007-2012  Douglas P Lau
 *
 * Public functions:
 *
 *	cl_pool_create		Initialize a memory pool
 *	cl_pool_destroy		Destroy a memory pool
 *	cl_pool_alloc		Allocate a new object from a pool
 *	cl_pool_release		Release an object back to a pool
 *	cl_pool_clear		Release all objects back to a pool
 */
/** \file
 *
 * A memory pool is a special type of memory allocator.  It is designed for
 * efficiently allocating many small objects of the same size.
 *
 * A pool is managed as a linked list of memory blocks.  At the beginning of
 * each block is a pointer to the previous block (if any).  The remainder of
 * the block is split into "slots" of objects of the same size.
 *
 * <pre>
 *      SLOT #	DESCRIPTION			SIZE (BYTES)
 *	0	previous block pointer		sizeof(void *)
 *	1	first object			pool.n_bytes
 *	2	second object			pool.n_bytes
 *	3	third object			pool.n_bytes
 *	N	last object			pool.n_bytes
 * </pre>
 *
 * Before a slot has been allocated to an object, it is initialized with a
 * freelist pointer to the next free slot.  When the freelist is exhausted,
 * a new block is allocated and initialized.
 */
#include <assert.h>
#include <stdlib.h>
#include "clump.h"

/** Memory pool structure.
 */
struct cl_pool {
	void		*block_head;	/* head of used block list */
	void		*block_free;	/* head of free block list */
	void		*free_head;	/* head of free list */
	unsigned int	n_bytes;	/* number of bytes for each object */
	unsigned int	n_slots;	/* number of slots for each block */
};

/** Get the first slot of a block.
 */
static inline void **cl_pool_block_slot(void **block) {
	return block + 1;
}

/** Create a memory pool.
 *
 * Create a new cl_pool structure on the heap.  Memory pools are designed for
 * allocating many small objects of the same size.
 *
 * @param s Size of each object (in bytes).
 * @return Pointer to the memory pool.
 */
struct cl_pool *cl_pool_create(unsigned int s) {
	struct cl_pool *p = malloc(sizeof(struct cl_pool));
	assert(p);
	p->n_bytes = s > sizeof(void *) ? s : sizeof(void *);
	p->n_slots = (4096 - sizeof(void *)) / p->n_bytes;
	if(p->n_slots < 8)
		p->n_slots = 8;
	p->block_head = NULL;
	p->block_free = NULL;
	p->free_head = NULL;
	return p;
}

/** Free memory of a block list.
 */
static void cl_pool_block_free(struct cl_pool *p, void **block) {
	while(block) {
		void *b = block;
		block = *block;
		free(b);
	}
}

/** Destroy a memory pool.
 *
 * Destroy a memory pool that is no longer needed.
 *
 * @param p Memory pool.
 */
void cl_pool_destroy(struct cl_pool *p) {
	assert(p);
	cl_pool_block_free(p, p->block_head);
	cl_pool_block_free(p, p->block_free);
#ifndef NDEBUG
	p->block_head = NULL;
	p->block_free = NULL;
	p->free_head = NULL;
#endif
	free(p);
}

/** Get the size of a block.
 *
 * Calculate the size of a block.
 *
 * @param p Memory pool.
 * @return Size of one block.
 */
static inline size_t cl_pool_block_size(struct cl_pool *p) {
	return sizeof(void *) + p->n_bytes * p->n_slots;
}

/** Allocate a block for the memory pool.
 *
 * @param p Memory pool.
 * @return Pointer to new block.
 */
static void **cl_pool_block_alloc(struct cl_pool *p) {
	if(p->block_free) {
		void **block = p->block_free;
		p->block_free = *block;
		return block;
	} else
		return malloc(cl_pool_block_size(p));
}

/** Initialize a block.
 *
 * A memory pool block is initialized by linking all slots into a freelist.
 *
 * @param p Memory pool.
 * @param block Block pointer.
 */
static void cl_pool_block_init(struct cl_pool *p, void **block) {
	void **slot = cl_pool_block_slot(block);
	char *next = (char *)slot;
	char *last = next + p->n_bytes * p->n_slots;
	for(next += p->n_bytes; next < last; next += p->n_bytes) {
		*slot = next;
		slot = (void **)next;
	}
	*slot = NULL;
}

/** Add a block to a memory pool.
 *
 * When the freelist is empty, this function must be called.  It allocates a
 * new block and initializes the freelist.
 *
 * @param p Memory pool.
 */
static void cl_pool_add_block(struct cl_pool *p) {
	void **block;

	assert(p->free_head == NULL);
	block = cl_pool_block_alloc(p);
	assert(block);
	*block = p->block_head;		/* link to previous block head */
	p->block_head = block;		/* update block head */
	p->free_head = cl_pool_block_slot(block);
	cl_pool_block_init(p, block);
}

/** Allocate an object.
 *
 * Allocate a new object from a memory pool.
 *
 * @param p Memory pool.
 * @return Pointer to new object.
 */
void *cl_pool_alloc(struct cl_pool *p) {
	void **slot;
	if(p->free_head == NULL)
		cl_pool_add_block(p);
	slot = p->free_head;
	p->free_head = *slot;
	return slot;
}

/** Release an object.
 *
 * Release an object back into a memory pool.
 *
 * @param p Memory pool.
 * @param m Pointer to object to release.
 */
void cl_pool_release(struct cl_pool *p, void *m) {
	void **slot = m;

	assert(m);
	*slot = p->free_head;	/* link object back to free list */
	p->free_head = m;	/* update head of free list */
}

/** Clear a memory pool.
 *
 * Release all previously allocated objects back to a memory pool.
 *
 * @param p Memory pool.
 */
void cl_pool_clear(struct cl_pool *p) {
	void **block = p->block_head;
	while(block) {
		void **b = block;
		block = *block;
		*b = p->block_free;	/* link to head of free block list */
		p->block_free = b;	/* update free block head */
	}
	p->block_head = NULL;
	p->free_head = NULL;
}
