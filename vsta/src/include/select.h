/*
 * select.h
 *	Definitions for select() operations
 */
#ifndef _SELECT_H
#define _SELECT_H
#include <sys/types.h>
#include <time.h>

/*
 * By default, size of a file descriptor set.  May be overriden.
 * Must be a power of two.
 */
#ifndef FD_SETSIZE
#define FD_SETSIZE (64)
#endif

typedef struct {uint _fds[FD_SETSIZE / sizeof(uint)]; } fd_set;

extern int select(uint, fd_set *, fd_set *, fd_set *, struct timeval *);

#define FD_ISSET(idx, p) \
	((p)->_fds[(idx) / sizeof(uint)] & \
		(1 << ((idx) & (sizeof(uint) - 1))))

#define FD_SET(idx, p) \
	((p)->_fds[(idx) / sizeof(uint)] |= \
		(1 << ((idx) & (sizeof(uint) - 1))))

#define FD_CLR(idx, p) \
	((p)->_fds[(idx) / sizeof(uint)] &= \
		~(1 << ((idx) & (sizeof(uint) - 1))))

#define FD_ZERO(p) bzero((p), sizeof(fd_set))

 
/*
 * Aliases for some access bits we use for our own purposes
 */
#define ACC_EXCEP ACC_EXEC
#define ACC_UNSUPP ACC_SYM	/* Internal */

#ifdef _SELFS_INTERNAL

/*
 * Format of messages used to control selfs
 */

/*
 * This is the format of data returned from an FS_READ to a client.  It
 * indicates which sources satisfied the select request.  Zero (for timeout)
 * or more of these will be returned as the FS_READ data.
 */
struct select_complete {
	uint sc_index;		/* Index of client connection */
	uint sc_mask;		/* Mask of satisfied events */
	ulong sc_iocount;	/* # message exchanges seen */
};

/*
 * A server will write one or more of these to selfs to indicate
 * events which should awake a client.
 */
struct select_event {
	long se_clid;		/* Client's connection ID */
	ulong se_key;		/* Magic # to inhibit bogus clid's */
	uint se_index;		/* Index of client's server connection */
	uint se_mask;		/* Mask of events which have occurred */
	ulong se_iocount;	/* Indicated sc_iocount for this event */
};

/*
 * While not a struct, the format of the data which a client writes to a
 * server in an FS_WSTAT message is:
 *  select=<mask>,<host>,<se_clid>,<se_index>,<key>
 * or:
 *  unselect=<mask>,<host>,<se_clid>,<se_index>,<key>
 * The server's iocount is expected to count up from 0 starting from this
 * point.
 */

#endif /* _SELFS_INTERNAL */

#endif /* _SELECT_H */
