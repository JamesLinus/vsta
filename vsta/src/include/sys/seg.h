#ifndef _SEG_H
#define _SEG_H
/*
 * seg.h
 *	Definitions for passing around segments of memory
 *
 * A segment is simply an opaque handle into a per-process set of buffers
 * which have been attached to the process due to message I/O.
 */
#include <sys/types.h>

typedef struct {
	void *s_buf;
	uint s_buflen;
} seg_t;	/* Handle for a range of memory */

/*
 * Functions for handling the stuff
 */

/*
 * seg_copyin(segments, nseg, dest, max_count)
 *	Copies one or more segments into contiguous memory
 *
 * Returns number of bytes actually copied, or -1 on error.
 */
int seg_copyin(seg_t *, uint, void *, uint);

/*
 * seg_copyout(segments, nseg, src, count)
 *	Copy memory out to one or more segments
 *
 * Returns number of bytes copied, or error.
 */
int seg_copyout(seg_t *, uint, void *, uint);

/*
 * seg_create(buf, buflen)
 *	Create a segment
 */
seg_t seg_create(void *, uint);

#ifdef KERNEL

#include <sys/pview.h>
#include <sys/param.h>

/*
 * This part is for the kernel's organization of segments
 */
struct seg {
	struct pview		/* View of pages in this segment */
		s_pview;
	uint s_off,		/* Offset/length within view for memory */
		s_len;
};

/*
 * This tabulates a port or portref's set of segments which are
 * currently mapped and should be torn down at the end of the
 * transaction.
 */
struct segref {
	struct seg
		*s_refs[MSGSEGS+2];
};

/*
 * Routines
 */
extern void free_seg(struct seg *);
extern struct seg *make_seg(struct vas *, void *, uint);
extern attach_seg(struct vas *, struct seg *);
extern void detach_seg(struct seg *);
extern struct seg *kern_mem(void *, uint);
/* copyoutsegs() defined in <sys/msg.h> */

#endif /* KERNEL */

#endif /* _SEG_H */
