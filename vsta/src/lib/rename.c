/*
 * rename.c
 *	Implementation of rename() function
 *
 * In VSTa, renaming is done by opening the directories containing
 * the targets, sending an FS_RENAME to the source dir specifying
 * the source name, and a corresponding FS_RENAME to the destination
 * dir, specifying the destination name.
 */
#include <sys/fs.h>
#include <std.h>
#include <fcntl.h>
#include <alloc.h>

/*
 * Not reentrant XXX
 */
static int srcfd = -1, destfd = -1;
static char *srcent, *destent;

/*
 * getpath()
 *	Access named path, return various parts of it
 *
 * If needed, convert CWD-relative to absolute.  Open a port to the
 * named containing directory, and return a pointer to the element
 * within this directory, as well as the port_name of the server for
 * the containing directory.
 *
 * Return 0 on success, non-zero on failure.
 */
static int
getpath(char *path, int *fdp, port_name *namep, char **name)
{
	char *p, *d;
	extern char *__cwd;

	p = strrchr(path, '/');
	if (p == 0) {
		/*
		 * If we don't have a name for the directory, build it using
		 * the CWD.
		 */
		*name = path;
		d = __cwd;
	} else {
		/*
		 * Otherwise trim off the last part
		 */
		*name = p+1;
		d = alloca(strlen(path)+1);
		strcpy(d, path);
		d[p - path] = '\0';
	}

	/*
	 * Open access to the directory
	 */
	*fdp = open(d, O_RDWR);
	if (*fdp < 0) {
		return(1);
	}

	/*
	 * Get port_name of server
	 */
	*namep = msg_portname(__fd_port(*fdp));

	/*
	 * Success
	 */
	return(0);
}

/*
 * msg()
 *	Send a message
 */
static int
msg(int fd, int type, int arg, char *buf)
{
	struct msg m;

	/*
	 * Build the message
	 */
	m.m_op = type;
	m.m_arg = arg;
	m.m_arg1 = getpid();
	m.m_nseg = 1;
	m.m_buf = buf;
	m.m_buflen = strlen(buf)+1;

	/*
	 * Off it goes
	 */
	return(msg_send(__fd_port(fd), &m));
}

/*
 * request()
 *	Send off the request
 */
static void
request(void)
{
	/*
	 * Send a message requesting a rename start
	 */
	(void)msg(srcfd, FS_RENAME, 0, srcent);
	_exit(0);
}

/*
 * rename()
 *	Request rename of object within a server
 */
int
rename(char *src, char *dest)
{
	int err;
	char *p;
	port_name srcname, destname;

	/*
	 * Explode src/dest paths into needed elements
	 */
	if (getpath(src, &srcfd, &srcname, &srcent)) {
		return(-1);
	}
	if (getpath(dest, &destfd, &destname, &destent)) {
		err = -1;
		goto out;
	}

	/*
	 * Can't do this across two distinct servers
	 */
	if (srcname != destname) {
		err = -1;
		__seterr(EXDEV);
		goto out;
	}

	/*
	 * Launch a thread to start the request.  We will do the
	 * matching rename.
	 */
	if (tfork(request) < 0) {
		return(-1);
	} else {
		__msleep(10);	/* Let'em get set */
	}

	/*
	 * Now connect with the destination
	 */
	if (msg(destfd, FS_RENAME, 1, destent)) {
		err = -1;
		goto out;
	}

	/*
	 * Success
	 */
	err = 0;
out:
	if (srcfd != -1) {
		close(srcfd);
	}
	if (destfd != -1) {
		close(destfd);
	}
	return(err);
}
