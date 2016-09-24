/*
 * tree.c	A generic tree-set or -map
 *
 * Copyright (c) 2007-2014  Douglas P Lau
 *
 * Public functions:
 *
 *	cl_tree_create_set	Create a tree set
 *	cl_tree_create_map	Create a tree map
 * 	cl_tree_destroy		Destroy a tree
 *	cl_tree_count		Count the entries in a tree
 *	cl_tree_contains	Test if a tree contains a key
 *	cl_tree_peek		Get the first key in a tree
 *	cl_tree_get_key		Get a key from a tree
 *	cl_tree_get		Get a value from a tree map
 *	cl_tree_add		Add a key to a tree set
 *	cl_tree_put		Put a mapping into a tree map
 *	cl_tree_remove_key	Remove a key from a tree
 *	cl_tree_remove		Remove a mapping from a tree map
 *	cl_tree_clear		Clear all entries from a tree
 *	cl_tree_iterator_create Create a tree key iterator
 *	cl_tree_iterator_destroy Destroy a tree key iterator
 *	cl_tree_iterator_next	Get the next key from an iterator
 *	cl_tree_iterator_value	Get the value mapped to most recent key
 */
/** \file
 *
 * A tree is a generic collection which implements both sorted sets and sorted
 * maps.
 * A map uses a bit more memory than a set, but allows an arbitrary value to be
 * mapped to each key.
 * Iterating over the keys is a fast operation.
 * The tree is implemented as a left-leaning red-black tree, as described by
 * Robert Sedgewick.
 * The node colors (red/black) are stored in the low bit of link pointers to
 * keep memory usage as small as possible.
 * NOTE: some functions can be used with either sets or maps, but some must
 * only be used with either sets or maps.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "clump.h"

/** Tree node structure.
 */
struct cl_node {
	struct cl_node		*left;		/*< left node link */
	struct cl_node		*right;		/*< right node link */
	const void		*key;		/*< user-defined key */
};

/** Node mapping structure.
 */
struct cl_node_mapping {
	struct cl_node		node;		/*< node structure */
	const void		*value;		/*< value for mapping */
};

/** Tree branch structure.
 */
struct cl_tree_branch {
	struct cl_tree_branch	*branch;	/**< next branch */
	struct cl_node		*node;		/**< tree node */
};

/** Tree iterator structure.
 */
struct cl_tree_iterator {
	struct cl_tree		*tree;		/**< the tree */
	struct cl_tree_branch	*branch;	/**< next branch */
};

/** Check if a node is red */
static inline bool cl_node_is_red(struct cl_node *n) {
	return ((long)n & 1);
}

/** Get a black pointer to a node */
static inline struct cl_node *cl_node_black(struct cl_node *n) {
	return (struct cl_node *)((long)n & ~1);
}

/** Get a red pointer to a node */
static inline struct cl_node *cl_node_red(struct cl_node *n) {
	return (struct cl_node *)((long)n | 1);
}

/** Get a left subnode */
static inline struct cl_node *cl_node_left(struct cl_node *n) {
	return cl_node_black(n)->left;
}

/** Get a right subnode */
static inline struct cl_node *cl_node_right(struct cl_node *n) {
	return cl_node_black(n)->right;
}

/** Get the key associated with a node */
static inline const void *cl_node_key(struct cl_node *n) {
	return cl_node_black(n)->key;
}

/** Set the key associated with a node */
static inline void cl_node_set_key(struct cl_node *n, const void *key) {
	cl_node_black(n)->key = key;
}

/** Get the value associated with a node */
static inline const void *cl_node_value(struct cl_node *n) {
	return ((struct cl_node_mapping *)cl_node_black(n))->value;
}

/** Set the value associated with a node mapping */
static inline void cl_node_set_value(struct cl_node *n, const void *value) {
	struct cl_node_mapping *m = (struct cl_node_mapping *)cl_node_black(n);
	m->value = value;
}

/** Set a left subnode */
static inline void cl_node_set_left(struct cl_node *n, struct cl_node *c) {
	cl_node_black(n)->left = c;
}

/** Set a right subnode */
static inline void cl_node_set_right(struct cl_node *n, struct cl_node *c) {
	cl_node_black(n)->right = c;
}

