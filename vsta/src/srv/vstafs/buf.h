#ifndef BUF_H
#define BUF_H
/*
 * buf.h
 *	Definitions for the block buffer code
 */
#include <sys/types.h>
#include <vstafs/vstafs.h>
#include <lib/llist.h>

/*
 * Description of a particular buffer of data
 */
struct buf {
	struct llist *b_list;	/* Linked under bufhead */
	void *b_data;		/* Actual data */
	daddr_t b_start;	/* Starting sector # */
	uint b_nsec;		/*  ...# SECSZ units contained */
	uint b_flags;		/* Flags */
	uint b_locks;		/* Count of locks on buf */
};

/*
 * Bits in b_flags
 */
#define B_DIRTY 2		/* Buffer needs to be written to disk */

/*
 * Routines for accessing a buf
 */
extern struct buf *find_buf(daddr_t, uint);
extern void *index_buf(struct buf *, uint, uint);
extern void init_buf(void);
extern void dirty_buf(struct buf *);
extern void lock_buf(struct buf *), unlock_buf(struct buf *);
extern void sync_buf(struct buf *);
extern int extend_buf(daddr_t, uint, int);
extern void inval_buf(daddr_t, uint);
extern void sync(void);

#endif /* BUF_H */
