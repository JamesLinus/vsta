/*
 * proc.c
 *	Routines for creating/deleting processes
 */
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/vas.h>
#include <sys/pview.h>
#include <sys/pset.h>
#include <sys/percpu.h>
#include <sys/boot.h>
#include <sys/vm.h>
#include <sys/sched.h>
#include <lib/alloc.h>
#include <sys/param.h>
#include <lib/hash.h>
#include <sys/fs.h>
#include <sys/assert.h>

extern void setrun(), dup_stack();
extern struct sched sched_root;
extern lock_t runq_lock;

ulong npid_free = (ulong)-1;	/* # PIDs free in pool */
ulong pid_nextfree = 0L;	/* Next free PID number */
struct proc *allprocs = 0;	/* List of all procs */
sema_t pid_sema;		/* Mutex for PID pool and proc lists */

int nthread;			/* # threads currently in existence */

struct hash *pid_hash;		/* Mapping PID->proc */

/*
 * mkview()
 *	Create a boot task pview
 */
static struct pview *
mkview(uint pfn, void *vaddr, uint pages, struct vas *vas)
{
	struct pview *pv;
	struct pset *ps;
	extern struct pset *physmem_pset();

	ps = physmem_pset(pfn, pages);
	pv = malloc(sizeof(struct pview));
	pv->p_set = ps;
	ref_pset(ps);
	pv->p_vaddr = vaddr;
	pv->p_len = pages;
	pv->p_off = 0;
	pv->p_vas = vas;
	pv->p_next = vas->v_views;
	vas->v_views = pv;
	pv->p_prot = 0;
	return(pv);
}

/*
 * bootproc()
 *	Given a boot process image, throw together a proc for it
 */
static void
bootproc(struct boot_task *b)
{
	struct proc *p;
	struct thread *t;
	struct pview *pv;
	struct vas *vas;
	extern void boot_regs();
	extern void *alloc_zfod_vaddr();

	/*
	 * Get a proc, make him sys/sys with the usual values for
	 * the various fields.
	 */
	p = malloc(sizeof(struct proc));
	bzero(p, sizeof(struct proc));
	zero_ids(p->p_ids, PROCPERMS);
	p->p_ids[0].perm_len = 2;
	p->p_ids[0].perm_id[0] = 1;
	p->p_ids[0].perm_id[1] = 1;

	/*
	 * Grant him root, but leave it disabled.  Those who really
	 * need it will know how to turn it on.
	 */
	p->p_ids[1].perm_len = 0;
	PERM_DISABLE(&p->p_ids[1]);

	/*
	 * Default protection: require sys/sys to touch us.
	 */
	p->p_prot.prot_len = 2;
	p->p_prot.prot_default = 0;
	p->p_prot.prot_id[0] = 1;
	p->p_prot.prot_id[1] = 1;
	p->p_prot.prot_bits[0] = 0;
	p->p_prot.prot_bits[1] = P_PRIO|P_SIG|P_KILL|P_STAT|P_DEBUG;

	/*
	 * Initialize other fields
	 */
	init_sema(&p->p_sema);
	p->p_runq = sched_node(&sched_root);
	p->p_pgrp = alloc_pgrp();
	p->p_children = alloc_exitgrp(p);
	p->p_parent = alloc_exitgrp(0); ref_exitgrp(p->p_parent);

	/*
	 * Set up the single thread for the proc
	 */
	t = malloc(sizeof(struct thread));
	bzero(t, sizeof(struct thread));
	t->t_kstack = malloc(KSTACK_SIZE);
	t->t_ustack = (void *)USTACKADDR;
	p->p_threads = t;
	boot_regs(t, b);
	t->t_runq = sched_thread(p->p_runq, t);
	init_sema(&t->t_msgwait);
	t->t_proc = p;
	init_sema(&t->t_evq); set_sema(&t->t_evq, 1);
	t->t_state = TS_SLEEP;	/* -> RUN in setrun() */

	/*
	 * The vas for the proc
	 */
	vas = malloc(sizeof(struct vas));
	vas->v_views = 0;
	vas->v_flags = VF_MEMLOCK;
	init_lock(&vas->v_lock);
	hat_initvas(vas);
	p->p_vas = vas;

	/*
	 * The views of text and data (and BSS)
	 */
	pv = mkview(b->b_pfn, b->b_textaddr, b->b_text, vas);
	pv->p_prot |= PROT_RO;
	(void)mkview(b->b_pfn + b->b_text, b->b_dataaddr, b->b_data, vas);

	/*
	 * Stack is ZFOD
	 */
	ASSERT(alloc_zfod_vaddr(vas, btorp(UMINSTACK), (void *)USTACKADDR),
		"bootproc: no stack");

	/*
	 * Get their PIDs
	 */
	p->p_pid = allocpid();
	t->t_pid = allocpid();
	join_pgrp(p->p_pgrp, p->p_pid);

	/*
	 * Add process to list of all processes
	 */
	p->p_allnext = allprocs;
	allprocs = p;

	/*
	 * Leave him hanging around ready to run
	 */
	setrun(t);
}

