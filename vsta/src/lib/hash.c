/*
 * hash.c
 *	A hashed lookup mechanism
 */

extern void *malloc();

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
 * hash_alloc()
 *	Allocate a hash data structure of the given hash size
 */
struct hash *
hash_alloc(int hashsize)
{
	struct hash *h;

	h = malloc(sizeof(struct hash) + hashsize*sizeof(struct hash *));
	if (h) {
		h->h_hashsize = hashsize;
		bzero(&h->h_hash, hashsize*sizeof(struct hash *));
	}
	return(h);
}

/*
 * hash_insert()
 *	Insert a new key/value pair into the hash
 *
 * Returns 1 on error, 0 on success.
 */
hash_insert(struct hash *h, long key, void *val)
{
	struct hash_node *hn;
	int idx;

	if (!h) {
		return(1);
	}
	idx = key % h->h_hashsize;
	hn = malloc(sizeof(struct hash_node));
	if (!hn)
		return(1);
	hn->h_key = key;
	hn->h_data = val;
	hn->h_next = h->h_hash[idx];
	h->h_hash[idx] = hn;
	return(0);
}

/*
 * hash_delete()
 *	Remove node from hash
 *
 * Returns 1 if key not found, 0 if removed successfully.
 */
hash_delete(struct hash *h, long key)
{
	struct hash_node **hnp, *hn;
	int idx;

	if (!h) {
		return(1);
	}

	/*
	 * Walk hash chain list.  Walk both the pointer, as
	 * well as a pointer to the previous pointer.  When
	 * we find the node, patch out the current node and
	 * free it.
	 */
	idx = key % h->h_hashsize;
	hnp = &h->h_hash[idx];
	hn = *hnp;
	while (hn) {
		if (hn->h_key == key) {
			*hnp = hn->h_next;
			free(hn);
			return(0);
		}
		hnp = &hn->h_next;
		hn = *hnp;
	}
	return(1);
}

/*
 * hash_dealloc()
 *	Free up the entire hash structure
 */
void
hash_dealloc(struct hash *h)
{
	int x;
	struct hash_node *hn, *hnn;

	for (x = 0; x < h->h_hashsize; ++x) {
		for (hn = h->h_hash[x]; hn; hn = hnn) {
			hnn = hn->h_next;
			free(hn);
		}
	}
	free(h);
}

/*
 * hash_lookup()
 *	Look up a node based on its key
 */
void *
hash_lookup(struct hash *h, long key)
{
	struct hash_node *hn;

	if (!h) {
		return(0);
	}
	for (hn = h->h_hash[key % h->h_hashsize]; hn; hn = hn->h_next) {
		if (hn->h_key == key)
			return(hn->h_data);
	}
	return(0);
}
