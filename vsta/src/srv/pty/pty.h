#ifndef PTY_H
#define PTY_H
/*
 * pty.h
 *	Data structures in pseudo-tty support
 *
 * PTY offers a matched set of TTY/PTY pairs.  The master opens the pty
 * variant, and the client the tty version.  Data written to the tty
 * is available in read()'s from the pty; similarly, data written to the
 * pty becomes "input" readable from the TTY device.  Only one process
 * can hold the pty open (further open()'es will fail) but any numer
 * of processes can open() and use the TTY.
 */
#include <sys/types.h>
#include <sys/fs.h>
#include <sys/perm.h>
#include <llist.h>

/*
 * Number of PTY's offered... they are named "pty[0..9a..z]"
 */
#define NPTY 8

/*
 * A pipeline of data, including handling of queued I/O
 * for both writers and readers.
 */
struct ioq {
	char *ioq_buf;		/* Queued data */
	uint ioq_nbuf;		/*  ...amount of data */
	struct llist
		ioq_write,	/* Queue waiting for room to write */
		ioq_read;	/*  ...waiting for data to read */
};

/*
 * Structure of a pty
 */
struct pty {
	struct prot p_prot;	/* Protection of tty */
	uint p_owner;		/* Owner UID */
	struct ioq p_ioqr,	/* Queue of data written to TTY */
		p_ioqw;		/*  ...read on TTY */
	uint p_nmaster,		/* Counts of masters/slaves */
		p_nslave;
	uint p_rows, p_cols;	/* Pseudo-geometry */
};

/*
 * Our per-open-file data structure
 */
struct file {
	struct pty	/* Current pty open */
		*f_file;
	uint f_master;	/* Hold of ptyX, otherwise just a ttyX user */
	struct perm	/* Things we're allowed to do */
		f_perms[PROCPERMS];
	uint f_nperm;
	uint f_perm;	/*  ...for the current f_file */
	struct llist	/* When request active, queue we're in */
		*f_q;
	struct msg	/* For writes, segments of data */
		f_msg;	/*  for reads, just reply addr & count */
	uint f_pos;	/* Only for directory reads */
};

/*
 * Global variables
 */
extern struct pty ptys[];
extern char *ptydir;
extern int ptydirlen;

/*
 * Global routines
 */
extern void pty_open(struct msg *, struct file *),
	pty_read(struct msg *, struct file *),
	pty_write(struct msg *, struct file *),
	pty_abort(struct msg *, struct file *),
	pty_stat(struct msg *, struct file *),
	pty_close(struct file *),
	pty_wstat(struct msg *, struct file *);
extern void ioq_init(struct ioq *), ioq_abort(struct ioq *),
	ioq_add_data(struct file *, struct ioq *, struct msg *),
	ioq_read_data(struct file *, struct ioq *, struct msg *);

#endif /* PTY_H */
