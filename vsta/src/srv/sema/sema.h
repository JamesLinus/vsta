#ifndef SEMA_H
#define SEMA_H
/*
 * sema.h
 *	Data structures in semaphore filesystem
 *
 * SEMA is a memory-based filesystem, which thus does not survive
 * reboots.  Instead of files with contents, each file is instead
 * a semaphore.  Reading the file queues one as a reader; writing
 * queues one as a writer.  Multiple readers can enter the semaphore
 * at once; only one writer may enter at a time.
 *
 * Filenames are integers; the name "clone" will pick an unused integer.
 */
#include <sys/fs.h>
#include <sys/perm.h>
#include <llist.h>

/*
 * Structure of an open file in the filesystem
 */
struct openfile {
	struct llist o_queue;	/* Queue of would-be accesses */
	struct prot o_prot;	/* Protection of file */
	uint o_refs;		/* # references */
	uint o_owner;		/* Owner UID */
	uint o_nwrite;		/* # writers waiting for semaphore */
	uint o_holder;		/* # refs in through sema now */
	uint o_iname;		/* Integer name of this file */
	uint o_writing;		/* Held for writing now */
};

/*
 * Our per-open-file data structure
 */
struct file {
	struct openfile		/* Current file open */
		*f_file;
	struct perm		/* Things we're allowed to do */
		f_perms[PROCPERMS];
	uint f_pos;		/* Position (only for dir reads) */
	struct llist		/* Where we're queued */
		*f_entry;
	long f_sender;		/* Where to reply to after dequeueing */
	uchar f_nperm;
	uchar f_perm;		/*  ...for the current f_file */
	uchar f_writer;		/* We're waiting to write */
	uchar f_hold;		/* We're in through the sema */
};

extern void sema_open(struct msg *, struct file *),
	sema_read(struct msg *, struct file *),
	sema_write(struct msg *, struct file *),
	sema_stat(struct msg *, struct file *),
	sema_close(struct file *),
	sema_wstat(struct msg *, struct file *),
	sema_abort(struct msg *, struct file *);
extern int sema_seek(struct msg *, struct file *);

extern struct hash *files;
extern uint nfiles;

#endif /* SEMA_H */
