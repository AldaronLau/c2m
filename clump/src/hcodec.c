/*
 * hcodec.c	A simple huffman codec module.
 *
 * Copyright (c) 2011-2012  Douglas P Lau
 *
 * Public functions:
 *
 *	cl_hcodec_create	Create a huffman codec
 *	cl_hcodec_destroy	Destroy a huffman codec
 *	cl_hcodec_encode	Encode a block of data
 *	cl_hcodec_decode	Decode a block of data
 *
 * This is a data compression module intended to compress fixed-length
 * blocks of data (as opposed to streams).  Each block of data is treated
 * independently, with an accompanying code book.  Very small blocks (less
 * than 100 bytes) and very random blocks will not compress well.  The goal
 * is to compress 4K blocks optimally.  This is not a very fancy module --
 * it simply uses the huffman algorithm with a canonical encoding.
 */
#include <assert.h>	/* assert */
#include <stdlib.h>	/* malloc */
#include <string.h>	/* memset */
#include <stdio.h>
#include "clump.h"
#include "hcodec.h"

/** A symbol is one member of a code alphabet.
 */
struct cl_symbol {
	unsigned int	code;		/* bit code */
	unsigned int	n_bits;		/* number of bits */
	unsigned int	n_refs;		/* reference count of symbol in text */
	unsigned int	value;		/* uncompressed symbol value */
};

/** Debug a symbol.  Used to generate codebooks.
 */
static void cl_symbol_debug(struct cl_symbol *sym) {
#ifndef NDEBUG
	int b;
	printf("\t[%d] = { 0x%03x, %d },\t/* ", sym->value, sym->code,
		sym->n_bits);
	for(b = sym->n_bits - 1; b >= 0; b--)
		printf("%1d", (sym->code >> b) & 1);
	printf(" */\n");
#endif
}

/** Node in a huffman tree.
 */
struct cl_hnode {
	struct cl_hnode		*left;		/* left child node */
	struct cl_hnode		*right;		/* right child node */
	struct cl_symbol	*symbol;	/* symbol (leaf nodes only) */
	unsigned int		weight;		/* node weight */
};

/** Clear a node.
 */
static void cl_hnode_clear(struct cl_hnode *n) {
	n->left = NULL;
	n->right = NULL;
	n->symbol = NULL;
	n->weight = 0;
}

/** Compare two huffman tree nodes.
 */
static cl_compare_t cl_hnode_compare(const void *v0, const void *v1) {
	const struct cl_hnode *n0 = v0;
	const struct cl_hnode *n1 = v1;
	if(n0->weight < n1->weight)
		return -1;
	if(n0->weight > n1->weight)
		return 1;
	/* weights are equal, prefer leaf nodes first */
	if(n0->symbol && (n1->symbol == NULL))
		return -1;
	if(n1->symbol && (n0->symbol == NULL))
		return 1;
	if(n0->symbol && n1->symbol) {
		/* both nodes are leaf nodes, compare symbols */
		if(n0->symbol < n1->symbol)
			return -1;
		if(n0->symbol > n1->symbol)
			return 1;
	}
	return 0;
}

/** Huffman codec structure.
 */
struct cl_hcodec {
	struct cl_symbol	symbols[MAX_SYMBOLS];	/* symbol table */
	struct cl_hnode		nodes[MAX_NODES];	/* array of nodes */
	struct cl_tree		*pqueue;		/* priority queue */
	struct cl_bitarray	*bits;			/* bit array */
};

/** Create a huffman codec.
 */
struct cl_hcodec *cl_hcodec_create(void) {
	unsigned int i;
	struct cl_hcodec *hc = malloc(sizeof(struct cl_hcodec));
	assert(hc);
	hc->pqueue = cl_tree_create_set(cl_hnode_compare);
	hc->bits = cl_bitarray_create();
	memset(hc->symbols, 0, MAX_SYMBOLS * sizeof(struct cl_symbol));
	for(i = 0; i < MAX_SYMBOLS; i++)
		hc->symbols[i].value = i;
	return hc;
}

/** Destroy a huffman codec.
 */
void cl_hcodec_destroy(struct cl_hcodec *hc) {
	assert(hc);
	cl_bitarray_destroy(hc->bits);
	cl_tree_destroy(hc->pqueue);
#ifndef NDEBUG
	hc->pqueue = NULL;
	hc->bits = NULL;
#endif
	free(hc);
}

/** Scan symbols in a data buffer.
 */
static void cl_hcodec_scan_symbols(struct cl_hcodec *hc,
	const unsigned char *in, unsigned int size)
{
	unsigned int i;
	const unsigned char *buf;

	for(i = 0; i < MAX_SYMBOLS; i++)
		hc->symbols[i].n_refs = 0;
	for(buf = in; buf < in + size; buf++)
		hc->symbols[*buf].n_refs++;
}

