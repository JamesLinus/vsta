/*
 * open.c
 *	Our fancy routines to open a file
 *
 * Unlike UNIX, and like Plan9, we walk our way down the directory
 * tree manually.
 */
#include <sys/fs.h>
#include <std.h>
#include <mnttab.h>
#include <fcntl.h>

static char default_wd[] = "/";

char *__cwd =		/* Path to current directory */
	default_wd;
extern struct mnttab	/* List of mounted paths */
	*__mnttab;
extern int __nmnttab;	/*  ...# elements in __mnttab */

/*
 * mapmode()
 *	Convert to <sys/fs.h> format of modes
 */
static
mapmode(int mode)
{
	int m = 0;

	if (mode & O_READ) m |= ACC_READ;
	if (mode & O_WRITE) m |= ACC_WRITE;
	if (mode & O_CREAT) m |= ACC_CREATE;
	if (mode & O_DIR) m |= ACC_DIR;
	return(m);
}

/*
 * __dotdot()
 *	Remove ".."'s from path
 *
 * Done here because our file servers don't have our context
 * for interpretation.  Assumes path is in absolute format, with
 * a leading '/'.
 *
 * Also takes care of "."'s.
 */
__dotdot(char *path)
{
	int nelem = 0, x, had_dot = 0;
	char **elems = 0, *p = path;

	/*
	 * Map elements into vector
	 */
	while (p = strchr(p, '/')) {
		*p++ = '\0';
		nelem += 1;
		elems = realloc(elems, nelem*sizeof(char *));
		if (!elems) {
			return(1);
		}
		elems[nelem-1] = p;
	}

	/*
	 * Find ".."'s, and wipe them out
	 */
	x = 0;
	while (x < nelem) {
		/*
		 * Wipe out "." and empty ("//") elements
		 */
		if (!elems[x][0] || !strcmp(elems[x], ".")) {
			had_dot = 1;
			nelem -= 1;
			bcopy(elems+x+1, elems+x,
				(nelem-x)*sizeof(char *));
			continue;
		}

		/*
		 * If not "..", keep going
		 */
		if (strcmp(elems[x], "..")) {
			x += 1;
			continue;
		}

		/*
		 * Can't back up before root.  Just ignore.
		 */
		had_dot = 1;
		if (x == 0) {
			nelem -= 1;
			bcopy(elems+1, elems, nelem*sizeof(char *));
			continue;
		}

		/*
		 * Wipe out the "..", and the element before it
		 */
		nelem -= 2;
		x -= 1;
		bcopy(elems+x+2, elems+x, (nelem-x)*sizeof(char *));
	}

	/*
	 * No ".."'s--just put slashes back in place and return
	 */
	if (!had_dot) {
		for (x = 0; x < nelem; ++x) {
			elems[x][-1] = '/';
		}
		free(elems);
		return(0);
	}

	/*
	 * If no elements, always provide "/"
	 */
	if (nelem < 1) {
		*path++ = '/';
	} else {
		/*
		 * Rebuild path.  path[0] is already '\0' because of
		 * the requirement that all paths to __dotdot() be
		 * absolute and thus the first '/' was written to \0
		 * by the first loop of this routine.
		 */
		for (x = 0; x < nelem; ++x) {
			int len;

			*path++ = '/';
			len = strlen(elems[x]);
			bcopy(elems[x], path, len);
			path += len;
		}
	}
	*path = '\0';

	/*
	 * All done
	 */
	free(elems);
	return(0);
}

/*
 * try_open()
 *	Given a root point and a path, try to walk into the mount
 *
 * Returns 1 on error, 0 on success.
 */
static
try_open(port_t newfile, char *file, int mask, int mode)
{
	char *p;
	struct msg m;

	/*
	 * The mount point itself is a special case
	 */
	if (file[0] == '\0') {
		return(0);
	}

	/*
	 * Otherwise walk each element in the path
	 */
	do {
		/*
		 * Find the next '/', or end of string
		 */
		while (*file == '/') {
			++file;
		}
		p = strchr(file, '/');
		if (p) {
			*p++ = '\0';
		}

		/*
		 * Try to walk our file down to the new node
		 */
		m.m_op = FS_OPEN;
		m.m_buf = file;
		m.m_buflen = strlen(file)+1;
		m.m_nseg = 1;
		m.m_arg = p ? ACC_EXEC : mode;
		m.m_arg1 = p ? 0 : mask;
		if (msg_send(newfile, &m) < 0) {
			close(newfile);
			return(1);
		}

		/*
		 * Advance to next element
		 */
		file = p;
	} while (file);
	return(0);
}

/*
 * open()
 *	Open a file
 */
