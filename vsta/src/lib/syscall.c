/*
 * syscall.c
 *	C code to do a little massaging before firing the "true" syscall
 */
#include <sys/types.h>
#include <sys/fs.h>
#include <sys/sched.h>

extern pid_t _getid(int);
char *strerror(void);

char __err[ERRLEN] = "";	/* Latest error string */
int _errno = 0;			/* Simulation for POSIX errno */
int _old_errno = 0;		/* Last used value of the POSIX errno */
int _err_sync = 0;		/* Used to sync errors in kernel and libc */
#define NEXIT (32)		/* # atexit() entries permitted */
static voidfun atexits[NEXIT];


/*
 * msg_err()
 *	Count string length, then invoke system call
 *
 * It's a lot harder to do in kernel mode, so count length in the system
 * call layer.
 */
msg_err(long port, const char *errmsg)
{
	extern int _msg_err();

	return(_msg_err(port, errmsg, strlen(errmsg)));
}

/*
 * notify()
 *	Similarly for notify
 */
int
notify(long pid, long tid, const char *event)
{
	int x;
	extern int _notify();
	extern void __msleep();

	for (;;) {
		x = _notify(pid, tid, event, strlen(event));
		if ((x >= 0) || strcmp(strerror(), EAGAIN)) {
			return(x);
		}
		__msleep(40);

	}
}

/*
 * exit()
 *	Flush I/O buffers, then _exit()
 */
void volatile
exit(int val)
{
	int x;
	extern void volatile _exit(int);
	extern void __allclose();

	__allclose();
	for (x = 0; x < NEXIT; ++x) {
		voidfun f = atexits[x];
		if (f) {
			(*f)();
		}
	}
	for (;;) {
		_exit(val & 0xFF);
	}
}

/*
 * atexit()
 *	Maintain list of things to do on our way out
 */
int
atexit(voidfun f)
{
	int x, fidx = -1;
	voidfun f2;

	/*
	 * Only put something on the list once
	 */
	for (x = 0; x < NEXIT; ++x) {
		f2 = atexits[x];
		if (f2 == f) {
			return(0);
		}

		/*
		 * Note a free slot
		 */
		if (!f2) {
			fidx = x;
		}
	}

	/*
	 * No free slots--return an error
	 */
	if (fidx == -1) {
		return(__seterr(ENOMEM));
	}

	/*
	 * Record it and return success
	 */
	atexits[fidx] = f;
	return(0);
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
	/*
	 * This is a bit of a hack - we would include <errno.h>, but there
	 * are some #define'd constants with the same names, but different
	 * values as those in <sys/fs.h>
	 */
	extern int *__ptr_errno(void);

	/*
	 * We first check whether or not there's been an "errno" change
	 * since the last error string check/modification.  If there has,
	 * the new errno represents the latest error and should be used
	 */
	if (_errno != _old_errno) {
		int x = *__ptr_errno();
	} else if (_err_sync) {
		if (_strerror(__err) < 0) {
			strcpy(__err, "fault");
			_err_sync = 0;
		}
	}

	return(__err);
}
 
/*
 * __seterr()
 *	Set error string to given value
 *
 * Used by internals of C library to set our error state.  We then inform
 * the kernel to keep matters straight.
 */
__seterr(char *p)
{
	/*
	 * We need to make sure that the errno emulation won't become confused
	 * by what we're doing.  We also need to override any kernel messages
	 */
	_old_errno = _errno;
	_err_sync = 0;

	if (!p) {
		__err[0] = '\0';
	} else if (strlen(p) >= ERRLEN) {
		abort();
	} else {
		strcpy(__err, p);
	}

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

/*
 * mmap()
 */
void *
mmap(void *vaddr, ulong len, int prot, int flags, int fd, ulong offset)
{
	extern void *_mmap();

	return(_mmap(vaddr, len, prot, flags,
		fd ? __fd_port(fd) : 0, offset));
}

/*
 * vfork()
 *	Virtual memory efficient fork()
 *
 * Since we're already pretty efficient in this respect, we simply issue
 * a fork() call!
 */
int
vfork(void)
{
	return(fork());
}

/*
 * yield()
 *	Yield CPU, remaining runnable
 */
int
yield(void)
{
	sched_op(SCHEDOP_YIELD, 0);
	return(0);
}

/*
 * fork()
 *	Keep a tally count, so we can know when we're a new process
 */
uint fork_tally;
int
fork(void)
{
	fork_tally += 1;
	return(_fork());
}