/** Rotate a node left.
 *
 * <pre>
 *      n                       p
 *     / \                     / \
 *    m   p         ->        n   q
 *       / \                 / \
 *      o   q               m   o
 * </pre>
 */
static struct cl_node *cl_node_rotate_left(struct cl_node *n) {
	struct cl_node *p = cl_node_right(n);
	cl_node_set_right(n, cl_node_left(p));
	cl_node_set_left(p, cl_node_red(n));
	return cl_node_is_red(n) ? cl_node_red(p) : cl_node_black(p);
}

/** Rotate a node right.
 *
 * <pre>
 *        p                     n
 *       / \                   / \
 *      n   q       ->        m   p
 *     / \                       / \
 *    m   o                     o   q
 * </pre>
 */
static struct cl_node *cl_node_rotate_right(struct cl_node *p) {
	struct cl_node *n = cl_node_left(p);
	cl_node_set_left(p, cl_node_right(n));
	cl_node_set_right(n, cl_node_red(p));
	return cl_node_is_red(p) ? cl_node_red(n) : cl_node_black(n);
}

/** Flip the color of a node */
static inline struct cl_node *cl_node_flip(struct cl_node *n) {
	return cl_node_is_red(n) ? cl_node_black(n) : cl_node_red(n);
}

/** Flip the colors of a node and its children */
static struct cl_node *cl_node_flip_colors(struct cl_node *n) {
	cl_node_set_left(n, cl_node_flip(cl_node_left(n)));
	cl_node_set_right(n, cl_node_flip(cl_node_right(n)));
	return cl_node_flip(n);
}

/** Move a red link to the left.
 */
static struct cl_node *cl_node_move_red_left(struct cl_node *n) {
	n = cl_node_flip_colors(n);
	if(cl_node_is_red(cl_node_left(cl_node_right(n)))) {
		cl_node_set_right(n, cl_node_rotate_right(cl_node_right(n)));
		return cl_node_flip_colors(cl_node_rotate_left(n));
	} else
		return n;
}

/** Move a red link to the right.
 */
static struct cl_node *cl_node_move_red_right(struct cl_node *n) {
	n = cl_node_flip_colors(n);
	if(cl_node_is_red(cl_node_left(cl_node_left(n))))
		return cl_node_flip_colors(cl_node_rotate_right(n));
	else
		return n;
}

/** Ensure a subtree leans left after an insert or remove operation.
 */
static struct cl_node *cl_node_lean_left(struct cl_node *n) {
	if(cl_node_is_red(cl_node_right(n)) &&
	  !cl_node_is_red(cl_node_left(n)))
		n = cl_node_rotate_left(n);
	if(cl_node_is_red(cl_node_left(n)) &&
	   cl_node_is_red(cl_node_left(cl_node_left(n))))
		n = cl_node_rotate_right(n);
	if(cl_node_is_red(cl_node_left(n)) &&
	   cl_node_is_red(cl_node_right(n)))
		n = cl_node_flip_colors(n);
	return n;
}

/** Tree structure.
 */
struct cl_tree {
	cl_compare_cb		*fn_compare;	/*< comparison function */
	struct cl_pool		*pool;		/*< tree entry / mapping pool */
	struct cl_node		*leaf;		/*< sentinel for leaf nodes */
	struct cl_node		*root;		/*< root node of tree */
	struct cl_node		*match;		/*< node for insert/remove */
	unsigned int		n_entries;	/*< number of entries in tree */
	bool			is_map;		/*< flag for mapping */
};

/** Test if a node is the leaf sentinel node */
static inline bool cl_tree_is_leaf(struct cl_tree *tree, struct cl_node *n) {
	return cl_node_black(n) == tree->leaf;
}

/** Compare a key with a given node */
static inline cl_compare_t cl_tree_compare(struct cl_tree *tree,
	struct cl_node *n, const void *key)
{
	return tree->fn_compare(key, cl_node_key(n));
}

#ifndef NDEBUG

