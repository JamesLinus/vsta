/*
 * syscall.c
 *	C code to do a little massaging before firing the "true" syscall
 */
#include <sys/types.h>
#include <sys/fs.h>

/*
 * msg_err()
 *	Count string length, then invoke system call
 *
 * It's a lot harder to do in kernel mode, so do it in the system
 * call layer.
 */
msg_err(long port, char *errmsg)
{
	extern int _msg_err();

	return(_msg_err(port, errmsg, strlen(errmsg)));
}

/*
 * notify()
 *	Similarly for notify
 */
notify(long pid, long tid, char *event)
{
	extern int _notify();

	return(_notify(pid, tid, event, strlen(event)));
}

/*
 * exit()
 *	Flush I/O buffers, then _exit()
 */
void
exit(int val)
{
	extern int _exit();
	extern void __allclose();

	__allclose();
	_exit(val);
}

/*
 * abort()
 *	Croak ourselves
 */
void
abort(void)
{
	extern int notify();

	/*
	 * First send a generic event.  Follow with non-blockable death.
	 */
	notify(0L, 0L, "abort");
	notify(0L, 0L, EKILL);
	/*NOTREACHED*/
}