/*
 * refill_pids()
 *	When the PID pool runs out, replenish it
 *
 * Called with PID mutex held.
 */
static void
refill_pids(void)
{
	static ulong rotor = 200L;	/* Where to scan from */
	ulong pnext, pid;
	struct proc *p;

retry:
	pnext = (ulong)-1;	/* XXX assumes two's complement */
	for (p = allprocs; p; p = p->p_allnext) {
		struct thread *t = 0;

		pid = p->p_pid;
		do {
			/*
			 * We're not interested in ones below; we're trying
			 * to build a range starting at rotor
			 */
			if (pid < rotor) {
				continue;
			}

			/*
			 * Collision with rotor
			 */
			if (pid == rotor) {
				/*
				 * The worst of all cases--we collided and
				 * there's * one above.  Our whole range is
				 * shot.  Advance the rotor and try again.
				 */
				if (++rotor == pnext) {
					++rotor;
					goto retry;
				}
				continue;
			}

			/*
			 * If we've found a new lower bound for our range,
			 * record it.
			 */
			if (pid < pnext) {
				pnext = pid;
			}

			/*
			 * Advance to next PID in threads
			 */
			if (!t) {
				t = p->p_threads;
			} else {
				t = t->t_next;
			}
			if (t) {
				pid = t->t_pid;
			}
		} while (t);
	}

	/*
	 * Update our pool range; set the rotor to one above the
	 * highest.
	 */
	npid_free = pnext - rotor;
	pid_nextfree = rotor;
	rotor = pnext+1;
}

/*
 * allocpid()
 *	Allocate a new PID
 *
 * We keep a range of free process IDs memorized.  When we exhaust it,
 * we scan for a new range.
 */
ulong
allocpid(void)
{
	ulong pid;

	/*
	 * If we're out, scan for new ones
	 */
	if (npid_free == 0) {
		refill_pids();
	}

	/*
	 * Take next free PID
	 */
	npid_free -= 1;
	pid = pid_nextfree++;

	return(pid);
}

/*
 * fork_thread()
 *	Launch a new thread within the same process
 */
fork_thread(voidfun f)
{
	struct proc *p = curthread->t_proc;
	struct thread *t;
	void *ustack;
	uint npid;
	extern void *alloc_zfod();

	/*
	 * Get a user stack first
	 */
	ustack = alloc_zfod(p->p_vas, btop(UMINSTACK));
	if (!ustack) {
		return(err(ENOMEM));
	}

	/*
	 * Do an unlocked increment of the thread count.  The limit
	 * is thus approximate; worth it for a faster thread launch?
	 */
	if (nthread >= NPROC) {
		remove_pview(p->p_vas, ustack);
		return(err(ENOMEM));
	}
	ATOMIC_INC(&nthread);

	/*
	 * Allocate thread structure, set up its fields
	 */
	t = malloc(sizeof(struct thread));

	/*
	 * Most stuff we can just copy from the current thread
	 */
	*t = *curthread;

	/*
	 * Get him his own PID
	 */
	p_sema(&pid_sema, PRIHI);
	npid = t->t_pid = allocpid();
	v_sema(&pid_sema);

	/*
	 * He needs his own kernel stack; user stack was attached above
	 */
	t->t_kstack = malloc(KSTACK_SIZE);
	t->t_ustack = ustack;
	dup_stack(curthread, t, f);

	/*
	 * Initialize
	 */
	init_sema(&t->t_msgwait);
	t->t_usrcpu = t->t_syscpu = 0L;
	t->t_evsys[0] = t->t_evproc[0] = '\0';
	init_sema(&t->t_evq); set_sema(&t->t_evq, 1);
	t->t_err[0] = '\0';
	t->t_runq = sched_thread(p->p_runq, t);
	t->t_uregs = 0;
	t->t_state = TS_SLEEP;

	/*
	 * Add new guy to the proc's list
	 */
	p_sema(&p->p_sema, PRIHI);
	t->t_next = p->p_threads;
	p->p_threads = t;
	v_sema(&p->p_sema);

	/*
	 * Set him running
	 */
	setrun(t);

	/*
	 * Old thread returns with new thread's PID as its return value
	 */
	return (npid);
}

