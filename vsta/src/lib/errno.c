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
 * __ptr_errno()
 *	Report back the address of errno
 */
int *
__ptr_errno(void)
{
	static uint old_errcnt;
	static int my_errno;
	extern uint _errcnt;
	char *p;

	/*
	 * If no new errors, stick with what we have.  Otherwise record
	 * latest errno count, and continue.
	 */
	if (old_errcnt == _errcnt) {
		return(&my_errno);
	}
	old_errcnt = _errcnt;

	/*
	 * Get current error.  Don't touch anything if there is none.
	 */
	p = strerror();
	if (!p) {
		return(&my_errno);
	}

	/*
	 * Map from string to a value.
	 */
	my_errno = map_errstr(p);
	return(&my_errno);
}