/** Print one node of a tree */
static void cl_tree_print_node(struct cl_tree *tree, struct cl_node *n,
	int depth)
{
	int i;
	fprintf(stderr, "%10p", cl_node_key(n));
	for(i = 0; i <= depth; i++)
		fprintf(stderr, " - ");
	if(cl_node_is_red(n))
		fprintf(stderr, "red\n");
	else
		fprintf(stderr, "black\n");
}

/** Print a tree. */
static void cl_tree_print_sub(struct cl_tree *tree, struct cl_node *n,
	int depth)
{
	if(cl_tree_is_leaf(tree, n))
		return;
	cl_tree_print_sub(tree, cl_node_left(n), depth + 1);
	cl_tree_print_node(tree, n, depth);
	cl_tree_print_sub(tree, cl_node_right(n), depth + 1);
}

/** Debug a node */
static void cl_node_debug(struct cl_node *n, const char *name) {
	fprintf(stderr, "%8s  key=%p\n", name, cl_node_key(n));
}

/** Debug a subtree.
 *
 * @param tree The tree.
 * @param n Root of subtree.
 * @return Depth of tree, in black nodes (0 if tree is invalid)
 */
static int cl_tree_debug_sub(struct cl_tree *tree, struct cl_node *n) {
	if(cl_tree_is_leaf(tree, n))
		return 1;
	else {
		const void *key;
		int ld, rd;
		struct cl_node *ln = cl_node_left(n);
		struct cl_node *rn = cl_node_right(n);

		key = cl_node_key(n);

		/* Test for consecutive red links */
		if(cl_node_is_red(n)) {
			if(cl_node_is_red(ln)) {
				fprintf(stderr, "Red violation:\n");
				cl_node_debug(n, "NODE");
				cl_node_debug(ln, "LEFT");
				return 0;
			}
			if(cl_node_is_red(rn)) {
				fprintf(stderr, "Red violation:\n");
				cl_node_debug(n, "NODE");
				cl_node_debug(rn, "RIGHT");
				return 0;
			}
		}

		ld = cl_tree_debug_sub(tree, ln);
		rd = cl_tree_debug_sub(tree, rn);

		/* Invalid binary search tree */
		if(ln) {
			const void *lk = cl_node_key(ln);
			if(lk && cl_tree_compare(tree, ln, key) == CL_LESS) {
				fprintf(stderr, "Binary tree violation\n");
				cl_node_debug(n, "NODE");
				cl_node_debug(ln, "LEFT");
				return 0;
			}
		}
		if(rn) {
			const void *rk = cl_node_key(rn);
			if(rk && cl_tree_compare(tree, rn, key) == CL_GREATER) {
				fprintf(stderr, "Binary tree violation\n");
				cl_node_debug(n, "NODE");
				cl_node_debug(rn, "RIGHT");
				return 0;
			}
		}

		/* Black height mismatch */
		if(ld && rd && ld != rd) {
			fprintf(stderr, "Black violation %d != %d\n", ld, rd);
			cl_tree_print_sub(tree, n, 0);
			fprintf(stderr, "--------\n");
			return 0;
		}

		/* Only count black links */
		if(ld && rd)
			return cl_node_is_red(n) ? ld : ld + 1;
		else
			return 0;
	}
}
#endif

/** Debug a tree.
 *
 * Check the structure of the tree.
 */
static void cl_tree_debug(struct cl_tree *tree) {
#ifndef NDEBUG
	if(cl_tree_debug_sub(tree, tree->root) == 0) {
		cl_tree_print_sub(tree, tree->root, 0);
		assert(false);
	}
  #if 0
	else {
		fprintf(stderr, "\n\n====\n\n");
		cl_tree_print_sub(tree, tree->root, 0);
	}
  #endif
#endif
}

/** Create a tree node.
 *
 * @param tree The tree (set or map).
 * @param leaf Leaf node sentinel (or NULL to create sentinel).
 * @param key Key to associate with node.
 * @return Pointer to a new node.
 */
static struct cl_node *cl_tree_node_create(struct cl_tree *tree,
	struct cl_node *leaf, const void *key)
{
	struct cl_node *n = cl_pool_alloc(tree->pool);
	n->left = leaf ? leaf : n;	/* sentinel links to self */
	n->right = leaf ? leaf : n;
	n->key = key;
	/* red unless this is the "leaf" sentinel */
	return leaf ? cl_node_red(n) : n;
}

