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
#include <llist.h>

/*
 * Our per-open-client data structure
 */
struct file {
	struct llist		/* Where we're queued */
		*f_entry;
	long f_sender;		/* Where to reply to after dequeueing */
};

extern void tick_open(struct msg *, struct file *),
	tick_read(struct msg *, struct file *),
	tick_close(struct file *),
	tick_abort(struct msg *, struct file *),
	rw_init(void),
	empty_queue(void);

#endif /* TICK_H */