/*
 * init_proc()
 *	Set up stuff for process management
 */
void
init_proc(void)
{
	struct boot_task *b;
	int x;

	init_sema(&pid_sema);
	pid_hash = hash_alloc(NPROC/4);
	for (b = boot_tasks, x = 0; x < nboot_task; ++b, ++x) {
		bootproc(b);
	}
}

/*
 * fork()
 *	Fork out an entirely new process
 */
fork(void)
{
	struct thread *tnew, *told = curthread;
	struct proc *pold = told->t_proc, *pnew;
	uint npid;
	extern struct vas *fork_vas();

	/*
	 * Allocate new structures
	 */
	tnew = malloc(sizeof(struct thread));
	pnew = malloc(sizeof(struct proc));

	/*
	 * Get new thread
	 */
	tnew->t_kstack = malloc(KSTACK_SIZE);
	tnew->t_flags = told->t_flags;
	tnew->t_hd = tnew->t_tl = tnew->t_next = 0;
	tnew->t_wchan = 0;
	tnew->t_intr = 0;
	init_sema(&tnew->t_msgwait);
	tnew->t_probe = 0;
	tnew->t_err[0] = '\0';
	tnew->t_usrcpu = tnew->t_syscpu = 0L;
	tnew->t_evsys[0] = tnew->t_evproc[0] = '\0';
	init_sema(&tnew->t_evq); set_sema(&tnew->t_evq, 1);
	tnew->t_state = TS_SLEEP;	/* -> RUN in setrun() */
	tnew->t_ustack = (void *)USTACKADDR;

	/*
	 * Get new PIDs for process and initial thread.  Insert
	 * PID into hash.
	 */
	p_sema(&pid_sema, PRIHI);
	pnew->p_pid = allocpid();
	tnew->t_pid = allocpid();
	v_sema(&pid_sema);


	/*
	 * Get new proc, copy over fields as appropriate
	 */
	tnew->t_proc = pnew;
	p_sema(&pold->p_sema, PRIHI);
	bcopy(pold->p_ids, pnew->p_ids, sizeof(pold->p_ids));
	init_sema(&pnew->p_sema);
	pnew->p_prot = pold->p_prot;
	pnew->p_threads = tnew;
	pnew->p_vas = fork_vas(tnew, pold->p_vas);
	pnew->p_runq = sched_node(pold->p_runq->s_up);
	tnew->t_runq = sched_thread(pnew->p_runq, tnew);
	bzero(&pnew->p_ports, sizeof(pnew->p_ports));
	fork_ports(pold->p_open, pnew->p_open, PROCOPENS);
	pnew->p_prefs = 0;
	pnew->p_nopen = pold->p_nopen;
	pnew->p_pgrp = pold->p_pgrp; join_pgrp(pold->p_pgrp, pnew->p_pid);
	pnew->p_parent = pold->p_children; ref_exitgrp(pnew->p_parent);
	bcopy(pold->p_cmd, pnew->p_cmd, sizeof(pnew->p_cmd));
	v_sema(&pold->p_sema);
	pnew->p_children = alloc_exitgrp(pnew);

	/*
	 * Duplicate stack now that we have a viable thread/proc
	 * structure.
	 */
	dup_stack(told, tnew, 0);

	/*
	 * Now that we're ready, make PID known globally
	 */
	p_sema(&pid_sema, PRIHI);
	hash_insert(pid_hash, pnew->p_pid, pnew);
	v_sema(&pid_sema);

	/*
	 * Add to "all procs" list
	 */
	p_lock(&runq_lock, SPLHI);
	pnew->p_allnext = allprocs;
	allprocs = pnew;
	npid = pnew->p_pid;

	/*
	 * Leave him runnable
	 */
	lsetrun(tnew);
	v_lock(&runq_lock, SPL0);
	return(npid);
}

/*
 * free_proc()
 *	Routine to tear down and free proc
 */
