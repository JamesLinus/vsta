#ifndef _PIPE_H
#define _PIPE_H
/*
 * tmpfs.h
 *	Data structures in temp filesystem
 *
 * PIPE is a VM-based FIFO buffer manager.  Its usual use is to open
 * /pipe/# (or wherever you mount the pipe manager) and receive a
 * new pipe.  Alternatively, /pipe/<number> will access an existing
 * pipe.  A pipe's "number" is its inum, from rstat().
 *
 * Internally, the pipe's "number" is simply its storage address.
 */
#include <sys/types.h>
#include <sys/fs.h>
#include <sys/perm.h>
#include <llist.h>

/*
 * Structure of a pipe
 */
struct pipe {
	struct llist *p_entry;	/* Link into list of pipes */
	struct prot p_prot;	/* Protection of pipe */
	int p_refs;		/* # references */
	uint p_owner;		/* Owner UID */
	struct llist p_readers,	/* List of read requests pending */
		p_writers;	/*  ...writers */
	int p_nwrite;		/* # clients open for writing */
};

/*
 * Our per-open-file data structure
 */
struct file {
	struct pipe	/* Current pipe open */
		*f_file;
	struct perm	/* Things we're allowed to do */
		f_perms[PROCPERMS];
	uint f_nperm;
	uint f_perm;	/*  ...for the current f_file */
	struct msg	/* For writes, segments of data */
		f_msg;	/*  for reads, just reply addr & count */
	struct llist	/* When request active, queue we're in */
		*f_q;
	uint f_pos;	/* Only for directory reads */
};

#endif /* _PIPE_H */
