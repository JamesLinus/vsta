/*
 * exec.c
 *	The way for a process to run a new executable file
 */
#include <sys/proc.h>
#include <sys/percpu.h>
#include <sys/thread.h>
#include <sys/mutex.h>
#include <sys/fs.h>
#include <sys/vas.h>
#include <sys/pview.h>
#include <sys/pset.h>
#include <sys/param.h>
#include <sys/exec.h>
#include <sys/port.h>
#include <sys/assert.h>
#include <lib/hash.h>

extern void set_execarg(), reset_uregs();
extern struct pset *alloc_pset_fod();
extern struct portref *delete_portref();

/* Flag that we're not allowing a map hash to be created */
#define NO_MAP_HASH ((struct hash *)1)

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
 * add_minstack()
 *	Add the minimal user stack pview
 */
static void
add_minstack(struct vas *vas)
{
	/*
	 * Stack is ZFOD at the fixed address USTACKADDR.  There isn't
	 * much to be done if it fails.
	 */
	if (!alloc_zfod_vaddr(vas, btorp(UMINSTACK), (void *)USTACKADDR)) {
#ifdef DEBUG
		printf("exec pid %d: out of swap on stack\n",
			curthread->t_proc->p_pid);
#endif
	}
}

/*
 * get_exec_pset()
 *	Return pset view of named file
 *
 * We will try to cache the mapping; if this is possible, all mappings
 * of the file will come through the same pset.  If not, we return
 * a simple private view.
 */
static struct pset *
get_exec_pset(struct portref *pr, uint hi)
{
	struct pset *ps;
	struct portref *prmap;
	long args[2];
	struct port *port;

	/*
	 * Hold mutex so we're the only one searching/updating the
	 * cache of mappings.  This also keeps the port from shutting
	 * on us.
	 */
	p_lock(&pr->p_lock, SPL0);
	port = pr->p_port;
	if (port) {
		if ((port->p_flags & P_CLOSING) ||
				(port->p_maps == NO_MAP_HASH)) {
			v_lock(&pr->p_lock, SPL0);
			port = 0;
		} else {
			p_sema_v_lock(&port->p_mapsema, PRIHI,
				&pr->p_lock);
		}
	}

	/*
	 * Try to get file ID.  Bail to simple, inefficient case if
	 * we can't.  FS_FID returns a unique ID in args[0], and
	 * the size in pages in args[1].
	 */
	if (!port || kernmsg_send(pr, FS_FID, args) || (args[1] < hi)) {
		/*
		 * Allocate a pset of the file from 0 to highest mapping needed
		 */
		if (port) {
			v_sema(&port->p_mapsema);
		}
		ps = alloc_pset_fod(pr, hi);
		ref_pset(ps);
		return(ps);
	}

	/*
	 * If there's a hash already, search it for our ID.  Otherwise
	 * create a new hash and flag that we didn't find the pset.
	 */
	if (port->p_maps) {
		ps = hash_lookup(port->p_maps, args[0]);
	} else {
		/*
		 * New hash table.  Is 32 a good number?  Who knows?
		 */
		port->p_maps = hash_alloc(32);
		ps = 0;
	}

	/*
	 * If no pset, create one and insert it.  The mapping in
	 * the hash counts as a reference.
	 */
	if (ps == 0) {
		ps = alloc_pset_fod(pr, args[1]);
		(void)hash_insert(port->p_maps, args[0], ps);
		ref_pset(ps);
	} else {
		/*
		 * Found one in hash.  Free our current portref
		 * and go with the existing cached pset/portref.
		 */
		shut_client(pr);
	}

	/*
	 * Leave an extra ref on the pset so it won't go away
	 * while we add views to it.  Our caller will release
	 * this ref once he's done his own refs.
	 */
	ref_pset(ps);

	/*
	 * Release sema and return pset
	 */
	v_sema(&port->p_mapsema);
	return(ps);
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
	struct pview *pv;
	struct pset *ps;
	uint len, x, hi;
	struct mapseg *m;

	/*
	 * Scan mappings and find the highest page needing mapping
	 */
	for (x = 0, hi = 0; x < NMAP; ++x) {
		m = &fm->m_map[x];
		if (m->m_len == 0) {		/* No more slots */
			break;
		}
		if (m->m_flags & M_ZFOD) {	/* ZFOD--doesn't count */
			continue;
		}
		if (m->m_off > hi) {		/* Highest yet */
			hi = m->m_off + m->m_len;
		}
	}

	/*
	 * Get a pset view of this file
	 */
	ps = get_exec_pset(pr, hi);

	/*
	 * Add in each view of the file
	 */
	for (x = 0, hi = 0; x < NMAP; ++x) {
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
			if (m->m_flags & M_RO) {
				/*
				 * Read-only can be a direct view
				 * of the file.
				 */
				pv = alloc_pview(ps);
				pv->p_prot = PROT_RO;
				pv->p_off = m->m_off;
			} else {
				struct pset *ps2;

				/*
				 * Read/write needs to isolate the writes
				 * by using COW.
				 */
				ps2 = alloc_pset_cow(ps, m->m_off, m->m_len);
				pv = alloc_pview(ps2);
				pv->p_prot = 0;
				pv->p_off = 0;
			}
			pv->p_len = m->m_len;
			pv->p_vaddr = m->m_vaddr;

			/*
			 * Try to attach.  Throw away if it's rejected.  Most
			 * likely this is a bogus filemap, with an address
			 * which the HAT rejected.
			 */
			if (attach_pview(vas, pv) == 0) {
				free_pview(pv);
			}
		}
	}

	/*
	 * Drop "placeholder" ref to pset (see get_exec_pset())
	 */
	deref_pset(ps);
}

