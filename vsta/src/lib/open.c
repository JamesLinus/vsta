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
#include <alloc.h>
#include <pwd.h>

#define MAXLINK (16)	/* Max levels of symlink to follow */
#define MAXSYMLEN (128)	/*  ...max length of one element */

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
static int
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
int
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
 * follow_symlink()
 *	Try to extract a symlink contents and build a new path
 *
 * Returns a new malloc()'ed  string containing the mapped path
 */
static char *
follow_symlink(port_t port, char *file, char *p)
{
	struct msg m;
	port_t port2;
	int x, len;
	char *newpath, *lenstr;

	/*
	 * Walk down a copy into the file so we don't lose
	 * our place in the path.
	 */
	port2 = clone(port);
	if (port2 < 0) {
		return(0);
	}
	m.m_op = FS_OPEN;
	m.m_buf = file;
	m.m_buflen = strlen(file)+1;
	m.m_nseg = 1;
	m.m_arg = ACC_READ | ACC_SYM;
	m.m_arg1 = 0;
	x = msg_send(port2, &m);

	/*
	 * If we can't get it, bail
	 */
	if (x < 0) {
		msg_disconnect(port2);
		return(0);
	}

	/*
	 * Calculate length, get a buffer to hold new path
	 */
	lenstr = rstat(port2, "size");
	if (lenstr == 0) {
		msg_disconnect(port2);
		return(0);
	}
	len = atoi(lenstr);
	if (len > MAXSYMLEN) {
		msg_disconnect(port2);
		return(0);
	}
	newpath = malloc(len + (p ? (strlen(p+1)) : 0) + 1);
	if (newpath == 0) {
		msg_disconnect(port2);
		return(0);
	}

	/*
	 * Read the contents
	 */
	m.m_op = FS_READ | M_READ;
	m.m_nseg = 1;
	m.m_buf = newpath;
	m.m_arg = m.m_buflen = len;
	m.m_arg1 = 0;
	x = msg_send(port2, &m);
	if (x < 0) {
		free(newpath);
		msg_disconnect(port2);
		return(0);
	}

	/*
	 * Tack on the remainder of the path
	 */
	if (p) {
		sprintf(newpath+len, "/%s", p+1);
	} else {
		newpath[len] = '\0';
	}

	/*
	 * There's your new path
	 */
	return(newpath);
}

/*
 * try_open()
 *	Given a root point and a path, try to walk into the mount
 *
 * Returns 1 on error, 0 on success.
 */
static int
try_open(port_t newfile, char *file, int mask, int mode)
{
	char *p;
	struct msg m;
	int x, nlink = 0;

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
		char *tmp;

		/*
		 * Find the next '/', or end of string
		 */
		while (*file == '/') {
			++file;
		}
		p = strchr(file, '/');
		if (p) {
			*p = '\0';
		}

		/*
		 * Map element to a getenv() of it if it  has
		 * a "@" prefix.
		 */
		tmp = 0;
		if (*file == '@') {
			tmp = getenv(file+1);
			if (tmp) {
				file = tmp;
			}
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
		x = msg_send(newfile, &m);
		if (tmp) {
			free(tmp);	/* Free env storage if any */
		}

		/*
		 * If we encounter a symlink, see about following it
		 */
		if ((x < 0) && !strcmp(strerror(), ESYMLINK)) {
			/*
			 * Cap number of levels of symlink we'll follow
			 */
			if (nlink++ >= MAXLINK) {
				__seterr(ELOOP);
				return(1);
			}

			/*
			 * Pull in contents of symlink, make that
			 * our new path to lookup
			 */
			tmp = follow_symlink(newfile, file, p);
			if (tmp) {
				file = alloca(strlen(tmp)+1);
				strcpy(file, tmp);
				free(tmp);
				continue;
			}
		}

		if (p) {
			*p++ = '/';	/* Restore path seperator */
		}
		if (x < 0) {		/* Return error if any */
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
 * do_home()
 *	Rewrite ~ to $HOME, and ~foo to foo's home dir
 *
 * Result is returned in a malloc()'ed buffer on success, 0 on failure
 */
static char *
do_home(char *in)
{
	char *p, *q, *name, *name_end;

	/*
	 * Find end of element, either first '/' or end of string.
	 * If '/', create private copy of name, null-terminated.
	 */
	p = strchr(in, '/');
	if (p == 0) {
		name = in;
		name_end = in + strlen(in);
	} else {
		name = alloca((p - in) + 1);
		bcopy(in, name, p - in);
		name[p - in] = '\0';
		name_end = p;
	}

	if (!strcmp(name, "~")) {
		static char *home;

		/*
		 * ~ -> $HOME.  Cache it for speed on reuse.
		 */
		if (home == 0) {
			home = getenv("HOME");
		}
		p = home;
	} else {
		struct passwd *pw;

		/*
		 * ~name -> $HOME for "name"
		 */


		/*
		 * Look it up in the password database
		 */
		pw = getpwnam(name+1);
		if (pw) {
			p = pw->pw_dir;
		} else {
			p = 0;
		}
	}

	/*
	 * If didn't find ~name's home or $HOME, return 0
	 */
	if (p == 0) {
		return(0);
	}

	/*
	 * malloc() result, assemble our replacement part and
	 * rest of original path
	 */
	q = malloc(strlen(p) + strlen(name_end) + 1);
	if (q == 0) {
		return(0);
	}
	sprintf(q, "%s%s", p, name_end);
	return(q);
}

/*
 * open()
 *	Open a file
 */
int
open(const char *file, int mode, ...)
{
	int x, len, mask;
	port_t newfile;
	char buf[MAXPATH], *p, *home_buf;
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
	 * Rewrite ~
	 */
	if (file[0] == '~') {
		home_buf = do_home((char *)file);
		if (home_buf) {
			file = home_buf;
		}
	} else {
		home_buf = 0;
	}

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
	 * Free $HOME processing buffer now that we've used it
	 */
	if (home_buf) {
		free(home_buf);
	}

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
			/*
			 * Don't allow, say, /vsta to match against /v in
			 * the mount table.  Require that the mount
			 * table entry /xyz match against /xyz/...
			 * Note special case for root mounts, where mount
			 * name ends with /, all other mount points end
			 * in non-/.
			 */
			if (((q[-1] == '/') && (r[-1] == '/')) ||
				((q[-1] != '/') && (r[0] == '/')))
			{
				if ((q - mt->m_name) > len) {
					len = q - mt->m_name;
					match = mt;
				}
				continue;
			}

		} else if (*q != *r) {
			/*
			 * Mismatch--ignore entry and continue scan
			 */
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
int
chdir(const char *newdir)
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
int
mkdir(const char *dir)
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
int
unlink(const char *path)
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

/*
 * creat()
 *	Create a file - basically a specialised version of open()
 */
int
creat(const char *path, int mode)
{
	return(open(path, (O_WRONLY | O_CREAT | O_TRUNC), mode));
}

/*
 * rmdir()
 *	Remove a directory
 */
int
rmdir(const char *olddir)
{
	int fd;
	char *p;

	/*
	 * Open entry
	 */
	fd = open(olddir, O_READ, 0);
	if (fd < 0) {
		return(-1);
	}

	/*
	 * See if it's a dir
	 */
	p = rstat(__fd_port(fd), "type");
	close(fd);
	if (!p || strcmp(p, "d")) {
		__seterr(ENOTDIR);
		return(-1);
	}
	return(unlink(olddir));
}
