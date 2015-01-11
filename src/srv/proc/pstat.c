/*
 * pstat.c
 *	Statistic handling functions
 */
#include "proc.h"
#include <sys/fs.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/perm.h>


/*
 * emulate_client_perms()]
 *	Simulate our client's permissions and disable our own
 *
 * We need to be able to start with the capability to dominate all of our
 * client's permissions in order to do this (in reality it means we need to
 * have <root> abilities
 */
void
emulate_client_perms(struct file *f)
{
	struct perm perm;
	int i;

	/*
	 * Attempt to get <root> permissions
	 */
	zero_ids(&perm, 1);
	perm.perm_len = 0;
	if (perm_ctl(1, &perm, (void *)0) < 0) {
		syslog(LOG_WARNING, "unable to activate <root> permission");
	}

	/*
	 * Now try and setup permissions equivalent to our client's
	 */
	if (f->f_nperm > PROCPERMS - 2) {
		f->f_nperm = PROCPERMS - 2;
		syslog(LOG_WARNING, "client has more than %d IDs",
		       PROCPERMS - 2);
	}
	for (i = 0; i < f->f_nperm; i++) {
		perm = f->f_perms[i];
		perm_ctl(i + 2, &perm, (void *)0);
	}
	
	/*
	 * Remove our default permissions, leaving only our client's
	 */
	for (i = 0; i < 2; i++) {
		perm_ctl(i, (void *)0, &perm);
		PERM_DISABLE(&perm);
		perm_ctl(i, &perm, (void *)0);
	}
}

/*
 * release_client_perms()]
 *	Release our client's permissions and re-enable our own
 */
void
release_client_perms(struct file *f)
{
	struct perm perm;
	int i;
	
	/*
	 * Get our initial ID and re-enable it
	 */
	perm_ctl(0, (void *)0, &perm);
	PERM_ENABLE(&perm);
	perm_ctl(0, &perm, (void *)0);

	/*
	 * Run through our other IDs and disable them all
	 */
	for (i = 1; i < f->f_nperm + 2; i++) {
		perm_ctl(i, (void *)0, &perm);
		PERM_DISABLE(&perm);
		perm_ctl(i, &perm, (void *)0);
	}
}

/*
 * proclist_pstat()
 *	Get the IDs of all processes we are allowed to see
 *
 * Return the number of process IDs we found
 */
int
proclist_pstat(struct file *f)
{
	int ret_val;

	emulate_client_perms(f);
	ret_val = pstat(PSTAT_PROCLIST, 0, f->f_proclist,
			NPROC * sizeof(pid_t));
	release_client_perms(f);
	
	return(ret_val);
}

/*
 * proc_pstat()
 *	Get the specified process' status information
 */
int
proc_pstat(struct file *f)
{
	int ret_val;

	emulate_client_perms(f);
	ret_val = pstat(PSTAT_PROC, (long)f->f_pid, &f->f_proc,
			sizeof(struct pstat_proc));
	release_client_perms(f);
	
	/*
	 * We want to sort out the protections for the pseudo file
	 * we have just "read".  We can't just use the process protection
	 * info as the rights are different, but we're close!
	 */
	f->f_prot = f->f_proc.psp_prot;
	f->f_prot.prot_bits[f->f_prot.prot_len - 1]
		= ACC_READ | ACC_WRITE | ACC_CHMOD;

	return(ret_val);
}

/*
 * kernel_pstat()
 *	Get the kernel statistics
 */
int
kernel_pstat(struct file *f)
{
	return(pstat(PSTAT_KERNEL, 0, &f->f_kern,
		     sizeof(struct pstat_kernel)));
}