/** Create a tree node mapping.
 *
 * @param tree The tree (map).
 * @param leaf Leaf node sentinel (or NULL to create sentinel).
 * @param key Key to associate with node.
 * @param value Value to map with key.
 * @return Pointer to a new node mapping.
 */
static struct cl_node_mapping *cl_tree_node_mapping_create(struct cl_tree *tree,
	struct cl_node *leaf, const void *key, const void *value)
{
	struct cl_node *m = cl_tree_node_create(tree, leaf, key);
	assert(tree->is_map);
	cl_node_set_value(m, value);
	return (struct cl_node_mapping *)m;
}

/** Create a tree.
 *
 * @param fn_compare Function to compare two keys for ordering.
 * @param sz Size of each node.
 * @return Newly created tree.
 */
static struct cl_tree *cl_tree_create(cl_compare_cb *fn_compare, size_t sz) {
	struct cl_tree *tree = malloc(sizeof(struct cl_tree));
	assert(tree);
	assert(fn_compare);
	tree->fn_compare = fn_compare;
	tree->pool = cl_pool_create(sz);
	tree->leaf = cl_tree_node_create(tree, NULL, NULL);
	tree->root = tree->leaf;
	tree->match = NULL;
	tree->n_entries = 0;
	tree->is_map = false;
	return tree;
}

/** Create a tree set.
 *
 * @param fn_compare Function to compare two keys for ordering.
 * @return Newly created tree set.
 */
struct cl_tree *cl_tree_create_set(cl_compare_cb *fn_compare) {
	return cl_tree_create(fn_compare, sizeof(struct cl_node));
}

/** Create a tree map.
 *
 * @param fn_compare Function to compare two keys for ordering.
 * @return Newly created tree map.
 */
struct cl_tree *cl_tree_create_map(cl_compare_cb *fn_compare) {
	struct cl_tree *tree = cl_tree_create(fn_compare,
		sizeof(struct cl_node_mapping));
	tree->is_map = true;
	return tree;
}

/** Destroy a tree.
 *
 * @param tree The tree (set or map).
 */
void cl_tree_destroy(struct cl_tree *tree) {
	assert(tree);
	cl_pool_destroy(tree->pool);
#ifndef NDEBUG
	tree->fn_compare = NULL;
	tree->pool = NULL;
	tree->leaf = NULL;
	tree->root = NULL;
	tree->match = NULL;
#endif
	free(tree);
}

/** Get the count of items.
 *
 * @param tree The tree (set or map).
 * @return Count of items currently in the tree.
 */
unsigned int cl_tree_count(struct cl_tree *tree) {
	return tree->n_entries;
}

/** Search for a node with a matching key.
 *
 * @param tree The tree (set or map).
 * @param key Key to search for.
 * @return Node with matching key, or leaf node if no match found.
 */
static struct cl_node *cl_tree_search(struct cl_tree *tree, const void *key) {
	struct cl_node *n = tree->root;
	while(!cl_tree_is_leaf(tree, n)) {
		switch(cl_tree_compare(tree, n, key)) {
		case CL_EQUAL:
			return n;
		case CL_LESS:
			n = cl_node_left(n);
			break;
		case CL_GREATER:
			n = cl_node_right(n);
			break;
		default:
			assert(false);
		}
	}
	return n;
}

/** Test if a tree contains a key.
 *
 * @param tree The tree (set or map).
 * @param key Key to test for.
 * @return True if tree contains the key, otherwise false.
 */
bool cl_tree_contains(struct cl_tree *tree, const void *key) {
	struct cl_node *n = cl_tree_search(tree, key);
	return !cl_tree_is_leaf(tree, n) &&
	       (cl_tree_compare(tree, n, key) == CL_EQUAL);
}

/** Get (peek) the first key in a sub-tree.
 *
 * @param tree The tree (set or map).
 * @param n Root node of sub-tree.
 * @return Lowest key in the sub-tree, or NULL if sub-tree is empty.
 */
static const void *cl_tree_peek_sub(struct cl_tree *tree, struct cl_node *n) {
	while(!cl_tree_is_leaf(tree, cl_node_left(n)))
		n = cl_node_left(n);
	return cl_node_key(n);
}

