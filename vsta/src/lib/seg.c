/*
 * seg.c
 *	Handling of user-level segments
 */
#include <sys/seg.h>

/*
 * seg_copyin(segments, nseg, dest, max_count)
 *	Copies one or more segments into contiguous memory
 *
 * Returns number of bytes actually copied, or -1 on error.
 */
seg_copyin(seg_t *seg, uint nseg, void *buf, uint buflen)
{
	int x;
	uint step, count = 0;

	for (x = 0; x < nseg; ++x,++seg) {
		if (count == buflen) {
			break;
		}
		step = seg->s_buflen;
		if ((count+step) > buflen) {
			step = buflen - count;
		}
		bcopy(seg->s_buf, (char *)buf + count, step);
		count += step;
	}
	return(count);
}

/*
 * seg_copyout(segments, nseg, src, count)
 *	Copy memory out to one or more segments
 *
 * Returns number of bytes copied, or error.
 */
seg_copyout(seg_t *seg, uint nseg, void *buf, uint buflen)
{
	int x;
	uint step, count = 0;

	for (x = 0; x < nseg; ++x,++seg) {
		if (count == buflen) {
			break;
		}
		step = seg->s_buflen;
		if ((count+step) > buflen) {
			step = buflen - count;
		}
		bcopy((char *)buf + count, seg->s_buf, step);
		count += step;
	}
	return(count);
}

/*
 * seg_create(buf, buflen)
 *	Create a segment
 */
seg_t
seg_create(void *buf, uint buflen)
{
	seg_t s;

	s.s_buf = buf;
	s.s_buflen = buflen;
	return(s);
}
