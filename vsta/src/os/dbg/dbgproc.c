#ifdef DEBUG
/*
 * proc.c
 *	Routines to dump out processes
 */
#include <sys/proc.h>
#include <sys/thread.h>
#include <mach/setjmp.h>
#include <sys/mutex.h>

#define FLAG(v, s) if (f & v) { printf(" %s", s); }

extern char *nameval();
extern jmp_buf dbg_errjmp;
extern struct proc *allprocs;

/*
 * statename()
 *	Give printable string for t_stat field
 */
static char *
statename(s)
	int s;
{
	switch (s) {
	case TS_SLEEP: return("SLEEP");
	case TS_RUN: return("RUN");
	case TS_ONPROC: return("ONPROC");
	case TS_DEAD: return("DEAD");
	default: return("???");
	}
}

/*
 * dump_thread()
 *	Dump out the contents of a thread structure
 */
static void
dump_thread(struct thread *t, int brief)
{
	uint f;

	printf(" %d %s kstack %x uregs %x wchan %x %s/%s\n",
		t->t_pid, statename(t->t_state), t->t_kstack, t->t_uregs,
		t->t_wchan,
		t->t_evsys[0] ? t->t_evsys : "<none>",
		t->t_evproc[0] ? t->t_evproc : "<none>");
	if (brief) {
		return;
	}
	printf("  kregs %x proc %x ustack %x runq %x runticks %d\n",
		t->t_kregs, t->t_proc, t->t_ustack, t->t_runq, t->t_runticks);
	printf("  flags:"); f = t->t_flags;
	FLAG(T_RT, "RT"); FLAG(T_BG, "BG"); FLAG(T_KERN, "KERN");
	printf("\n  hd %x tl %d next %x msgwait %x qsav %x\n",
		t->t_hd, t->t_tl, t->t_next, &t->t_msgwait, t->t_qsav);
	printf("  probe %x err %s usr/sys %d/%d evq %x eng %x\n",
		t->t_probe,
		t->t_err[0] ? t->t_err : "<none>",
		t->t_syscpu, t->t_usrcpu,
		&t->t_evq, t->t_eng);
}

/*
 * dump_prot()
 *	Print out a protection structure
 */
static void
dump_prot(struct prot *prot)
{
	int x;

	printf("%x", prot->prot_default);
	for (x = 0; x < prot->prot_len; ++x) {
		printf(", %d+%x", prot->prot_id[x], prot->prot_bits[x]);
	}
	printf("\n");
}

/*
 * dump_ids()
 *	Dump out the IDs from a process
 */
static void
dump_ids(struct perm *perms)
{
	int x;

	for (x = 0; x < PROCPERMS; ++x, ++perms) {
		int y;

		if (perms->perm_len > PERMLEN) {
			continue;
		}
		for (y = 0; y < perms->perm_len; ++y) {
			if (y == 0) {
				printf(" ");
			} else {
				printf(".");
			}
			printf("%d", perms->perm_id[y]);
		}
	}
	printf("\n");
}

/*
 * dump_proc()
 *	Dump a proc structure
 */
static void
dump_proc(struct proc *p, int brief)
{
	int x;

	printf("%d\n", p->p_pid);
	if (brief) {
		return;
	}
	printf(" vas %x threads %x runq %x sys/usr %d/%d\n",
		p->p_vas, p->p_threads, p->p_runq, p->p_sys, p->p_usr);
	printf(" sema %x prefs %x nopen %d all %x handler %x\n",
		&p->p_sema, p->p_prefs, p->p_nopen, p->p_allnext,
		p->p_handler);
	printf(" pgrp 0x%x\n", p->p_pgrp);
	printf(" ports:");
	for (x = 0; x < PROCPORTS; ++x) {
		if (p->p_ports[x]) {
			printf(" %x", p->p_ports[x]);
		}
	}
	printf("\n open:");
	for (x = 0; x < PROCOPENS; ++x) {
		if (p->p_open[x]) {
			printf(" %x", p->p_open[x]);
		}
	}
	printf("\n prot: "); dump_prot(&p->p_prot);
	printf(" ids:"); dump_ids(p->p_ids);
}

/*
 * dbgpfind()
 *	Private version which doesn't do locking
 *
 * The "real" version can even sleep (!)
 */
static struct proc *
dbgpfind(ulong pid)
{
	struct proc *p;

	for (p = allprocs; p; p = p->p_allnext) {
		if (p->p_pid == pid) {
			return(p);
		}
	}
	return(0);
}

/*
 * dump_procs()
 *	Dump a particular process, or all of them
 */
void
dump_procs(arg)
	char *arg;
{
	struct proc *p;
	struct thread *t;
	int pid;

	/*
	 * Dump all in brief format
	 */
	if (!arg || !arg[0]) {
		for (p = allprocs; p; p = p->p_allnext) {
			dump_proc(p, 1);
			for (t = p->p_threads; t; t = t->t_next) {
				dump_thread(t, 1);
			}
		}
		return;
	}

	/*
	 * Dump one in extended format
	 */
	pid = atoi(arg);
	p = dbgpfind(pid);
	if (!p) {
		printf("No such process %d\n", pid);
		return;
	}
	dump_proc(p, 0);
	for (t = p->p_threads; t; t = t->t_next) {
		dump_thread(t, 0);
	}
	v_sema(&p->p_sema);
}

#endif /* DEBUG */