/** Get (peek) the first key in a tree.
 *
 * @param tree The tree (set or map).
 * @return Lowest key in the tree, or NULL if tree is empty.
 */
const void *cl_tree_peek(struct cl_tree *tree) {
	return cl_tree_peek_sub(tree, tree->root);
}

/** Get a matching key from a tree.
 *
 * @param tree The tree (set or map).
 * @param key Key to search for.
 * @return Matching key from the tree, or NULL if key is not in tree.
 */
const void *cl_tree_get_key(struct cl_tree *tree, const void *key) {
	struct cl_node *n = cl_tree_search(tree, key);
	return cl_node_key(n);
}

/** Get a value from a tree map.
 *
 * @param tree The tree (map).
 * @param key Key to search for.
 * @return Mapped value from the tree, or NULL if key is not in tree.
 */
const void *cl_tree_get(struct cl_tree *tree, const void *key) {
	struct cl_node *n = cl_tree_search(tree, key);
	assert(tree->is_map);
	return cl_tree_is_leaf(tree, n) ? NULL : cl_node_value(n);
}

/** Insert a node into a subtree.
 *
 * @param tree The tree (set or map).
 * @param r Root node of subtree to insert node into.
 * @param n New node to insert.
 * @return New subtree root node.
 */
static struct cl_node *cl_tree_insert_sub(struct cl_tree *tree,
	struct cl_node *r, struct cl_node *n)
{
	if(cl_tree_is_leaf(tree, r))
		return n;
	switch(cl_tree_compare(tree, r, cl_node_key(n))) {
	case CL_LESS:
		cl_node_set_left(r, cl_tree_insert_sub(tree,cl_node_left(r),n));
		break;
	case CL_EQUAL:
		tree->match = cl_node_black(r);
		return r;
	case CL_GREATER:
		cl_node_set_right(r, cl_tree_insert_sub(tree, cl_node_right(r),
			n));
		break;
	}
	return cl_node_lean_left(r);
}

/** Insert a node into a tree.
 *
 * @param tree The tree (set or map).
 * @param n Node to insert.
 * @return Existing node with matching key, or NULL if node was inserted.
 */
static struct cl_node *cl_tree_insert(struct cl_tree *tree, struct cl_node *n) {
	tree->match = NULL;
	tree->root = cl_node_black(cl_tree_insert_sub(tree, tree->root, n));
	return tree->match;
}

/** Add a key into a tree set.
 *
 * @param tree The tree (set).
 * @param key Key to add.
 * @return Matching replaced key if it existed, or NULL otherwise.
 */
const void *cl_tree_add(struct cl_tree *tree, const void *key) {
	struct cl_node *n = cl_tree_node_create(tree, tree->leaf, key);
	struct cl_node *m = cl_tree_insert(tree, n);
	assert(!tree->is_map);
	/* Did the key already exist in the tree? */
	if(m) {
		const void *mkey = cl_node_key(m);
		cl_node_set_key(m, key);
		cl_pool_release(tree->pool, cl_node_black(n));
		cl_tree_debug(tree);
		return mkey;
	} else {
		tree->n_entries++;
		cl_tree_debug(tree);
		return NULL;
	}
}

/** Put a key/value pair into a tree map.
 *
 * @param tree The tree (map).
 * @param key Key to put.
 * @param value Value to put.
 * @return Value previously mapped with key if it existed, or NULL otherwise.
 */
const void *cl_tree_put(struct cl_tree *tree, const void *key,
	const void *value)
{
	struct cl_node_mapping *n = cl_tree_node_mapping_create(tree,
		tree->leaf, key, value);
	struct cl_node *m = cl_tree_insert(tree, &n->node);
	assert(tree->is_map);
	/* Did the key already exist in the tree? */
	if(m) {
		const void *mvalue = cl_node_value(m);
		cl_node_set_key(m, key);
		cl_node_set_value(m, value);
		cl_pool_release(tree->pool, cl_node_black(&n->node));
		cl_tree_debug(tree);
		return mvalue;
	} else {
		tree->n_entries++;
		cl_tree_debug(tree);
		return NULL;
	}
}

