#ifndef _TMPFS_H
#define _TMPFS_H
/*
 * tmpfs.h
 *	Data structures in temp filesystem
 *
 * TMPFS is a VM-based filesystem, which thus does not survive
 * reboots.  It is flat, and stores file contents into a chain
 * of malloc()-allocated blocks.
 *
 * Directory entries are found using a symbol table lookup.  Each
 * open file contains a hash which maps block number into the
 * block's virtual address.
 */
#include <sys/types.h>
#include <sys/fs.h>
#include <sys/perm.h>

#define BLOCKSIZE 8192		/* Size of blocks in filesystem */

/*
 * Structure of an open file in the filesystem
 */
struct openfile {
	char *o_name;		/* Name of file */
	struct hash *o_blocks;	/* Map to blocks (0-based) */
	ulong o_len;		/* Length in bytes */
	struct llist *o_entry;	/* Link into list of names in filesystem */
	struct prot o_prot;	/* Protection of file */
	uint o_refs;		/* # references */
};

/*
 * Our per-open-file data structure
 */
struct file {
	struct openfile	/* Current file open */
		*f_file;
	ulong f_pos;	/* Current file offset */
	struct perm	/* Things we're allowed to do */
		f_perms[PROCPERMS];
	int f_nperm;
	int f_perm;	/*  ...for the current f_file */
};

#endif /* _TMPFS_H */