/*
 * exec()
 *	Tear down old address space and map in this file to run
 *
 * We allow the caller to pass a single value, which is pushed onto
 * the new stack of the process.
 */
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
	discard_vas(p->p_vas);

	/*
	 * Add back a minimal stack
	 */
	add_minstack(p->p_vas);

	/*
	 * Put in the views of the file
	 */
	add_views(p->p_vas, pr, &m);

	/*
	 * We don't really have a name for this any more
	 */
	p->p_cmd[0] = '\0';

	/*
	 * Pass the argument back in a machine-dependent way
	 */
	reset_uregs(t, (ulong)(m.m_entry));
	set_execarg(t, arg);
	return(0);
}

/*
 * do_derefport()
 *	Release a cached access to a port
 */
static
do_derefport(struct port *port, void *dummy)
{
	deref_port(port);
}

/*
 * exec_cleanup()
 *	Clean up mapped file cache, if any
 */
void
exec_cleanup(struct port *port)
{
	/*
	 * Take map mutex
	 */
	p_sema(&port->p_mapsema, PRIHI);

	/*
	 * No cache--nothing to do
	 */
	if (port->p_maps == 0) {
		v_sema(&port->p_mapsema);
		return;
	}

	/*
	 * Remove reference from pset for each entry
	 * in our cache.
	 */
	hash_foreach(port->p_maps, do_derefport, 0);

	/*
	 * Free the hash storage, flag port as shutting down
	 */
	hash_dealloc(port->p_maps);
	port->p_maps = NO_MAP_HASH;

	v_sema(&port->p_mapsema);
}

/*
 * unhash()
 *	Unhash any reference to the indicated hash #
 */
unhash(port_t arg_port, long arg_fid)
{
	struct port *port;
	struct proc *p = curthread->t_proc;

	/*
	 * Verify range
	 */
	if (arg_port < PROCOPENS) {
		return(err(EBADF));
	}
	arg_port -= PROCOPENS;
	if (arg_port >= PROCPORTS) {
		return(err(EBADF));
	}

	/*
	 * Interlock, get access to port, atomically switch
	 * to mapsema.
	 */
	(void)p_sema(&p->p_sema, PRILO);
	port = p->p_ports[arg_port];
	if (port == 0) {
		v_sema(&p->p_sema);
		return(err(EBADF));
	}
	p_lock(&port->p_lock, SPL0);
	v_sema(&p->p_sema);
	p_sema_v_lock(&port->p_mapsema, PRIHI, &port->p_lock);

	/*
	 * If there's a map, delete entry from it
	 */
	if (port->p_maps && (port->p_maps != NO_MAP_HASH)) {
		struct pset *ps;

		ps = hash_lookup(port->p_maps, arg_fid);
		if (ps) {
			(void)hash_delete(port->p_maps, arg_fid);
			deref_pset(ps);
		}
	}

	/*
	 * Release mutex, return success
	 */
	v_sema(&port->p_mapsema);
	return(0);
}
