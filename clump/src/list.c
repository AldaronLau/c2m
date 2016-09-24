/*
 * list.c	A generic linked list
 *
 * Copyright (c) 2007-2012  Douglas P Lau
 *
 * Public functions:
 *
 *	cl_list_create			Create a linked list
 *	cl_list_destroy			Destroy a linked list
 *	cl_list_is_empty		Check if a list is empty
 *	cl_list_count			Count the items in a list
 *	cl_list_add			Add an item to a list
 *	cl_list_add_tail		Add an item to tail of a list
 *	cl_list_remove			Remove an item from a list
 *	cl_list_contains		Check if a list contains an item
 *	cl_list_pop			Pop an item from a list
 *	cl_list_clear			Clear all items from a list
 *	cl_list_iterator_create		Create a list iterator
 *	cl_list_iterator_destroy	Destroy a list iterator
 *	cl_list_iterator_next		Get next item from an iterator
 *	cl_list_iterator_remove		Remove current item from an iterator
 */
/** \file
 *
 * A linked list is a simple ordered collection.  Iterating and adding items
 * to the list are fast operations.
 */
#include <assert.h>
#include <stdlib.h>
#include "clump.h"

/** List node structure.
 */
struct cl_list_node {
	void			*item;		/**< list item */
	struct cl_list_node	*next;		/**< link to next node */
};

/** List iterator structure.
 */
struct cl_list_iterator {
	struct cl_list		*list;		/**< list */
	struct cl_list_node	*prev;		/**< previous list node */
};

/** Linked list structure.
 */
struct cl_list {
	struct cl_pool		*pool;		/**< node memory pool */
	struct cl_list_node	*head;		/**< link to head node */
	struct cl_list_node	*tail;		/**< link to tail node */
	unsigned int		n_entries;	/**< number of entries */
};

/** Get the max of two size_t values */
static inline size_t max_size_t(size_t a, size_t b) {
	return a > b ? a : b;
}

/** Create a linked list.
 *
 * Allocate a new cl_list structure on the heap.
 *
 * @return Pointer to a new list.
 */
struct cl_list *cl_list_create(void) {
	struct cl_list *list = malloc(sizeof(struct cl_list));
	assert(list);
	list->pool = cl_pool_create(max_size_t(sizeof(struct cl_list_node),
		sizeof(struct cl_list_iterator)));
	list->head = NULL;
	list->tail = NULL;
	list->n_entries = 0;
	return list;
}

/** Destroy a linked list.
 *
 * Destroy the specified linked list, releasing all its resources.
 *
 * @param list Pointer to the list.
 */
void cl_list_destroy(struct cl_list *list) {
	assert(list);
	cl_pool_destroy(list->pool);
#ifndef NDEBUG
	list->head = NULL;
	list->tail = NULL;
	list->pool = NULL;
#endif
	free(list);
}

/** Test if a list is empty.
 *
 * @return true if there are no items in the list; false otherwise.
 */
bool cl_list_is_empty(struct cl_list *list) {
	return list->head == NULL;
}

/** Get a count of items in a list.
 *
 * @param list Pointer to the list.
 * @return Count of items in the list.
 */
unsigned int cl_list_count(struct cl_list *list) {
	return list->n_entries;
}

/** Add an item to a list.
 *
 * Add a new item at the head of a linked list.
 *
 * @param list Pointer to the list.
 * @param item Pointer to item to add.
 * @return Pointer to added item.
 */
void *cl_list_add(struct cl_list *list, void *item) {
	struct cl_list_node *n = cl_pool_alloc(list->pool);
	n->item = item;
	n->next = list->head;
	list->head = n;
	if(list->tail == NULL)
		list->tail = n;
	list->n_entries++;
	return item;
}

/** Add an item to the tail of a list.
 *
 * Add a new item at the tail of a linked list.
 *
 * @param list Pointer to the list.
 * @param item Pointer to item to add.
 * @return Pointer to added item.
 */
void *cl_list_add_tail(struct cl_list *list, void *item) {
	if(list->tail) {
		struct cl_list_node *n = cl_pool_alloc(list->pool);
		n->item = item;
		n->next = NULL;
		list->tail->next = n;
		list->tail = n;
		list->n_entries++;
		return item;
	} else
		return cl_list_add(list, item);
}

/** Remove an item from a list.
 *
 * Remove the specified item from a linked list.
 *
 * @param list Pointer to the list.
 * @param item Pointer to the item to remove.
 * @return Pointer to the removed item, or NULL if not found.
 */