/** Remove the lowest sub-node.
 *
 * @param tree The tree (set or map).
 * @param n Root of sub-tree.
 * @return New root of sub-tree.
 */
static struct cl_node *cl_tree_pop_sub(struct cl_tree *tree, struct cl_node *n){
	if(cl_tree_is_leaf(tree, cl_node_left(n))) {
		tree->match = cl_node_black(n);
		return tree->leaf;
	}
	if(!cl_node_is_red(cl_node_left(n)) &&
	   !cl_node_is_red(cl_node_left(cl_node_left(n))))
		n = cl_node_move_red_left(n);
	cl_node_set_left(n, cl_tree_pop_sub(tree, cl_node_left(n)));
	return cl_node_lean_left(n);
}

/** Remove an internal node.
 *
 * @param tree The tree (set or map).
 * @param n Node to be removed.
 * @return New root of sub-tree.
 */
static struct cl_node *cl_tree_remove_internal(struct cl_tree *tree,
	struct cl_node *n)
{
	struct cl_node *r = cl_node_right(n);
	struct cl_node *nr = cl_tree_pop_sub(tree, r);
	struct cl_node *mn = tree->match;
	assert(mn && !cl_tree_is_leaf(tree, mn));
	const void *key = cl_node_key(mn);
	cl_node_set_key(mn, cl_node_key(n));
	cl_node_set_key(n, key);
	if(tree->is_map) {
		const void *val = cl_node_value(mn);
		cl_node_set_value(mn, cl_node_value(n));
		cl_node_set_value(n, val);
	}
	cl_node_set_right(n, nr);
	return n;
}

/** Remove a node from a sub-tree.
 *
 * @param tree The tree (set or map).
 * @param n Root node of sub-tree.
 * @param key Key of node to remove.
 * @return New root of sub-tree.
 */
static struct cl_node *cl_tree_remove_sub(struct cl_tree *tree,
	struct cl_node *n, const void *key)
{
	if(cl_tree_is_leaf(tree, n))
		return n;
	if(cl_tree_compare(tree, n, key) == CL_LESS) {
		if(!cl_node_is_red(cl_node_left(n)) &&
		   !cl_node_is_red(cl_node_left(cl_node_left(n))))
			n = cl_node_move_red_left(n);
		cl_node_set_left(n, cl_tree_remove_sub(tree, cl_node_left(n),
			key));
	} else {
		if(cl_node_is_red(cl_node_left(n)))
			n = cl_node_rotate_right(n);
		if(cl_tree_compare(tree, n, key) == CL_EQUAL &&
		   cl_tree_is_leaf(tree, cl_node_right(n)))
		{
			tree->match = cl_node_black(n);
			return tree->leaf;
		}
		if(!cl_node_is_red(cl_node_right(n)) &&
		   !cl_node_is_red(cl_node_left(cl_node_right(n))))
			n = cl_node_move_red_right(n);
		if(cl_tree_compare(tree, n, key) == CL_EQUAL)
			n = cl_tree_remove_internal(tree, n);
		else {
			cl_node_set_right(n, cl_tree_remove_sub(tree,
				cl_node_right(n), key));
		}
	}
	return cl_node_lean_left(n);
}

/** Remove a node from a tree.
 *
 * @param tree The tree (set or map).
 * @param key Key to remove.
 * @return Matching node if it existed, or NULL otherwise.
 */
static struct cl_node *cl_tree_remove_node(struct cl_tree *tree,
	const void *key)
{
	tree->match = NULL;
	tree->root = cl_node_black(cl_tree_remove_sub(tree, tree->root, key));
	return tree->match;
}

/** Remove a key from a tree.
 *
 * @param tree The tree (set or map).
 * @param key Key to remove.
 * @return Previous key if it existed, or NULL otherwise.
 */
const void *cl_tree_remove_key(struct cl_tree *tree, const void *key) {
	struct cl_node *m = cl_tree_remove_node(tree, key);
	/* Did the key exist in the tree? */
	if(m) {
		const void *mkey = cl_node_key(m);
		cl_pool_release(tree->pool, m);
		tree->n_entries--;
		cl_tree_debug(tree);
		return mkey;
	} else {
		cl_tree_debug(tree);
		return NULL;
	}
}

