/*
 * selfs.h
 *	Standard stuff for a server to use to support select()
 */
#ifndef _SELFS_H
#define _SELFS_H
#include <sys/msg.h>

/*
 * Per-client select state needed
 */
struct selclient {
	void *sc_selfs;		/* Connection to its select() server */
				/*  (opaque state; may be shared) */
	uint sc_mask;		/* Mask of events; non-zero when */
				/*  select() is used by this client */
	uint sc_nosupp;		/*  ...unsupported bits */
	long sc_clid;		/* Client's ID on that server */
	ulong sc_key;		/* Key */
	int sc_fd;		/* File descriptor value */
	ulong sc_iocount;	/* Count of I/O's */
	int sc_needsel;		/* Flag that a select_event is needed */
};

/*
 * sc_init()
 *	Initialize struct fields
 */
extern void sc_init(struct selclient *scp);

/*
 * sc_wstat()
 *	Handle an FS_WSTAT message
 *
 * Returns 0 if the message was handled, 1 otherwise.  "select"
 * is decoded to enable select() support, "unselect" turns it off.
 */
extern int sc_wstat(struct msg *m,
	struct selclient *scp, char *field, char *val);

/*
 * sc_done()
 *	Indicate that the client has closed its connection
 */
extern void sc_done(struct selclient *scp);

/*
 * sc_event()
 *	Tell the client's select server that an event has occurred
 */
extern void sc_event(struct selclient *scp, uint mask);

#endif /* _SELFS_H */
