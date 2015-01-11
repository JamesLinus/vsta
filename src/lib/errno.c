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

/*
 * sys_errlist[] emulation, filled in on demand
 */
static const char **errlist;
static int nerr;

/*
 * Our default error value
 */
static char errdef[] = "unknown error";

static struct {
	int errnum;
	char *errstr;
} errmap[] = {
	{ 0, "" },
	{ EPERM, "perm" },
	{ ENOENT, "no file" },
	{ ESRCH, "no entry" },
	{ EINTR, "intr" },
	{ EIO, "io err" },
	{ ENXIO, "no io" },
	{ E2BIG, "too big" },
	{ ENOEXEC, "exec fmt" },
	{ EBADF, "bad file" },
	{ ECHILD, "no child" },
	{ EAGAIN, "again" },
	{ ENOMEM, "no mem" },
	{ EACCES, "access" },
	{ EFAULT, "fault" },
	{ ENOTBLK, "not blk dev" },
	{ EBUSY, "busy" },
	{ EEXIST, "exists" },
	{ EXDEV, "cross dev link" },
	{ ENODEV, "not dev" },
	{ ENOTDIR, "not dir" },
	{ EISDIR, "is dir" },
	{ EINVAL, "invalid" },
	{ ENFILE, "file tab ovfl" },
	{ EMFILE, "too many files" },
	{ ENOTTY, "not tty" },
	{ ETXTBSY, "txt file busy" },
	{ EFBIG, "file too large" },
	{ ENOSPC, "no space" },
	{ ESPIPE, "ill seek" },
	{ EROFS, "RO fs" },
	{ EMLINK, "too many links" },
	{ EPIPE, "broken pipe" },
	{ EDOM, "math domain" },
	{ ERANGE, "math range" },
	{ EMATH, "math" },
	{ EILL, "ill instr" },
	{ EKILL, "kill" },
	{ EBALIGN, "blk align" },
	{ ESYMLINK, "symlink" },
	{ ELOOP, "symlink loop" },
	{ ENOSYS, "no sys" },
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
 * __map_errno()
 *	Given POSIX errno-type value, return a pointer to a VSTa error string
 */
char *
__map_errno(int err)
{
	int x;
	char *e;

	/*
	 * Scan for string match
	 */
	for (x = 0; e = errmap[x].errstr; ++x) {
		if (errmap[x].errnum == err) {
			return(e);
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
 * either because a new errno has been set, or because a new kernel error has
 * been set we do what we can to put things back in sync
 */
int *
__ptr_errno(void)
{
	extern int _old_errno;
	extern int _errno;
	char *p;

	/*
	 * First we want to know if someone has modified errno.  If they
	 * have, we want to put things back in sync
	 */
	if (_old_errno != _errno) {
		__seterr(__map_errno(_errno));
		return(&_errno);
	}

	/*
	 * Get current error.
	 */
	p = strerror();

	/*
	 * Map from string to a value.
	 */
	_old_errno = _errno = map_errstr(p);

	return(&_errno);
}

/*
 * init_errlist()
 *	Create sys_errlist[] array, and sys_nerr value
 */
static void
init_errlist(void)
{
	int x;

	/*
	 * Get highest errno in array
	 */
	for (x = 1; errmap[x].errstr; ++x) {
		if (errmap[x].errnum > nerr) {
			nerr = errmap[x].errnum;
		}
	}

	/*
	 * Allocate a sys_errlist[] for this size.  Initialize
	 * all locations to the default error string.
	 */
	errlist = malloc(nerr * sizeof(char *));
	for (x = 0; x < nerr; ++x) {
		errlist[x] = errdef;
	}

	/*
	 * For each known errno mapping, point at the appropriate
	 * string value.
	 */
	for (x = 1; errmap[x].errstr; ++x) {
		errlist[errmap[x].errnum] = errmap[x].errstr;
	}
}

/*
 * __get_errlist()
 *	Return pointer to sys_errlist[]
 *
 * Most applications won't use this; we create this array on first
 * reference.
 */
const char **
__get_errlist(void)
{

	/*
	 * If we've built it, return the pointer
	 */
	if (!errlist) {
		init_errlist();
	}
	return(errlist);
}

/*
 * __get_nerr()
 *	Return highest index in sys_errlist[]
 */
int
__get_nerr(void)
{
	if (!errlist) {
		init_errlist();
	}
	return(nerr);
}
