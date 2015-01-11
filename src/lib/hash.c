/*
 * hash.c
 *	A hashed lookup mechanism
 */
#include <std.h>
#include <hash.h>

/*
 * hashval()
 *	Convert key into hash value
 */
static inline uint
hashidx(ulong key, uint mask)
{
	return((key ^ (key >> 2) ^ (key >> 6)) & mask);
}

/*
 * hash_alloc()
 *	Allocate a hash data structure of the given hash size
 *
 * For speed we always round the hash table size to the nearest power
 * of 2 above the requested size.
 */
struct hash *
hash_alloc(int hashsize)
{
	struct hash *h;
	int i = 3, hashlim = 8;

	/*
	 * Adjust hash size to the next power of 2
	 */
	while(hashsize > hashlim) {
		i++;
		hashlim <<= 1;
	}
	h = malloc(sizeof(struct hash) + hashlim * sizeof(struct hash *));
	if (h) {
		h->h_hashsize = hashlim;
		h->h_hashmask = hashlim - 1;
		bzero(&h->h_hash, hashlim * sizeof(struct hash *));
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
	uint idx;

	if (!h) {
		return(1);
	}
	idx = hashidx(key, h->h_hashmask);
	hn = malloc(sizeof(struct hash_node));
	if (!hn) {
		return(1);
	}
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
	uint idx;

	if (!h) {
		return(1);
	}

	/*
	 * Walk hash chain list.  Walk both the pointer, as
	 * well as a pointer to the previous pointer.  When
	 * we find the node, patch out the current node and
	 * free it.
	 */
	idx = hashidx(key, h->h_hashmask);
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
	uint x;
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
	uint idx;

	if (!h) {
		return(0);
	}
	idx = hashidx(key, h->h_hashmask);
	for (hn = h->h_hash[idx]; hn; hn = hn->h_next) {
		if (hn->h_key == key) {
			return(hn->h_data);
		}
	}
	return(0);
}

/*
 * hash_size()
 *	Tell how many elements are stored in the hash
 */
uint
hash_size(struct hash *h)
{
	uint x, cnt = 0;
	struct hash_node *hn;

	for (x = 0; x < h->h_hashsize; ++x) {
		for (hn = h->h_hash[x]; hn; hn = hn->h_next) {
			cnt += 1;
		}
	}
	return(cnt);
}

/*
 * hash_foreach()
 *	Enumerate each entry in the hash, invoking a function
 */
void
hash_foreach(struct hash *h, intfun f, void *arg)
{
	uint x;
	struct hash_node *hn;

	for (x = 0; x < h->h_hashsize; ++x) {
		for (hn = h->h_hash[x]; hn; hn = hn->h_next) {
			if ((*f)(hn->h_key, hn->h_data, arg)) {
				return;
			}
		}
	}
}
