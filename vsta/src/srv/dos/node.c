/*
 * node.c
 *	Handling for dir/file nodes
 */
#include <dos/dos.h>
#include <sys/assert.h>

/*
 * ref_node()
 *	Add a reference to a node
 */
void
ref_node(struct node *n)
{
	n->n_refs += 1;
}

/*
 * deref_node()
 *	Remove a reference from a node
 *
 * Flush on last close of a dirty file
 */
void
deref_node(struct node *n)
{
	ASSERT(n->n_refs > 0, "deref_node: no refs");

	/*
	 * Remove ref, do nothing if still open
	 */
	if ((n->n_refs -= 1) > 0) {
		return;
	}

	/*
	 * Flush out dirty blocks on last close
	 */
	if (n->n_flags & N_DIRTY) {
		bsync();	/* XXX Should only sync this file & dir */
	}

	/*
	 * Free FAT cache
	 */
	free_clust(n->n_clust);

	/*
	 * If file, remove ref from dir we're within
	 */
	if (n->n_type == T_FILE) {
		hash_delete(n->n_dir->n_files, n->n_slot);
		deref_node(n->n_dir);
	}

	/*
	 * Free our memory
	 */
	free(n);
}
