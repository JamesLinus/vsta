/*
 * exec.c
 *	The way for a process to run a new executable file
 */
#include <sys/proc.h>
#include <sys/percpu.h>
#include <sys/thread.h>
#include <sys/fs.h>
#include <sys/vas.h>
#include <sys/pview.h>
#include <sys/pset.h>
#include <sys/param.h>
#include <sys/exec.h>
#include <sys/port.h>
#include <sys/assert.h>
#include <sys/misc.h>
#include <hash.h>
#include "../mach/mutex.h"
#include "../mach/locore.h"

extern void set_execarg(), reset_uregs();
extern struct portref *delete_portref();

/*
 * discard_vas()
 *	Tear down the vas, leaving just shared objects
 */
static void
discard_vas(struct vas *vas)
{
	struct pview *pv, *pvn;

	for (pv = vas->v_views; pv; pv = pvn) {
		pvn = pv->p_next;

		/*
		 * Only stuff we mmap()'ed as sharable will remain
		 */
		if ((pv->p_prot & PROT_MMAP) &&
				(pv->p_set->p_flags & PF_SHARED)) {
			continue;
		}

		/*
		 * Toss the rest
		 */
		ASSERT(detach_pview(vas, pv->p_vaddr),
			"discard_vas: detach failed");
		free_pview(pv);
	}
}

/*
 * add_stack()
 *	Add the user stack pview
 */
static void
add_stack(struct vas *vas)
{
	/*
	 * Stack is ZFOD at the fixed address USTACKADDR.  There isn't
	 * much to be done if it fails.
	 */
	if (!alloc_zfod_vaddr(vas, btorp(UMAXSTACK), (void *)USTACKADDR)) {
#ifdef DEBUG
		printf("exec pid %d: out of swap on stack\n",
			curthread->t_proc->p_pid);
#endif
	}
}

/*
 * add_views()
 *	Add mappings of the port
 *
 * Given the description of the port and its offset/lengths, we wire
 * in a pset doing FOD from that port, and the views described.
 */
static void
add_views(struct vas *vas, struct portref *pr, struct mapfile *fm)
{
	uint x;
	struct mapseg *m;

	/*
	 * Add in each view of the file
	 */
	for (x = 0; x < NMAP; ++x) {
		m = &fm->m_map[x];
		if (m->m_len == 0) {	/* No more */
			break;
		}

		/*
		 * Get the various views: FOD, COW, and ZFOD
		 */
		if (m->m_flags & M_ZFOD) {
			/*
			 * ZFOD--use our general-purpose routine
			 */
			(void)alloc_zfod_vaddr(vas, m->m_len, m->m_vaddr);
		} else {
			/*
			 * Get a view of our FOD pset, tweak its attributes
			 */
			(void)add_map(vas, pr,
				m->m_vaddr, m->m_len, m->m_off,
				m->m_flags & M_RO);
		}
	}
}

/*
 * exec()
 *	Tear down old address space and map in this file to run
 *
 * We allow the caller to pass a single value, which is pushed onto
 * the new stack of the process.
 */
int
exec(uint arg_port, struct mapfile *arg_map, void *arg)
{
	struct thread *t = curthread;
	struct proc *p = t->t_proc;
	uint x;
	struct portref *pr;
	struct mapfile m;

	/*
	 * Get the description of the file mapping
	 */
	if (copyin(arg_map, &m, sizeof(m))) {
		return(err(EFAULT));
	}

	/*
	 * Can't tear down the address space if we're sharing it
	 * with our friends.
	 */
	p_sema(&p->p_sema, PRIHI);
	x = ((p->p_threads != t) || (t->t_next != 0));
	v_sema(&p->p_sema);
	if (x) {
		return(err(EBUSY));
	}

	/*
	 * We can now assume we're single-threaded, sparing us from
	 * many of the race conditions which would otherwise exist.
	 */

	/*
	 * See if we can find this portref
	 */
	pr = delete_portref(p, arg_port);
	if (pr == 0) {
		return(err(EINVAL));
	}
	v_sema(&pr->p_sema);

	/*
	 * Tear down most of our vas
	 */
	discard_vas(&p->p_vas);

	/*
	 * Add back a minimal stack
	 */
	add_stack(&p->p_vas);

	/*
	 * Put in the views of the file
	 */
	add_views(&p->p_vas, pr, &m);

	/*
	 * Flush our own handle to the port; the views we added
	 * will have their own references.
	 */
	shut_client(pr);

	/*
	 * We don't really have a name for this any more, and our
	 * handler probably isn't meaningful.
	 */
	p->p_cmd[0] = '\0';
	p->p_handler = 0;

	/*
	 * Pass the argument back in a machine-dependent way
	 */
	reset_uregs(t, (ulong)(m.m_entry));
	set_execarg(t, arg);

	/*
	 * Pop out to debugger if so desired
	 */
	PTRACE_PENDING(p, PD_EXEC, 0);
	return(0);
}
