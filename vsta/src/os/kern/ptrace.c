/*
 * ptrace.c
 *	Not really a UNIX ptrace clone, but same idea
 *
 * Routines to support debugging.
 */
#include <sys/proc.h>
#ifdef PROC_DEBUG
#include <sys/thread.h>
#include <sys/fs.h>
#include <sys/percpu.h>
#include <sys/port.h>

/*
 * ptrace()
 *	Cause a process to begin talking to us as a debugging slave
 */
int
ptrace(pid_t pid, port_name name)
{
	struct proc *p;
	uint x;
	extern struct proc *pfind();

	/*
	 * Find the process.  Bomb if he doesn't exist, or is
	 * already being debugged.
	 */
	p = pfind(pid);
	if (!p) {
		return(err(ESRCH));
	}
	if (p->p_dbg.pd_name) {
		v_sema(&p->p_sema);
		return(err(EBUSY));
	}

	/*
	 * See if we have the rights to do this
	 */
	x = perm_calc(curthread->t_proc->p_ids, PROCPERMS, &p->p_prot);
	if (!(x & P_DEBUG)) {
		return(err(EPERM));
	}

	/*
	 * Stuff his fields with our request for him to call us.
	 * Then let him go.
	 */
	p->p_dbg.pd_port = -1;
	p->p_dbg.pd_name = name;
	p->p_dbg.pd_flags = PD_ALWAYS|PD_EVENT|PD_EXIT;
	v_sema(&p->p_sema);
	return(0);
}

/*
 * ptrace_attach()
 *	Called by slave to first connect to master
 */
static void
ptrace_attach(void)
{
	struct proc *p = curthread->t_proc;
	port_t port;
	uint oflags;

	/*
	 * Make sure we need to start up
	 */
	if ((p->p_dbg.pd_name == 0) || (p->p_dbg.pd_port != -1)
			|| (p->p_dbg.pd_flags & PD_CONNECTING)) {
		v_sema(&p->p_sema);
		return;
	}

	/*
	 * Try to connect.  The PD_CONNECTING bit will keep other
	 * threads from falling into this simultaneously.  We need
	 * to release p_sema because msg_connect() will acquire
	 * it.
	 */
	oflags = p->p_dbg.pd_flags;
	p->p_dbg.pd_flags |= PD_CONNECTING;
	v_sema(&p->p_sema);
	port = msg_connect(p->p_dbg.pd_name, 0);

	/*
	 * This might require taking the semaphore again, but I can't
	 * think of a case where anybody else is allowed to fiddle
	 * this while a connect is in progress.
	 */
	p->p_dbg.pd_flags = oflags;

	/*
	 * If our connect bombs, junk this ptrace() request
	 * and life just goes on.
	 */
	if (port < 0) {
		p->p_dbg.pd_name = 0;
		p->p_dbg.pd_flags = 0;
		return;
	}

	/*
	 * Flag our port, and away we go
	 */
	p->p_dbg.pd_port = port;
}

/*
 * ptrace_slave()
 *	Called by slave process when it detects a need to call the master
 *
 * Actually, it's called when it *appears* that a need exists; this
 * routine does locking and handles the case where it really wasn't
 * needed.
 *
 * XXX use "event" (pack into longs?), and allow them to clear it
 * unless it's the unblockable kill message.
 */