/** Build leaf nodes of huffman tree.
 */
static struct cl_hnode *cl_hcodec_build_leaf_nodes(struct cl_hcodec *hc) {
	struct cl_symbol *sym;
	struct cl_hnode *n = hc->nodes;
	for(sym = hc->symbols; sym < hc->symbols + MAX_SYMBOLS; sym++) {
		if(sym->n_refs) {
			n->left = NULL;
			n->right = NULL;
			n->symbol = sym;
			n->weight = sym->n_refs;
			cl_tree_add(hc->pqueue, n);
			n++;
		}
	}
	return n;
}

/** Build the huffman tree.
 */
static struct cl_hnode *cl_hcodec_build(struct cl_hcodec *hc) {
	struct cl_hnode *n = cl_hcodec_build_leaf_nodes(hc);
	while(cl_tree_count(hc->pqueue) > 1) {
		const struct cl_hnode *n0, *n1;
		n0 = cl_tree_peek(hc->pqueue);
		assert(n0);
		cl_tree_remove(hc->pqueue, n0);
		n1 = cl_tree_peek(hc->pqueue);
		assert(n1);
		cl_tree_remove(hc->pqueue, n1);
		assert(n < hc->nodes + MAX_NODES);
		n->left = (struct cl_hnode *)n1;
		n->right = (struct cl_hnode *)n0;
		n->weight = n0->weight + n1->weight;
		n->symbol = NULL;
		cl_tree_add(hc->pqueue, n);
		n++;
	}
	n = (struct cl_hnode *)cl_tree_peek(hc->pqueue);
	assert(n);
	cl_tree_remove(hc->pqueue, n);
	return n;
}

/** Assign codes to symbols in huffman tree.
 */
static void cl_hnode_assign_code(struct cl_hnode *n, unsigned int code,
	unsigned int n_bits)
{
	if(n->symbol) {
		n->symbol->code = code;
		/* If there is only one node in the tree,
		 * we still need a bit to encode it. */
		n->symbol->n_bits = n_bits ? n_bits : 1;
	} else {
		code <<= 1;
		cl_hnode_assign_code(n->left, code, n_bits + 1);
		code++;
		cl_hnode_assign_code(n->right, code, n_bits + 1);
	}
}

/** Compare two symbols for canonical ordering.
 */
static int cl_symbol_canonical_compare(const void *v0, const void *v1) {
	const struct cl_symbol **ss0 = (const struct cl_symbol **)v0;
	const struct cl_symbol *s0 = *ss0;
	const struct cl_symbol **ss1 = (const struct cl_symbol **)v1;
	const struct cl_symbol *s1 = *ss1;
	if(s0->n_bits < s1->n_bits)
		return -1;
	if(s0->n_bits > s1->n_bits)
		return 1;
	if(s0->value < s1->value)
		return -1;
	if(s0->value > s1->value)
		return 1;
	else
		return 0;
}

/** Make the codebook canonical.
 */
static void cl_hcodec_make_canonical(struct cl_hcodec *hc) {
	struct cl_symbol *csym[MAX_SYMBOLS];
	unsigned int i;
	unsigned int code = 0;
	unsigned int n_bits = 0;

	for(i = 0; i < MAX_SYMBOLS; i++)
		csym[i] = hc->symbols + i;
	qsort(csym, MAX_SYMBOLS, sizeof(struct cl_symbol *),
		cl_symbol_canonical_compare);
	for(i = 0; i < MAX_SYMBOLS; i++) {
		struct cl_symbol *sym = csym[i];
		while(n_bits < sym->n_bits) {
			code <<= 1;
			n_bits++;
		}
		sym->code = code;
		if(n_bits)
			code++;
	}
}

/** Encode the huffman codebook.
 */
static void cl_hcodec_encode_codebook(struct cl_hcodec *hc) {
	unsigned int i;
	for(i = 0; i < MAX_SYMBOLS; i++) {
		struct cl_symbol *sym = hc->symbols + i;
		cl_symbol_debug(sym);
		cl_bitarray_push_range(hc->bits, CL_HCODEC_BOOK[sym->n_bits][1],
			CL_HCODEC_BOOK[sym->n_bits][0]);
	}
}

/** Encode the data in a buffer.
 */
static void cl_hcodec_encode_buffer(struct cl_hcodec *hc,
	const unsigned char *in, unsigned int n_in)
{
	const unsigned char *buf;

	for(buf = in; buf < in + n_in; buf++) {
		struct cl_symbol *sym = hc->symbols + (*buf);
		cl_bitarray_push_range(hc->bits, sym->n_bits, sym->code);
	}
}

/** Encode a block of data.
 */