/** Remove a mapping from a tree map.
 *
 * @param tree The tree (map).
 * @param key Key to remove.
 * @return Previous value mapped to key if it existed, or NULL otherwise.
 */
const void *cl_tree_remove(struct cl_tree *tree, const void *key) {
	struct cl_node *n = cl_tree_remove_node(tree, key);
	assert(tree->is_map);
	/* Did the key exist in the tree? */
	if(n) {
		const void *mvalue = cl_node_value(n);
		cl_pool_release(tree->pool, n);
		tree->n_entries--;
		cl_tree_debug(tree);
		return mvalue;
	} else {
		cl_tree_debug(tree);
		return NULL;
	}
}

/** Clear a tree.
 *
 * Remove all items from a tree.
 *
 * @param tree The tree (set or map).
 */
void cl_tree_clear(struct cl_tree *tree) {
	cl_pool_clear(tree->pool);
	tree->leaf = cl_tree_node_create(tree, NULL, NULL);
	tree->root = tree->leaf;
	tree->n_entries = 0;
	cl_tree_debug(tree);
}

/** Create a tree iterator.
 *
 * @param tree The tree (set or map).
 * @return Iterator for tree keys.
 */
struct cl_tree_iterator *cl_tree_iterator_create(struct cl_tree *tree) {
	struct cl_tree_iterator *it = cl_pool_alloc(tree->pool);
	it->tree = tree;
	it->branch = NULL;
	return it;
}

/** Destroy a tree iterator.
 *
 * @param it The tree iterator.
 */
void cl_tree_iterator_destroy(struct cl_tree_iterator *it) {
	assert(it);
	struct cl_tree *tree = it->tree;
	struct cl_tree_branch *br = it->branch;
	while(br) {
		struct cl_tree_branch *next = br->branch;
		cl_pool_release(tree->pool, br);
		br = next;
	}
#ifndef NDEBUG
	it->tree = NULL;
	it->branch = NULL;
#endif
	cl_pool_release(tree->pool, it);
}

/** Descend a tree iterator.  Take left branch of each node until we reach a
 * leaf.  Put each intervening node on the iterator stack.  Upon return, the
 * iterator node points to the value at the leaf node found.
 */
static void cl_tree_iterator_descend(struct cl_tree_iterator *it,
	struct cl_node *n)
{
	struct cl_tree *tree = it->tree;
	struct cl_tree_branch *next = it->branch;
	while(!cl_tree_is_leaf(tree, n)) {
		struct cl_tree_branch *br = cl_pool_alloc(tree->pool);
		br->branch = next;
		br->node = n;
		next = br;
		n = cl_node_left(n);
	}
	it->branch = next;
}

/** Ascend a tree iterator.  Go up one branch and descend to the right.
 */
static void cl_tree_iterator_ascend(struct cl_tree_iterator *it) {
	struct cl_tree *tree = it->tree;
	struct cl_tree_branch *br = it->branch;
	assert(br);
	struct cl_node *n = cl_node_right(br->node);
	struct cl_tree_branch *next = br->branch;
	cl_pool_release(tree->pool, br);
	it->branch = next;
	cl_tree_iterator_descend(it, n);
}

/** Get next key from a tree iterator.
 *
 * @param it The iterator.
 * @return Next key, or NULL if no more keys.
 */
const void *cl_tree_iterator_next(struct cl_tree_iterator *it) {
	struct cl_tree *tree = it->tree;
	struct cl_tree_branch *br = it->branch;
	if(br)
		cl_tree_iterator_ascend(it);
	else
		cl_tree_iterator_descend(it, tree->root);
	br = it->branch;
	return br ? cl_node_key(br->node) : NULL;
}

/** Get the value associated with most recent key from a tree iterator.
 *
 * @param it The iterator.
 * @return Value associated with most recent key returned, or NULL.
 */
const void *cl_tree_iterator_value(struct cl_tree_iterator *it) {
	struct cl_tree *tree = it->tree;
	struct cl_tree_branch *br = it->branch;
	struct cl_node *n = br ? br->node : NULL;
	assert(tree->is_map);
	return cl_tree_is_leaf(tree, n) ? NULL : cl_node_value(n);
}
