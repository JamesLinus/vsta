/*
 * mmap.c
 *	Primary user interface to the VM system
 */
#include <sys/mman.h>
#include <sys/vas.h>
#include <sys/pview.h>
#include <sys/pset.h>
#include <sys/percpu.h>
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/fs.h>

/*
 * mmap()
 *	Map something into the address space
 *
 * Many combinations of options are not allowed.
 */
void *
mmap(void *addr, ulong len, int prot, int flags, int fd, ulong offset)
{
	struct vas *vas = curthread->t_proc->p_vas;
	struct pview *pv;
	struct pset *ps;
	void *vaddr;

	/*
	 * Anonymous memory
	 */
	if (flags & MAP_ANON) {
		/*
		 * We don't allow you to choose (yet).  When we do, it'll
		 * probably require an enhancement to attach_zfod_vaddr(),
		 * as we need to atmoically check and add the new view at
		 * the given vaddr.
		 */
		if (addr) {
			err(EINVAL);
			return(0);
		}

		/*
		 * Keep it simple, eh?  Read-only ZFOD???
		 */
		if ((flags & (MAP_FILE|MAP_FIXED|MAP_PHYS)) ||
				!(prot & PROT_WRITE)) {
			err(EINVAL);
			return(0);
		}

		/*
		 * Get view/set
		 */
		ps = alloc_pset_zfod(btorp(len));
		pv = alloc_pview(ps);

		/*
		 * Flag view as from mmap().  We use this to keep
		 * munmap() honest; it only works on views which
		 * were created by the user.
		 */
		pv->p_prot |= PROT_MMAP;

		/*
		 * If shared, turn on bit
		 */
		if (flags & MAP_SHARED) {
			ps->p_flags |= PF_SHARED;
		}

		/*
		 * Try to attach
		 */
		vaddr = attach_pview(vas, pv);
		if (vaddr == 0) {
			free_pview(pv);
		}
		return(vaddr);
	}

	/*
	 * Physical mapping
	 */
	if (flags & MAP_PHYS) {
		/*
		 * Sanity
		 */
		if (flags & (MAP_FILE|MAP_FIXED)) {
			err(EINVAL);
			return(0);
		}

		/*
		 * Protection
		 */
		if (!issys()) {
			return(0);
		}

		/*
		 * Get a physical pset, create a view, map it in.
		 */
		ps = physmem_pset(btop(addr), btorp(len));
		pv = alloc_pview(ps);
		pv->p_prot = PROT_MMAP;
		vaddr = attach_pview(vas, pv);
		if (vaddr == 0) {
			free_pview(pv);
		}
		return(vaddr);
	}
	err(EINVAL);
	return(0);
}

/*
 * munmap()
 *	Unmap a region given an address within
 */
munmap(void *vaddr, ulong len)
{
	struct vas *vas = curthread->t_proc->p_vas;
	struct pview *pv;
	struct pset *ps;

	/*
	 * Look for the view which matches
	 */
	pv = find_pview(vas, vaddr);
	if (!pv) {
		return(err(EINVAL));
	}
	ps = pv->p_set;

	/*
	 * Make sure it's an mmap()'ed view.  Turn off bit so once we
	 * release the lock and call detach_view(), we can't race with
	 * another thread coming into munmap().
	 */
	if ((pv->p_prot & PROT_MMAP) == 0) {
		v_lock(&ps->p_lock, SPL0);
		return(err(EBUSY));
	}
	pv->p_prot &= ~(PROT_MMAP);
	v_lock(&ps->p_lock, SPL0);

	/*
	 * Blow it away
	 */
	remove_pview(vas, vaddr);

	return(0);
}