int cl_hcodec_encode(struct cl_hcodec *hc, const unsigned char *in,
	unsigned int n_in, unsigned char *out, unsigned int n_out)
{
	struct cl_hnode *root;

	cl_hcodec_scan_symbols(hc, in, n_in);
	root = cl_hcodec_build(hc);
	if(root) {
		cl_bitarray_wrap(hc->bits, out, n_out * 8);
		cl_bitarray_clear(hc->bits);
		cl_hnode_assign_code(root, 0, 0);
		cl_hcodec_make_canonical(hc);
		cl_hcodec_encode_codebook(hc);
		cl_hcodec_encode_buffer(hc, in, n_in);
		return cl_bitarray_bytes(hc->bits);
	} else
		return 0;
}

/** Decode number of bits for a codebook symbol.
 */
static int cl_hcodec_decode_codebook_bits(struct cl_hcodec *hc) {
	unsigned int n_bits;
	unsigned int code = 0;
	for(n_bits = 1; n_bits <= CL_HCODEC_BOOK_MAX_BITS; n_bits++) {
		int b = cl_bitarray_pop(hc->bits);
		if(b >= 0) {
			int i;
			code |= b;
			for(i = 0; i < MAX_SYMBOLS; i++) {
				if(CL_HCODEC_BOOK[i][1] == n_bits &&
				   CL_HCODEC_BOOK[i][0] == code)
					return i;
			}
			code <<= 1;
		} else
			break;
	}
	/* Symbol not found in codebook */
	return -1;
}

/** Restore one symbol node.
 */
static struct cl_hnode *cl_hcodec_restore_node(struct cl_hcodec *hc,
	struct cl_hnode *n, struct cl_symbol *sym)
{
	struct cl_hnode	*node = hc->nodes;
	unsigned int n_bits;

	for(n_bits = 1; n_bits <= sym->n_bits; n_bits++) {
		unsigned int b = (sym->code >> (sym->n_bits - n_bits)) & 0x01;
		if(node->left == NULL) {
			cl_hnode_clear(n);
			node->left = n;
			n++;
			if(n >= hc->nodes + MAX_NODES)
				return NULL;
		}
		if(node->right == NULL) {
			cl_hnode_clear(n);
			node->right = n;
			n++;
			if(n >= hc->nodes + MAX_NODES)
				return NULL;
		}
		if(b)
			node = node->right;
		else
			node = node->left;
	}
	node->symbol = sym;
	return n;
}

/** Restore tree of nodes.
 */
static struct cl_hnode *cl_hcodec_restore_tree(struct cl_hcodec *hc) {
	struct cl_hnode *n = hc->nodes;
	unsigned int i;
	cl_hnode_clear(n);
	n++;
	for(i = 0; i < MAX_SYMBOLS; i++) {
		struct cl_symbol *sym = hc->symbols + i;
		if(sym->n_bits)
			n = cl_hcodec_restore_node(hc, n, sym);
		if(n == NULL)
			return NULL;
		cl_symbol_debug(sym);
	}
	return hc->nodes;
}

/** Decode a canonical huffman codebook.
 */
static struct cl_hnode *cl_hcodec_decode_codebook(struct cl_hcodec *hc) {
	unsigned int i;
	for(i = 0; i < MAX_SYMBOLS; i++) {
		struct cl_symbol *sym = hc->symbols + i;
		int n_bits = cl_hcodec_decode_codebook_bits(hc);
		if(n_bits >= 0)
			sym->n_bits = n_bits;
		else
			return NULL;
	}
	cl_hcodec_make_canonical(hc);
	return cl_hcodec_restore_tree(hc);
}

/** Decode one symbol.
 */
static struct cl_symbol *cl_hcodec_decode_symbol(struct cl_hcodec *hc,
	struct cl_hnode *root)
{
	struct cl_hnode *n = root;
	while(n && n->symbol == NULL) {
		int b = cl_bitarray_pop(hc->bits);
		if(b < 0)
			return NULL;
		if(b)
			n = n->right;
		else
			n = n->left;
	}
	if(n)
		return n->symbol;
	else
		return NULL;
}

/** Decode a block of data.
 */
int cl_hcodec_decode(struct cl_hcodec *hc, unsigned char *in,
	unsigned int n_in, unsigned char *out, unsigned int n_out)
{
	struct cl_hnode		*root;
	unsigned int		i;
	unsigned char		*buf;

	cl_bitarray_wrap(hc->bits, in, n_in * 8);
	root = cl_hcodec_decode_codebook(hc);
	if(!root)
		return -1;
	buf = out;
	for(i = 0; i < n_out; i++) {
		struct cl_symbol *sym = cl_hcodec_decode_symbol(hc, root);
		if(sym) {
			*buf = sym->value;
			buf++;
		} else
			break;
	}
	return buf - out;
}
