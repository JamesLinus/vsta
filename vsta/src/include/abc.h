#ifndef ABC_H
#define ABC_H
/*
 * abc.h
 *	Definitions for the Asynchronous Buffer Cache
 */
#include <sys/types.h>

/*
 * Types unique to ABC
 */
typedef unsigned long daddr_t;

/*
 * Flags to find_buf()
 */
#define ABC_FILL (0x01)		/* Fill from disk? */
#define ABC_BG (0x02)		/* Fill in background? */

/*
 * Routines for accessing an ABC buf
 */
extern struct buf *find_buf(daddr_t, uint, int);
extern int resize_buf(daddr_t, uint, int);
extern void *index_buf(struct buf *, uint, uint),
	free_buf(struct buf *),
	init_buf(port_t, int),
	dirty_buf(struct buf *, void *),
	lock_buf(struct buf *),
	unlock_buf(struct buf *),
	sync_buf(struct buf *),
	inval_buf(daddr_t, uint),
	sync_bufs(void *);

#endif /* ABC_H */
