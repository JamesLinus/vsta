/*
 * syscall.c
 *	C code to do a little massaging before firing the "true" syscall
 */
#include <sys/types.h>
#include <sys/fs.h>

extern pid_t _getid(int);

char __err[ERRLEN];	/* Latest error string */

/*
 * msg_err()
 *	Count string length, then invoke system call
 *
 * It's a lot harder to do in kernel mode, so count length in the system
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
void volatile
exit(int val)
{
	extern void volatile _exit(int);
	extern void __allclose();

	__allclose();
	for (;;) {
		_exit(val & 0xFF);
	}
}

/*
 * abort()
 *	Croak ourselves
 */
void volatile
abort(void)
{
	extern int notify();

	/*
	 * First send a generic event.  Follow with non-blockable death.
	 */
	notify(0L, 0L, "abort");
	for (;;) {
		notify(0L, 0L, EKILL);
	}
}

/*
 * strerror()
 *	Get error string
 *
 * The error string comes from one of two places; if __err[] is
 * empty, we query the kernel.  Otherwise we use its current value.
 * This allows us to set a system error from the C library.  Since
 * we emulate much of what is traditionally kernel functionality in
 * the C library, it is necessary for us to be able to set "system"
 * errors.
 */
char *
strerror(void)
{

	if (__err[0] == '\0') {
		if ((_strerror(__err) < 0) || !__err[0]) {
			strcpy(__err, "unknown error");
		}
	}
	return(__err);
}

/*
 * __seterr()
 *	Set error string to given value
 *
 * Used by internals of C library to set our error without involving
 * the kernel.
 */
__seterr(char *p)
{
	if (strlen(p) >= ERRLEN) {
		abort();
	}
	strcpy(__err, p);
	return(-1);
}

/*
 * getpid()/gettid()/getppid()
 *	Get PID, TID, PPID
 */
pid_t
getpid(void)
{
	return(_getid(0));
}
pid_t
gettid(void)
{
	return(_getid(1));
}
pid_t
getppid(void)
{
	return(_getid(2));
}
