/*
 * node.c
 *	Handling of open file nodes
 */
#include <vstafs/vstafs.h>
#include <vstafs/buf.h>
#include <sys/assert.h>
#include <lib/hash.h>
#include <std.h>

static struct hash *node_hash;

/*
 * init_node()
 *	Initialize node system
 */
void
init_node(void)
{
	node_hash = hash_alloc(NCACHE);
	ASSERT(node_hash, "init_node: out of memory");
}

/*
 * ref_node()
 *	Add a reference to a node
 */
void
ref_node(struct openfile *o)
{
	o->o_refs += 1;
	ASSERT_DEBUG(o->o_refs > 0, "ref_node: overflow");
}

/*
 * deref_node()
 *	Remove a reference, free on last reference
 */
void
deref_node(struct openfile *o)
{
	ASSERT_DEBUG(o->o_refs > 0, "deref_node: zero");
	o->o_refs -= 1;
	if (o->o_refs == 0) {
		ASSERT(hash_delete(node_hash, o->o_file) == 0,
			"deref_node: hash mismatch");
		free(o);
	}
}

/*
 * alloc_node()
 *	Allocate a new node
 *
 * Somewhat complicated because the file's storage structure is stored
 * at the front of the file.  We read in the first sector, then extend
 * the buffered block out to the indicated size.
 */
static struct openfile *
alloc_node(daddr_t d)
{
	struct buf *b;
	struct fs_file *fs;
	ulong len;
	struct openfile *o;

	/*
	 * Get buf, address first sector as an fs_file
	 */
	b = find_buf(d, 1);
	fs = index_buf(b, 0, 1);
	ASSERT(fs->fs_nblk > 0, "alloc_node: zero");
	ASSERT(fs->fs_blks[0].a_start == d, "alloc_node: mismatch");

	/*
	 * Extend the buf if necessary
	 */
	len = fs->fs_blks[0].a_len;
	if (len > 1) {
		if (len > EXTSIZ) {
			len = EXTSIZ;
		}
		if (resize_buf(d, (uint)len, 1)) {
			return(0);
		}
	}

	/*
	 * Create a new openfile and return it
	 */
	o = malloc(sizeof(struct openfile));
	if (o == 0) {
		return(0);
	}
	o->o_file = d;
	o->o_hiwrite = o->o_len = len;
	o->o_refs = 1;
	return(o);
}

/*
 * get_node()
 *	Return node, either from hash or created from scratch
 */
struct openfile *
get_node(daddr_t d)
{
	struct openfile *o;

	/*
	 * From hash?
	 */
	o = hash_lookup(node_hash, d);
	if (o) {
		/*
		 * Yes--add a reference, and return
		 */
		ref_node(o);
		return(o);
	}

	/*
	 * Get a new one, and return it
	 */
	o = alloc_node(d);
	if (hash_insert(node_hash, d, o)) {
		deref_node(o);
		o = 0;
	}
	return(o);
}
