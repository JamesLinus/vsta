#ifndef SELFS_H
#define SELFS_H
/*
 * selfs.h
 *	Data structures in selfsphore filesystem
 *
 * selfs is a virtual filesystem supporting the select() "system call" (which,
 * of course, is nothing of the sort in VSTa).  Clients and servers connect
 * to this server (in addition to, as always, connecting to each other for
 * purposes of I/O).  When a client wishes to block waiting for some set of
 * events, it enumerates across each server, sending it a FS_WSTAT which
 * indicates the select server to use, along with parameters so that its
 * context can be indicated on that select server.
 *
 * A filesystem which does not support select will simply not recognize this
 * FS_WSTAT type, and will return an error.
 *
 * Filesystems which do will then connect to this select server (or reuse
 * an existing cached connection).  As indicated select events occur, a
 * normal FS_WRITE to the select server will indicate the availability
 * of data.  The select server then handles the other side of this, deciding
 * when to wake clients.
 *
 * On the client side, after the client has FS_WSTAT'ed each server (note that
 * it only has to do this once; the server's behavior continues until
 * explicitly un-requested), it posts an FS_READ to its select server.
 * The client then sleeps until the requested time interval has passed,
 * or until the select server sends back an indication of available
 * I/O.
 */
#include <sys/fs.h>
#include <llist.h>
#include <hash.h>

/*
 * Our per-open-file data structure
 */
struct file {
	struct openfile		/* Current file open */
		*f_file;
	struct llist		/* Events for us */
		f_events;
	ulong f_key;		/* Our key */
	uint f_mode;		/* Root/client/server flag */
	long f_sender;		/* Non-0 if client is blocked */
	uint f_size;		/*  ...# bytes permitted */
};

/*
 * Values for f_mode
 */
#define MODE_ROOT (1)		/* Just opened, haven't selected */
#define MODE_CLIENT (2)		/*  ...in fs/select:client */
#define MODE_SERVER (3)		/*  ...in fs/select:server */

/*
 * Items queued under f_events
 */
struct event {
	uint e_index;		/* Index of client's server connection */
	ulong e_iocount;	/* Indicated sc_iocount for this event */
	uint e_mask;		/* Events pending */
	struct llist *e_next;	/* Linked list of these */
};

/*
 * Server routines
 */
extern void selfs_open(struct msg *, struct file *),
	selfs_read(struct msg *, struct file *),
	selfs_write(struct msg *, struct file *),
	selfs_stat(struct msg *, struct file *),
	selfs_wstat(struct msg *, struct file *),
	selfs_abort(struct msg *, struct file *);

/*
 * Global data
 */
extern struct hash *filehash;

/*
 * Data shared with library side of implementation
 */
#define _SELFS_INTERNAL
#include <select.h>

#endif /* SELFS_H */
