/*
 * main.c
 *	Initial C code run during bootup
 */
#include <sys/assert.h>
#include "../mach/mutex.h"

extern void init_machdep(), init_page(), init_qio(), init_sched(),
	init_proc(), init_swap(), swtch(), init_malloc(), init_msg();
extern void init_wire(), start_clock(), init_cons();
#ifdef KDB
extern void init_debug();
#endif

extern lock_t runq_lock;

int upyet = 0;	/* Set to true once basic stuff initialized */

main(void)
{
	init_machdep();
	init_page();
	init_malloc();
	init_cons();
#ifdef KDB
	init_debug();
#endif
	init_qio();
	init_sched();
	init_proc();
	init_msg();
	init_swap();
	init_wire();
	start_clock();

	/*
	 * Flag that we're up.  swtch() assumes runq_lock is held,
	 * so take it and fall into the scheduler.
	 */
	upyet = 1;
	p_lock_void(&runq_lock, SPLHI);
	swtch();	/* Assumes curthread is 0 currently */
	ASSERT_DEBUG(0, "main: swtch returned");
}
