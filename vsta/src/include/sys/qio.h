#ifndef _QIO_H
#define _QIO_H
/*
 * qio.h
 *	Data structures and routine definitions for kernel asynch I/O
 */
#include <sys/types.h>

/*
 * One of these describes a queued I/O
 */
struct qio {
	struct portref *q_port;	/* Port for operation */
	int q_op;		/* FS_READ/FS_WRITE */
	struct pset *q_pset;	/* Page set page within */
	struct perpage *q_pp;	/* Per-page info on page */
	off_t q_off;		/* Byte offset in q_port */
	uint q_cnt;		/* Byte count of operation */
	voidfun q_iodone;	/* Function to call on I/O done */
	struct qio *q_next;	/* For linking qios */
};

/*
 * Queue an I/O to the I/O daemon
 */
extern void qio(struct qio *);

/*
 * Allocate a qio structure
 */
extern struct qio *alloc_qio(void);

#endif /* _QIO_H */