void
ptrace_slave(char *event)
{
	struct thread *t = curthread;
	struct proc *p = t->t_proc;
	struct portref *pr;
	port_t port;
	long args[3];
	uint x;
	extern struct portref *find_portref();

retry:
	p_sema(&p->p_sema, PRIHI);

	/*
	 * If we've raced with another thread doing the setup, just
	 * continue.  This would be a pretty chaotic situation anyway.
	 */
	if (p->p_dbg.pd_flags & PD_CONNECTING) {
		v_sema(&p->p_sema);
		return;
	}

	/*
	 * If it appears we're the first to have seen this ptrace
	 * request, do the initial connect and setup.  Then start
	 * over.
	 */
	if (p->p_dbg.pd_name && (p->p_dbg.pd_port == -1)) {
		ptrace_attach();
		goto retry;
	}

	/*
	 * Try to get our portref to the debug port.  If it has
	 * gone away, clear our debug environment and continue.
	 *
	 * After this block of code, we hold a semaphore for clients
	 * on the named portref, and we have released our proc
	 * semaphore.  We are thus in a pretty good position to
	 * interact at length with our debugger.
	 */
	port = p->p_dbg.pd_port;
	v_sema(&p->p_sema);
	pr = find_portref(p, port);
	if (pr == 0) {
		p_sema(&p->p_sema, PRIHI);
		/*
		 * This could actually blow away a usable, new debug
		 * session.  C'est la vie.  To get here you had to
		 * hunt down your port and msg_disconnect() it yourself,
		 * which is pretty hosed in itself.
		 */
		if (p->p_dbg.pd_port == port) {
			bzero(&p->p_dbg, sizeof(struct pdbg));
		}
		v_sema(&p->p_sema);
		return;
	}

	/*
	 * kernmsg_send() does this for itself.  We hold the
	 * semaphore, so we won't race with other I/O clients.
	 */
	v_lock(&pr->p_lock, SPL0);

	/*
	 * Return value is initially 0.  It will be set to the
	 * result of the last operation on iterations of the loop.
	 */
	args[0] = args[1] = 0;
	for (;;) {
		/*
		 * Build a message and send it
		 */
		if (kernmsg_send(pr, PD_SLAVE, args) < 0) {
			v_sema(&pr->p_sema);
			p_sema(&p->p_sema, PRIHI);
			(void)msg_disconnect(p->p_dbg.pd_port);
			bzero(&p->p_dbg, sizeof(struct pdbg));
			v_sema(&p->p_sema);
			return;
		}

		/*
		 * Act on his answer
		 */
		switch (args[2]) {
		case PD_RUN:	/* Continue running */
			v_sema(&pr->p_sema);
			return;

		case PD_STEP:	/* Run for one step */
			single_step(args[0]);
			break;

		case PD_BREAK:	/* Set/clear breakpoint */
			args[0] = set_break(args[1], args[0]);
			break;

		case PD_RDREG:	/* Read register */
			args[0] = getreg(args[0]);
			break;
		case PD_WRREG:	/* Write register */
			args[0] = setreg(args[0], args[1]);
			break;

		case PD_MASK:	/* Set debug event mask */
			p->p_dbg.pd_flags = args[1];
			break;

		case PD_RDMEM:	/* Read memory */
			{ ulong l;
			  if (copyin(args[0], &l, sizeof(l)) < 0) {
				args[1] = 1;
			  } else {
			  	args[0] = l;
				args[1] = 0;
			  }
			}
			break;
		case PD_WRMEM:	/* Write memory */
			if (copyout(args[0], &args[1], sizeof(args[1]))) {
				args[1] = 1;
			} else {
				args[1] = 0;
			}
			args[0] = 0;
			break;
		case PD_MEVENT:	/* Read/write event string */
			x = args[0] & 0xFF;
			if (x > ERRLEN) {
				args[0] = -1;
				break;
			}
			if (args[0] & 0xFF00) {
				if (event) {
					event[x] = args[1];
				}
			} else {
				if (event) {
					args[1] = event[x];
				} else {
					args[1] = 0;
				}
			}
			break;
		default:	/* Bogus--drop him */
			v_sema(&pr->p_sema);
			(void)msg_disconnect(port);
			p_sema(&p->p_sema, PRIHI);
			if (p->p_dbg.pd_port == port) {
				bzero(&p->p_dbg, sizeof(struct pdbg));
			}
			v_sema(&p->p_sema);
			return;
		}
	}
}

#endif /* PROC_DEBUG */