void *cl_list_remove(struct cl_list *list, void *item) {
	struct cl_list_node *n, *p = NULL;
	struct cl_list_node **pp = &list->head;

	for(n = list->head; n; n = n->next) {
		if(n->item == item) {
			*pp = n->next;
			if(!*pp)
				list->tail = p;
			list->n_entries--;
			cl_pool_release(list->pool, n);
			return item;
		}
		pp = &n->next;
		p = n;
	}
	return NULL;
}

/** Check if a list contains an item.
 *
 * Test if a list contains the specified item.
 *
 * @param list Pointer to the list.
 * @param item Pointer to the item to check.
 * @return true If the list contains the specified item; otherwise false.
 */
bool cl_list_contains(struct cl_list *list, void *item) {
	struct cl_list_node *n;

	for(n = list->head; n; n = n->next) {
		if(n->item == item)
			return true;
	}
	return false;
}

/** Pop an item from a list.
 *
 * Remove the head item from a linked list.  This is useful for using the list
 * as a stack or FIFO.
 *
 * @param list Pointer to the list.
 * @return Pointer to the head item in the list, or NULL if the list is empty.
 */
void *cl_list_pop(struct cl_list *list) {
	struct cl_list_node *n = list->head;
	if(n) {
		void *item = n->item;
		list->head = n->next;
		if(n->next)
			n->next = NULL;
		else
			list->tail = NULL;
		list->n_entries--;
		cl_pool_release(list->pool, n);
		return item;
	} else
		return NULL;
}

/** Clear a linked list.
 *
 * Remove all items from a linked list.
 *
 * @param list Pointer to the list.
 */
void cl_list_clear(struct cl_list *list) {
	cl_pool_clear(list->pool);
	list->head = NULL;
	list->tail = NULL;
	list->n_entries = 0;
}

/** Create a list iterator.
 *
 * Create an iterator which can be used to iterate over items in a list.
 *
 * @param list Pointer to the list.
 * @return Pointer to a list iterator.
 */
struct cl_list_iterator *cl_list_iterator_create(struct cl_list *list) {
	struct cl_list_iterator *it = cl_pool_alloc(list->pool);
	it->list = list;
	it->prev = NULL;
	return it;
}

/** Destroy a list iterator.
 *
 * @param it Pointer to the list iterator.
 */
void cl_list_iterator_destroy(struct cl_list_iterator *it) {
	struct cl_list *list = it->list;
#ifndef NDEBUG
	it->list = NULL;
	it->prev = NULL;
#endif
	cl_pool_release(list->pool, it);
}

/** Get previous node from an iterator.
 *
 * @param it Pointer to the iterator.
 * @return Pointer to the previous list node.
 */
static struct cl_list_node *cl_list_iterator_prev(struct cl_list_iterator *it) {
	return (struct cl_list_node *)((long)it->prev & ~1);
}

/** Get current node from an iterator.
 *
 * @param it Pointer to the iterator.
 * @return Pointer to the current list node.
 */
static struct cl_list_node *cl_list_iterator_curr(struct cl_list_iterator *it) {
	struct cl_list_node *p = cl_list_iterator_prev(it);
	return p ? p->next : it->list->head;
}

/** Advance an iterator to the next node.  The lowest bit of the prev pointer
 * is used to indicate that the current item has been removed.
 */
static void cl_list_iterator_advance(struct cl_list_iterator *it) {
	struct cl_list_node *p = it->prev;
	if(p == NULL)
		it->prev = (struct cl_list_node *)1;
	else if((long)p == 1)
		it->prev = it->list->head;
	else if((long)p & 1)
		it->prev = (struct cl_list_node *)((long)p & ~1);
	else
		it->prev = p->next;
}

/** Get next item from an iterator.
 *
 * @param it Pointer to the iterator.
 * @return Pointer to the next item, or NULL at end of list.
 */
void *cl_list_iterator_next(struct cl_list_iterator *it) {
	struct cl_list_node *curr;
	cl_list_iterator_advance(it);
	curr = cl_list_iterator_curr(it);
	return curr ? curr->item : NULL;
}

/** Remove current item from an iterator.
 *
 * @param it Pointer to the iterator.
 */
void cl_list_iterator_remove(struct cl_list_iterator *it) {
	struct cl_list *list = it->list;
	struct cl_list_node *p = cl_list_iterator_prev(it);
	struct cl_list_node *curr = cl_list_iterator_curr(it);
	if(p) {
		p->next = curr ? curr->next : NULL;
		it->prev = (struct cl_list_node *)((long)it->prev | 1);
	} else if(curr) {
		list->head = curr->next;
		it->prev = NULL;
	}
	if(curr) {
		if(!curr->next)
			list->tail = p;
		list->n_entries--;
		cl_pool_release(list->pool, curr);
	}
}
