/*
 * errno.c
 *	Compatibility wrapper mapping VSTa errors into old-style values
 *
 * We dance a fine line here, in which we attempt to detect when true,
 * new errors have come up from the kernel, while preserving errno
 * when such has not occurred.
 */
#include <sys/types.h>
#include <errno.h>
#include <std.h>

static struct {
	int errnum;
	char *errstr;
} errmap[] = {
	{ EPERM, "perm" },
	{ ENOENT, "no entry" },
	{ EINTR, "intr" },
	{ EIO, "io err" },
	{ ENXIO, "no io" },
	{ E2BIG, "too big" },
	{ EBADF, "bad file" },
	{ EAGAIN, "again" },
	{ ENOMEM, "no mem" },
	{ EFAULT, "fault" },
	{ EBUSY, "busy" },
	{ EEXIST, "exists" },
	{ ENOTDIR, "not dir" },
	{ EINVAL, "invalid" },
	{ ENOSPC, "no space" },
	{ EROFS, "RO fs" },
	{ EXDEV, "cross dev"},
	{ EISDIR, "is dir"},
	{ 0, 0 }
};

/*
 * map_errstr()
 *	Given VSTa error string, turn into POSIX errno-type value
 */
static int
map_errstr(char *err)
{
	int x;
	char *p;

	/*
	 * Scan for string match
	 */
	for (x = 0; p = errmap[x].errstr; ++x) {
		if (!strcmp(err, p)) {
			return(errmap[x].errnum);
		}
	}

	/*
	 * Didn't find, just pick something
	 */
	return(EINVAL);
}

/*
 * map_errno()
 *	Given POSIX errno-type value, return a pointer to a VSTa error string
 */
static char *
map_errno(int err)
{
	static char errdef[] = "unknown error";
	int x;
	int e;

	/*
	 * Scan for string match
	 */
	for (x = 0; e = errmap[x].errnum; ++x) {
		if (e == err) {
			return(errmap[x].errstr);
		}
	}

	/*
	 * Didn't find, just pick something
	 */
	return(errdef);
}

/*
 * __ptr_errno()
 *	Report back the address of errno
 *
 * In order to keep the POSIX errno in step with the VSTa system error
 * message code we need to do some pretty nasty hacks to keep track of what
 * the "errno" value was when we last entered the routine.  If it's changed,
 * either because a new errno has been set, or because a new __err has been
 * set we do what we can to put things back in sync
 */
int *
__ptr_errno(void)
{
	static uint old_errcnt;
	static int old_errno;
	extern int _errno;
	extern uint _errcnt;
	char *p;

	/*
	 * First we want to know if someone has modified errno.  If they
	 * have, we want to put things back in sync
	 */
	if (old_errno != _errno) {
		__seterr(map_errno(_errno));
		old_errno = _errno;
		return(&_errno);
	}

	/*
	 * Has the system error string been updated?  If not, then we've
	 * already done everything before and simply return the old answer
	 */
	if (old_errcnt == _errcnt) {
		return(&_errno);
	}
	old_errcnt = _errcnt;

	/*
	 * Get current error.  Don't touch anything if there is none.
	 */
	p = strerror();
	if (!p) {
		return(&_errno);
	}

	/*
	 * Map from string to a value.
	 */
	_errno = map_errstr(p);

	return(&_errno);
}
