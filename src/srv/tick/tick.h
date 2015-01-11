#ifndef TICK_H
#define TICK_H
/*
 * tick.h
 *	Data structures in ticktock filesystem
 *
 * ticktock is a filesystem which support only one operation; a 4-byte
 * FS_READ.  Each posted read will be blocked until the next second, at
 * which point the time() value will be the returned result.
 *
 * This filesystem was originally created as a test bed for the select()
 * system call implementation.
 */
#include <sys/fs.h>
#include <selfs.h>
#include <llist.h>

/*
 * Our per-open-client data structure
 */
struct file {
	struct llist
		*f_entry,	/* Where we're queued */
		*f_sentry;	/* If we're a select() client */
	long f_sender;		/* Where to reply to after dequeueing */
	struct selclient
		f_selfs;	/* State for select() support */
};

extern void tick_open(struct msg *, struct file *),
	tick_read(struct msg *, struct file *),
	tick_close(struct file *),
	tick_abort(struct msg *, struct file *),
	tick_wstat(struct msg *, struct file *),
	rw_init(void),
	empty_queue(void);

/*
 * List of select() clients
 */
extern struct llist selectors;

#endif /* TICK_H */