open(char *file, int mode, ...)
{
	int x, len, mask;
	port_t newfile;
	char buf[MAXPATH], *p;
	struct mnttab *mt, *match = 0;
	struct mntent *me;

	/*
	 * Before first mount, can't open anything!
	 */
	if (__mnttab == 0) {
		return(__seterr(ESRCH));
	}

	/*
	 * If O_CREAT, get mask.
	 * XXX this isn't very portable, but I HATE the varargs interface.
	 */
	if (mode & O_CREAT) {
		mask = *((&mode)+1);
	} else {
		mask = 0;
	}

	/*
	 * Map to <sys/fs.h> format for access bits
	 */
	mode = mapmode(mode);

	/*
	 * See where to start.  We always have to copy the string
	 * because "__dotdot" is going to write it, and the supplied
	 * string might be const, and thus perhaps not writable.
	 */
	if (file[0] == '/') {
		strcpy(buf, file);
	} else {
		sprintf(buf, "%s/%s", __cwd, file);
	}
	p = buf;

	/*
	 * Remove ".."s
	 */
	if (__dotdot(p)) {
		return(-1);
	}

	/*
	 * Scan for longest match in mount table
	 */
	len = 0;
	for (x = 0; x < __nmnttab; ++x) {
		char *q, *r;

		/*
		 * Scan strings until end or mismatch
		 */
		mt = &__mnttab[x];
		for (q = mt->m_name, r = p; *q && *r; ++q, ++r) {
			if (*q != *r) {
				break;
			}
		}

		/*
		 * Exact match--end now
		 */
		if (!r[0] && !q[0]) {
			len = strlen(mt->m_name);
			match = mt;
			break;
		}

		/*
		 * Mount path ended first.  If this is the longest
		 * match so far, record this as the current prefix
		 * directory.
		 */
		if (r[0] && !q[0]) {
			if ((q - mt->m_name) > len) {
				len = q - mt->m_name;
				match = mt;
			}
			continue;
		}

		/*
		 * Mismatch--ignore entry and continue scan
		 */
		if (*q != *r) {
			continue;
		}

		/*
		 * Else our target string ended first--ignore
		 * this element, since it isn't for us.
		 */
	}

	/*
	 * We now have our starting point.  Advance our target
	 * string to ignore the leading prefix.
	 */
	p += len;

	/*
	 * No matches--no hope of an open() succeeding
	 */
	if ((match == 0) || (match->m_entries == 0)) {
		return(__seterr(ESRCH));
	}

	/*
	 * Try each mntent under the chosen mnttab slot
	 */
	for (me = match->m_entries; me; me = me->m_next) {
		newfile = clone(me->m_port);
		if (try_open(newfile, p, mask, mode) == 0) {
			x = __fd_alloc(newfile);
			if (x < 0) {
				msg_disconnect(newfile);
				return(__seterr(ENOMEM));
			}
			return(x);
		}
		msg_disconnect(newfile);
	}

	/*
	 * The interaction with the port server will have set
	 * the error string already.
	 */
	return(-1);
}

/*
 * chdir()
 *	Change current directory
 */
chdir(char *newdir)
{
	char buf[MAXPATH], *p;
	int fd;

	/*
	 * Get copy, flatten ".."'s out
	 */
	if (newdir[0] == '/') {
		strcpy(buf, newdir);
	} else {
		sprintf(buf, "%s/%s", __cwd, newdir);
	}
	__dotdot(buf);

#ifdef XXX
	/*
	 * Try to open it.
	 */
	fd = open(buf, O_READ);
	if (fd < 0) {
		return(-1);
	}

	/*
	 * Make sure it's a directory-like substance
	 */
	{
		extern char *rstat();

		p = rstat(__fd_port(fd), "type");
	}
	if (!p || strcmp(p, "d")) {
		return(-1);
	}
	close(fd);
#endif

	/*
	 * Looks OK.  Move to this dir.
	 */
	p = strdup(buf);
	if (!p) {
		return(__seterr(ENOMEM));
	}
	if (__cwd != default_wd) {
		free(__cwd);
	}
	__cwd = p;
	return(0);
}

/*
 * mkdir()
 *	Create a directory
 */
mkdir(char *dir)
{
	int fd;

	fd = open(dir, O_CREAT|O_DIR);
	if (fd > 0) {
		close(fd);
		return(0);
	}
	return(-1);
}

/*
 * __cwd_size()
 *	Tell how many bytes to save cwd state
 */
long
__cwd_size(void)
{
	/*
	 * Length of string, null byte, and a leading one-byte count.
	 * If cwd is ever > 255, this would have to change.
	 */
	return(strlen(__cwd)+2);
}

/*
 * __cwd_save()
 *	Save cwd into byte stream
 */
void
__cwd_save(char *p)
{
	*p++ = strlen(__cwd)+1;
	strcpy(p, __cwd);
}

/*
 * __cwd_restore()
 *	Restore cwd from byte stream, return pointer to next data
 */
char *
__cwd_restore(char *p)
{
	int len;

	len = ((*p++) & 0xFF);
	__cwd = malloc(len);
	if (__cwd == 0) {
		abort();
	}
	bcopy(p, __cwd, len);
	return(p+len);
}

/*
 * getcwd()
 *	Get current working directory
 */
char *
getcwd(char *buf, int len)
{
	if (strlen(__cwd) >= len) {
		return(0);
	}
	strcpy(buf, __cwd);
	return(buf);
}

/*
 * unlink()
 *	Move down to dir containing entry, and try to remove it
 */
unlink(char *path)
{
	int fd, x, tries;
	char *dir, *file, buf[MAXPATH];
	struct msg m;

	/*
	 * Get writable copy of string, flatten out ".."'s and
	 * parse into a directory and filename.
	 */
	strcpy(buf, path);
	__dotdot(buf);
	file = strrchr(buf, '/');
	if (file) {
		dir = buf;
		*file++ = '\0';
	} else {
		dir = __cwd;
		file = buf;
	}

	/*
	 * Get access to the directory
	 */
	fd = open(dir, O_DIR|O_READ);
	if (fd < 0) {
		return(-1);
	}

	/*
	 * Ask to remove the filename within this dir.  EAGAIN can
	 * come back it we race with the a.out caching.
	 */
	for (tries = 0; tries < 3; ++tries) {
		m.m_op = FS_REMOVE;
		m.m_buf = file;
		m.m_buflen = strlen(file)+1;
		m.m_nseg = 1;
		m.m_arg = m.m_arg1 = 0;
		x = msg_send(__fd_port(fd), &m);
		if ((x >= 0) || strcmp(strerror(), EAGAIN)) {
			break;
		}
		__msleep(100);
	}

	/*
	 * Clean up and return results
	 */
	close(fd);
	return(x);
}
