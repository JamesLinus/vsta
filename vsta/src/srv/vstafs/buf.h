#ifndef BUF_H
#define BUF_H
/*
 * buf.h
 *	Definitions for the block buffer code
 */
#include <sys/types.h>
#include "vstafs.h"
#include <llist.h>

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
#define B_SEC0 0x1		/* 1st sector valid  */
#define B_SECS 0x2		/*  ...rest of sectors valid too */
#define B_DIRTY 0x4		/* Some sector in buffer is dirty */

/*
 * Routines for accessing a buf
 */
extern struct buf *find_buf(daddr_t, uint);
extern void *index_buf(struct buf *, uint, uint);
extern void init_buf(void);
extern void dirty_buf(struct buf *);
extern void lock_buf(struct buf *), unlock_buf(struct buf *);
extern void sync_buf(struct buf *);
extern int resize_buf(daddr_t, uint, int);
extern void inval_buf(daddr_t, uint);
extern void sync(void);

#endif /* BUF_H */
