/*
 * array.c	A resizable array
 *
 * Copyright (c) 2015  Douglas P Lau
 *
 * Public functions:
 *
 *	cl_array_create			Create an array
 *	cl_array_destroy		Destroy an array
 *	cl_array_is_empty		Check if an array is empty
 *	cl_array_count			Count the items in an array
 *	cl_array_borrow			Borrow an item from an array
 *	cl_array_add			Add an item to an array
 *	cl_array_insert			Insert an item into an array
 *	cl_array_remove			Remove an item from an array
 *	cl_array_pop			Pop an item from an array
 *	cl_array_clear			Clear all items from an array
 */
/** \file
 *
 * A simple resizable array.
 */
#include <assert.h>
#include <string.h>
#include "clump.h"

/** Array structure.
 */
struct cl_array {
	void			*store;		/**< actual array store */
	size_t			i_size;		/**< size of items */
	uint32_t		n_size;		/**< size of array */
	uint32_t		n_items;	/**< number of items */
};

/** Get the minimum array size.
 *
 * @param n The requested size.
 * @return The minimum size.
 */
static uint32_t cl_array_min_size(uint32_t n) {
	return (n > 16) ? n : 16;
}

/** Create an array.
 *
 * @param s Size of items in array.
 * @param n Initial number of items in array.
 * @return The new array.
 */
struct cl_array *cl_array_create(size_t s, uint32_t n) {
	struct cl_array *arr = malloc(sizeof(struct cl_array));
	assert(arr);
	arr->i_size = s;
	arr->n_size = cl_array_min_size(n);
	arr->n_items = 0;
	arr->store = malloc(arr->i_size * arr->n_size);
	assert(arr->store);
	return arr;
}

/** Destroy an array.
 *
 * @param arr The array.
 */
void cl_array_destroy(struct cl_array *arr) {
	assert(arr);
	assert(arr->store);
	free(arr->store);
#ifndef NDEBUG
	arr->store = NULL;
#endif
	free(arr);
}

/** Test if an array is empty.
 *
 * @param arr The array.
 * @return true if there are no items in the array; false otherwise.
 */
bool cl_array_is_empty(struct cl_array *arr) {
	return !arr->n_items;
}

/** Get a count of items in an array.
 *
 * @param arr The array.
 * @return Count of items in the array.
 */
uint32_t cl_array_count(struct cl_array *arr) {
	return arr->n_items;
}

/** Get a pointer to an item in an array.
 *
 * @param arr The array.
 * @param i Index of item.
 * @return Pointer to item.
 */
static void *cl_array_item(struct cl_array *arr, uint32_t i) {
	assert(i < arr->n_size);
	return (char *) arr->store + (arr->i_size * i);
}

/** Borrow a pointer to an item in an array.
 *
 * @param arr The array.
 * @param i Index of item.
 * @return Borrowed pointer, or NULL if index is out of bounds.
 */
void *cl_array_borrow(struct cl_array *arr, uint32_t i) {
	return (i < arr->n_items) ? cl_array_item(arr, i) : NULL;
}

/** Expand an array to double its current size.
 *
 * @param arr The array.
 */
static void cl_array_expand(struct cl_array *arr) {
	arr->n_size *= 2;
	arr->store = realloc(arr->store, arr->i_size * arr->n_size);
	assert(arr->store);
}

/** Add an item to the end of an array.
 *
 * @param arr The array.
 * @return Borrowed pointer to item.
 */
void *cl_array_add(struct cl_array *arr) {
	uint32_t i = arr->n_items;
	if (arr->n_items == arr->n_size)
		cl_array_expand(arr);
	arr->n_items++;
	return cl_array_item(arr, i);
}

/** Insert an item into an array.
 *
 * @param arr The array.
 * @param i Index of item to insert.
 * @return Borrowed pointer to item.
 */
void *cl_array_insert(struct cl_array *arr, uint32_t i) {
	assert(i <= arr->n_items);
	if (arr->n_items == arr->n_size)
		cl_array_expand(arr);
	if (i < arr->n_items) {
		void *src = cl_array_item(arr, i);
		void *dst = cl_array_item(arr, i + 1);
		size_t n = arr->i_size * (arr->n_items - i);
		memmove(dst, src, n);
	}
	arr->n_items++;
	return cl_array_item(arr, i);
}

/** Remove an item from an array.
 *
 * @param arr The array.
 * @param i Index of item to remove.
 * @return true if item was removed.
 */
bool cl_array_remove(struct cl_array *arr, uint32_t i) {
	uint32_t j = i + 1;
	if (i >= arr->n_items)
		return false;
	if (j < arr->n_items) {
		void *src = cl_array_item(arr, j);
		void *dst = cl_array_item(arr, i);
		size_t n = arr->i_size * (arr->n_items - j);
		memmove(dst, src, n);
	}
	arr->n_items--;
	return true;
}

/** Pop an item from an array.
 *
 * Remove the last item from an array.  This is useful for using the array
 * as a stack or FIFO.
 *
 * @param arr The array.
 * @return Borrowed pointer to item, or NULL if array is empty.
 */
void *cl_array_pop(struct cl_array *arr) {
	if (arr->n_items) {
		arr->n_items--;
		return cl_array_item(arr, arr->n_items);
	} else
		return NULL;
}

/** Clear an array.
 *
 * @param arr The array.
 */
void cl_array_clear(struct cl_array *arr) {
	arr->n_items = 0;
}
