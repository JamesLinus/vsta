/*
 * seg.c
 *	Routines for manipulating segments
 *
 * A segment is the unit of memory which gets sent around the system.
 * It contains a pview, which holds a reference to the underlying
 * virtual pages.  It also contains a byte offset and byte length,
 * indicating the specific bytes within the range which the segment
 * represents.
 *
 * To minimize the cost of duplicating a pview while creating a new
 * segment, ATOMIC_INC() is used to increment the reference count
 * of the underlying pset.  Since the existing pview is assumed to
 * have been locked properly, it is impossible for this atomic
 * increment to race with a tear-down of the pset.
 *
 * Sadly, it must be done the hard way when ATOMIC_DEC'ing the pset.
 * In various failure scenarios the recipient of a segment may very
 * well be the last reference to it.
 */
#include <sys/vas.h>
#include <sys/pview.h>
#include <sys/pset.h>
#include <sys/core.h>
#include <sys/seg.h>
#include <sys/mutex.h>
#include <sys/param.h>
#include <sys/msg.h>
#include <sys/fs.h>
#include <sys/assert.h>

extern void *malloc(), page_lock(), page_unlock();

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

/*
 * free_seg()
 *	Delete a segment, remove its references
 *
 * Must free the pset when reference count reaches 0
 */
void
free_seg(struct seg *s)
{
	struct pset *ps;

	/*
	 * Release reference to pset
	 */
	ps = s->s_pview.p_set;
	deref_pset(ps);

	/*
	 * Free this segment storage
	 */
	free(s);
}

/*
 * make_seg()
 *	Given a vas and a byte range, return a segment describing it.
 *
 * The byte range must occupy a single pset.  On error, 0 is returned.
 */
struct seg *
make_seg(struct vas *vas, void *buf, uint buflen)
{
	struct pview *pv;
	struct seg *s;
	int x;

	/*
	 * Invalid buf/buflen?
	 */
	if ((buf == 0) || (buflen == 0)) {
		return(0);
	}

	/*
	 * Get new segment
	 */
	s = malloc(sizeof(struct seg));

	/*
	 * Find pview holding the starting address
	 */
	pv = find_pview(vas, buf);
	if (!pv) {
		free(s);
		return(0);
	}

	/*
	 * Make sure end lies within pview
	 */
	if (((char *)buf + buflen) > ((char *)pv->p_vaddr+ptob(pv->p_len))) {
		v_lock(&pv->p_set->p_lock, SPL0);
		free(s);
		return(0);
	}

	/*
	 * Duplicate view, record byte offset
	 */
	s->s_pview = *pv;
	ref_pset(pv->p_set);
	s->s_off = (char *)buf - (char *)pv->p_vaddr;
	s->s_len = buflen;

	/*
	 * Trim off leading and trailing pages from the view
	 */
	pv = &s->s_pview;
	x = btop(s->s_off);
	pv->p_off += x;
	s->s_off -= ptob(x);
	pv->p_len = btorp(s->s_off + buflen);

	/*
	 * Done with set
	 */
	v_lock(&pv->p_set->p_lock, SPL0);
	return(s);
}

/*
 * attach_seg()
 *	Attach a segment to a vas
 *
 * Since the *recipient* always copies in, the mapped segment will
 * never be writable.  XXX should make PROT_KERN for msg_receive();
 * the kernel does the copying out.
 */
attach_seg(struct vas *vas, struct seg *s)
{
	struct pview *pv;

	/*
	 * Arrange a couple of fields the HAT might want
	 */
	pv = &s->s_pview;
	if (vas->v_flags & VF_DMA) {
		pv->p_prot = 0;
	} else {
		pv->p_prot = PROT_RO;
	}
	pv->p_vaddr = 0;		/* We don't care */

	/*
	 * Get view attached
	 */
	if (attach_pview(vas, pv)) {
		return(0);
	}
	return(1);
}

/*
 * detach_seg()
 *	Remove a segment view from a vas
 */
void
detach_seg(struct seg *s)
{
	struct pview *pv = &s->s_pview;

	detach_pview(pv->p_vas, pv->p_vaddr);
}

/*
 * kern_mem()
 *	Create a segment which views a range of kernel memory
 *
 * Mostly used to provide a view of a new client's capabilities.
 */
struct seg *
kern_mem(void *vaddr, uint len)
{
	struct seg *s;
	ulong pgstart, pgend;
	struct pview *pv;
	int x;
	struct pset *ps;
	extern struct pset *physmem_pset();

	/*
	 * Allocate a new segment, fill in some fields.  Use the
	 * pset layer to create a physmem-type page set on which
	 * we build our view.
	 */
	s = malloc(sizeof(struct seg));
	s->s_off = (ulong)vaddr & (NBPG-1);
	s->s_len = len;
	pv = &s->s_pview;
	pgstart = btop(vaddr);
	pgend = btop((char *)vaddr + len - 1);
	pv->p_len = pgend-pgstart+1;
	pv->p_off = 0;
	ps = pv->p_set = physmem_pset(0, pv->p_len);
	ref_pset(ps);
	pv->p_prot = PROT_RO;

	/*
	 * Fill in the slots of the physmem pset with the actual
	 * page numbers for each page in the vaddr range.
	 */
	for (x = 0; x < pv->p_len; ++x) {
		struct perpage *pp;

		pp = find_pp(ps, x);
		pp->pp_pfn = btop(vtop((char *)vaddr + ptob(x)));
	}
	return(s);
}

/*
 * copyoutseg()
 *	Copy out segments to user address space
 *
 * Copies out to the greater of what's in the sysmsg and what's
 * specified by the user message.  Returns number of bytes actually
 * copied, or -1 on error.
 */
copyoutsegs(struct sysmsg *sm, struct msg *m)
{
	uint cnt, total = 0;
	int nsm_segs = sm->m_nseg, nm_segs = m->m_nseg;
	struct seg **sm_pp = &(sm->m_seg[0]),
		*sm_segs = *sm_pp++;
	seg_t *m_segs = m->m_seg;

	do {
		/*
		 * Calculate how much we can move this time
		 */
		cnt = MIN(sm_segs->s_len, m_segs->s_buflen);

		/*
		 * If non-zero, move the next batch
		 */
		if (cnt > 0) {
			char *from, *to;

			/*
			 * Copy the memory
			 */
			from = sm_segs->s_pview.p_vaddr;
			from += sm_segs->s_off;
			to = m_segs->s_buf;
			if (uucopy(to, from, cnt)) {
				return(err(EFAULT));
			}
			total += cnt;

			/*
			 * Advance the sysmsg
			 */
			if ((sm_segs->s_len -= cnt) == 0) {
				sm_segs = *sm_pp++;
				if ((--nsm_segs) == 0) {
					return(total);
				}
			} else {
				sm_segs->s_off += cnt;
			}

			/*
			 * Advance the user message
			 */
			if ((m_segs->s_buflen -= cnt) == 0) {
				++m_segs;
				if ((--nm_segs) == 0) {
					return(total);
				}
			} else {
				m_segs->s_buf = ((char *)m_segs->s_buf) + cnt;
			}
		}
	} while (cnt > 0);
	return(total);
}