static void
free_proc(struct proc *p)
{
	struct proc *p2, **pp;

	/*
	 * Close both server and client open ports
	 */
	close_ports(p->p_ports, PROCPORTS);
	close_portrefs(p->p_open, PROCOPENS);

	/*
	 * Clean our our vas
	 */
	if (p->p_vas->v_flags & VF_DMA) {
		pages_release(p);
	}
	free_vas(p->p_vas);

	/*
	 * Delete us from the "allproc" list
	 */
	p_sema(&pid_sema, PRIHI);
	pp = &allprocs;
	for (p2 = allprocs; p2; p2 = p2->p_allnext) {
		if (p2 == p) {
			*pp = p->p_allnext;
			break;
		}
		pp = &p2->p_allnext;
	}
	ASSERT(p2, "free_proc: lost proc");

	/*
	 * Unhash our PID
	 */
	hash_delete(pid_hash, p->p_pid);
	v_sema(&pid_sema);

	/*
	 * Depart process group
	 */
	leave_pgrp(p->p_pgrp, p->p_pid);

	/*
	 * Release proc storage
	 */
	free(p);
}

/*
 * exit()
 *	The exit() system call
 *
 * Tricky once we tear down our kernel stack, because we can no longer
 * use stack variables once this happens.  The trivial way to deal with
 * this is to declare them "register" and hope the declaration is honored.
 * I'm trying to avoid this by using just the percpu "curthread" value.
 */
exit(int code)
{
	struct thread *t = curthread, *t2, **tp;
	struct proc *p = t->t_proc;
	int last;

	/*
	 * Remove our thread from the process hash list
	 */
	p_sema(&p->p_sema, PRIHI);
	tp = &p->p_threads;
	for (t2 = p->p_threads; t2; t2 = t2->t_next) {
		if (t2 == t) {
			*tp = t->t_next;
			break;
		}
		tp = &t2->t_next;
	}
	ASSERT(t2, "exit: lost thread");
	last = (p->p_threads == 0);

	/*
	 * Accumulate CPU in proc
	 */
	p->p_usr += t->t_usrcpu;
	p->p_sys += t->t_syscpu;

	v_sema(&p->p_sema);

	/*
	 * Let through anybody waiting to signal
	 */
	while (blocked_sema(&t->t_evq)) {
		v_sema(&t->t_evq);
	}

	/*
	 * Tear down the thread's user stack if not last.  If it's
	 * last, it'll get torn down with the along with the rest
	 * of the vas.
	 */
	if (!last) {
		remove_pview(p->p_vas, t->t_ustack);
	} else {
		/*
		 * Detach from our exit groups
		 */
		noparent_exitgrp(p->p_children);
		post_exitgrp(p->p_parent, p, code);
		deref_exitgrp(p->p_parent);

		/*
		 * If last thread gone, tear down process.
		 */
		free_proc(p);
	}

	/*
	 * Free kernel stack once we've switched to our idle stack.
	 * Can't use local variables after this!
	 */
	ATOMIC_INC(&cpu.pc_locks);	/* To avoid preemption */
	idle_stack();
	free(curthread->t_kstack);
	free(curthread);
	curthread = 0;

	/*
	 * Free thread, switch to new work
	 */
	ATOMIC_DEC(&nthread);
	p_lock(&runq_lock, SPLHI);
	ATOMIC_DEC(&cpu.pc_locks);	/* swtch() will handle dispatch */
	swtch();
}

/*
 * pfind()
 *	Find a process, return it locked
 */
struct proc *
pfind(ulong pid)
{
	struct proc *p;

	p_sema(&pid_sema, PRIHI);
	p = hash_lookup(pid_hash, pid);
	if (p) {
		/*
		 * Lock him
		 */
		p_sema(&p->p_sema, PRIHI);
	}
	v_sema(&pid_sema);
	return(p);
}

/*
 * waits()
 *	Get next exit() event from our exit group
 */
waits(struct exitst *w)
{
	struct proc *p = curthread->t_proc;
	struct exitst *e;
	int x;

	/*
	 * Get next event.  We will vector out on interrupted system
	 * call, so a NULL return here simply means there are no
	 * children on which to wait.
	 */
	e = wait_exitgrp(p->p_children);
	if (e == 0) {
		return(err(ESRCH));
	}

	/*
	 * Copy out the status.  Hide a field which they don't
	 * need to see.  No big security risk, but helps provide
	 * determinism.
	 */
	if (w) {
		e->e_next = 0;
		if (copyout(w, e, sizeof(struct exitst))) {
			return(err(EFAULT));
		}
	}

	/*
	 * Record PID, free memory, return PID as value of syscall
	 */
	x = e->e_code;
	free(e);
	return(x);
}
