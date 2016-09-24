/*
 * clump.c	Utility functions for clump library.
 *
 * Copyright (c) 2012-2016  Douglas P Lau
 *
 * Public functions:
 *
 *	cl_compare_int		Compare two integers for sorting
 * 	cl_compare_ptr		Compare two pointers for sorting
 */
#include "clump.h"

/** Clump library version */
const char *CL_VERSION = "0.8.1";

/** Int compare function.
 *
 * @param v0 First integer (cast to void *).
 * @param v1 Second integer (cast to void *).
 * @return One of CL_LESS, CL_GREATER or CL_EQUAL.
 */
cl_compare_t cl_compare_int(const void *v0, const void *v1) {
	int i0 = (int)(long)v0;
	int i1 = (int)(long)v1;
	if(i0 > i1)
		return CL_GREATER;
	if(i0 < i1)
		return CL_LESS;
	return CL_EQUAL;
}

/** Pointer compare function.
 *
 * @param v0 First pointer.
 * @param v1 Second pointer.
 * @return One of CL_LESS, CL_GREATER or CL_EQUAL.
 */
cl_compare_t cl_compare_ptr(const void *v0, const void *v1) {
	if(v0 > v1)
		return CL_GREATER;
	if(v0 < v1)
		return CL_LESS;
	return CL_EQUAL;
}
