/*
 * main.c
 *	Initial C code run during bootup
 */
#include <sys/mutex.h>

extern void init_machdep(), init_page(), init_qio(), init_sched(),
	init_proc(), init_swap(), swtch(), init_malloc(), init_msg();
#ifdef DEBUG
extern void init_debug();
#endif

extern lock_t runq_lock;

int upyet = 0;	/* Set to true once basic stuff initialized */

main(void)
{
	init_machdep();
	init_page();
	init_malloc();
#ifdef DEBUG
	init_debug();
#endif
	init_qio();
	init_sched();
	init_proc();
	init_msg();
	init_swap();

	/*
	 * Flag that we're up.  swtch() assumes runq_lock is held,
	 * so take it and fall into the scheduler.
	 */
	upyet = 1;
	(void)p_lock(&runq_lock, SPLHI);
	swtch();	/* Assumes curthread is 0 currently */
}
