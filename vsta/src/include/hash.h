#ifndef _HASH_H
#define _HASH_H
/*
 * hash.h
 *	Data structures for a hashed lookup object
 */
#include <sys/types.h>

/*
 * This is the root of the hashed object.  You shouldn't monkey
 * with it directly.
 */
struct hash {
	int h_hashsize;		/* Width of h_hash array */
	struct hash_node	/* Chains under each hash value */
		*h_hash[1];
};

/*
 * Hash collision chains.  An internal data structure.
 */
struct hash_node {
	struct hash_node *h_next;	/* Next on hash chain */
	long h_key;			/* Key for this node */
	void *h_data;			/*  ...corresponding value */
};

/*
 * Smoke and mirrors to avoid name space pollution
 */
#define hash_alloc __hash_alloc
#define hash_insert __hash_insert
#define hash_delete __hash_delete
#define hash_lookup __hash_lookup
#define hash_dealloc __hash_dealloc
#define hash_foreach __hash_foreach
#define hash_size __hash_size

/*
 * Hash routines
 */
struct hash *hash_alloc(int);
int hash_insert(struct hash *, long, void *);
int hash_delete(struct hash *, long);
void *hash_lookup(struct hash *, long);
void hash_dealloc(struct hash *);
void hash_foreach(struct hash *, intfun, void *);
uint hash_size(struct hash *);

#endif /* _HASH_H */
