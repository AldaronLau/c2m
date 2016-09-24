/*
 * bitarray.c		A simple bit array
 *
 * Copyright (c) 2011-2012  Douglas P Lau
 *
 * Public functions:
 *
 *	cl_bitarray_create	Create a bit array
 *	cl_bitarray_destroy	Destroy a bit array
 *	cl_bitarray_wrap	Wrap a byte buffer
 *	cl_bitarray_clear	Clear a bit array
 *	cl_bitarray_get		Get the value of one bit
 *	cl_bitarray_pop		Pop one bit off a bit array
 *	cl_bitarray_get_range	Get a range of bits
 *	cl_bitarray_pop_range	Pop a range of bits
 *	cl_bitarray_set		Set the value of one bit
 *	cl_bitarray_push	Push one bit onto a bit array
 *	cl_bitarray_set_range	Set a range of bits
 *	cl_bitarray_push_range	Push a range of bits
 */
/** \file
 *
 * A bit array is a fixed-length array of bits.  It is implemented by wrapping
 * a byte buffer.  The first bit in the array (index 0) is the highest bit (7)
 * of the first byte.  The ninth bit (8) is the highest bit (7) of the second
 * byte.
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "clump.h"

/** Bit array structure.
 */
struct cl_bitarray {
	unsigned char	*buf;
	unsigned int	n_bits;
	unsigned int	pos;
};

/** Create a bit array.
 *
 * @return Pointer to the bit array.
 */
struct cl_bitarray *cl_bitarray_create(void) {
	struct cl_bitarray *ba = malloc(sizeof(struct cl_bitarray));
	assert(ba);
	ba->buf = NULL;
	ba->n_bits = 0;
	ba->pos = 0;
	return ba;
}

/** Destroy a bit array.
 *
 * @param ba Pointer to the bit array.
 */
void cl_bitarray_destroy(struct cl_bitarray *ba) {
	assert(ba);
#ifndef NDEBUG
	ba->buf = NULL;
#endif
	free(ba);
}

/** Wrap a byte buffer.
 *
 * @param ba Pointer to the bit array.
 * @param buf Memory buffer.
 * @param n_bits Number of bits in the array.
 */
void cl_bitarray_wrap(struct cl_bitarray *ba, unsigned char *buf,
	unsigned int n_bits)
{
	ba->buf = buf;
	ba->n_bits = n_bits;
	ba->pos = 0;
}

/** Clear a bit array.
 *
 * @param ba Pointer to the bit array.
 */
void cl_bitarray_clear(struct cl_bitarray *ba) {
	assert(ba);
	if(ba->buf)
		memset(ba->buf, 0, ba->n_bits / 8);
	ba->pos = 0;
}

/** Get the number of bytes.
 *
 * @param ba Pointer to the bit array.
 * @return Number of bytes in the bit array.
 */
unsigned int cl_bitarray_bytes(struct cl_bitarray *ba) {
	unsigned int n = ba->pos < ba->n_bits ? ba->pos : ba->n_bits;
	return (n + 7) / 8;
}

/** Get the value of one bit.
 *
 * @param ba Pointer to the bit array.
 * @param i Array index (starting from 0).
 * @return Bit value (0 or 1), or -1 if out of bounds.
 */
int cl_bitarray_get(struct cl_bitarray *ba, unsigned int i) {
	if(i < ba->n_bits) {
		unsigned int n_byte = i / 8;
		unsigned int n_bit = 7 - i % 8;
		return (ba->buf[n_byte] >> n_bit) & 0x01;
	} else
		return -1;
}

/** Pop one bit off a bit array.
 *
 * @param ba Pointer to the bit array.
 * @return Bit value (0 or 1), or -1 if out of bounds.
 */
int cl_bitarray_pop(struct cl_bitarray *ba) {
	return cl_bitarray_get(ba, ba->pos++);
}

/** Get a range of bits.
 *
 * @param ba Pointer to the bit array.
 * @param i Array index (starting from 0).
 * @param n Number of bits to get (1-31).
 * @return Bit mask of the requested range, or -1 if out of bounds.
 */
int cl_bitarray_get_range(struct cl_bitarray *ba, unsigned int i,
	unsigned int n)
{
	if(i + n < ba->n_bits && n < 32) {
		unsigned int b, r = 0;
		for(b = i; b < i + n; b++) {
			unsigned int n_byte = b / 8;
			unsigned int n_bit = 7 - b % 8;
			r = (r << 1) | ((ba->buf[n_byte] >> n_bit) & 0x01);
		}
		return r;
	} else
		return -1;
}

/** Pop a range of bits.
 *
 * @param ba Pointer to the bit array.
 * @param n Number of bits to pop (1-31).
 * @return Bit mask of the popped range, or -1 if out of bounds.
 */
int cl_bitarray_pop_range(struct cl_bitarray *ba, unsigned int n) {
	int r = cl_bitarray_get_range(ba, ba->pos, n);
	ba->pos += n;
	return r;
}

/** Set the value of one bit.
 *
 * @param ba Pointer to the bit array.
 * @param i Array index (starting from 0).
 * @param v Bit value (0 or 1).
 * @return 0 on success, -1 if out of bounds.
 */
int cl_bitarray_set(struct cl_bitarray *ba, unsigned int i, unsigned int v) {
	if(i < ba->n_bits) {
		unsigned int n_byte = i / 8;
		unsigned int n_bit = 7 - i % 8;
		unsigned int mask = ba->buf[n_byte] & ~(0x01 << n_bit);
		ba->buf[n_byte] = mask | ((v & 0x01) << n_bit);
		return 0;
	} else
		return -1;
}

/** Push one bit onto a bit array.
 *
 * @param ba Pointer to the bit array.
 * @param v Bit value (0 or 1).
 * @return 0 on success, -1 if out of bounds.
 */
int cl_bitarray_push(struct cl_bitarray *ba, unsigned int v) {
	return cl_bitarray_set(ba, ba->pos++, v);
}

/** Set a range of bits.
 *
 * @param ba Pointer to the bit array.
 * @param i Array index (starting from 0).
 * @param n Number of bits to set (1-31).
 * @param v Bit values.
 * @return 0 on success, -1 if out of bounds.
 */
int cl_bitarray_set_range(struct cl_bitarray *ba, unsigned int i,
	unsigned int n, unsigned int v)
{
	if(i + n < ba->n_bits && n < 32) {
		unsigned int b;
		for(b = i; b < i + n; b++) {
			unsigned int n_byte = b / 8;
			unsigned int n_bit = 7 - b % 8;
			unsigned int val = (v >> (i + n - b - 1)) & 0x01;
			unsigned int mask = ba->buf[n_byte] & ~(0x01 << n_bit);
			ba->buf[n_byte] = mask | ((val & 0x01) << n_bit);
		}
		return 0;
	} else
		return -1;
}

/** Push a range of bits.
 *
 * @param ba Pointer to the bit array.
 * @param n Number of bits to set (1-31).
 * @param v Bit values.
 * @return 0 on success, -1 if out of bounds.
 */
int cl_bitarray_push_range(struct cl_bitarray *ba, unsigned int n,
	unsigned int v)
{
	int r = cl_bitarray_set_range(ba, ba->pos, n, v);
	ba->pos += n;
	return r;
}
