/*
 * stat.c
 *	All the stat-like functions
 */
#include <sys/param.h>
#include <sys/fs.h>
#include <stat.h>
#include <std.h>
#include <fcntl.h>

/*
 * fieldval()
 *	Given "statbuf" format buffer, extract a field value
 */
static char *
fieldval(char *statbuf, char *field)
{
	int len;
	char *p, *pn;

	len = strlen(field);
	for (p = statbuf; p; p = pn) {
		/*
		 * Carve out next field
		 */
		pn = strchr(p, '\n');
		if (pn) {
			++pn;
		}

		/*
		 * See if we match
		 */
		if (!strncmp(p, field, len)) {
			if (p[len] == '=') {
				return(p+len+1);
			}
		}
	}
	return(0);
}

/*
 * rstat()
 *	Read stat strings from a port
 *
 * If the given string is non-null, only the value of the named field is
 * returned.  Otherwise all fields in "name=value\n" format are returned.
 *
 * The buffer returned is static and overwritten by each call.
 */
char *
rstat(port_t fd, char *field)
{
	struct msg m;
	static char statbuf[MAXSTAT];
	char *p, *q;

	/*
	 * Send a stat request
	 */
	m.m_op = FS_STAT|M_READ;
	m.m_buf = statbuf;
	m.m_arg = m.m_buflen = MAXSTAT;
	m.m_arg1 = 0;
	m.m_nseg = 1;
	if (msg_send(fd, &m) <= 0) {
		return(0);
	}

	/*
	 * No field--return whole thing
	 */
	if (!field) {
		return(statbuf);
	}

	/*
	 * Hunt for named field
	 */
	p = fieldval(statbuf, field);
	if (p == 0) {
		return(0);
	}

	/*
	 * Make it a null-terminated string
	 */
	q = strchr(p, '\n');
	if (q) {
		*q = '\0';
	}
	return(p);
}

/*
 * wstat()
 *	Write a string through the stat function
 */
wstat(port_t fd, char *field)
{
	struct msg m;

	m.m_op = FS_WSTAT;
	m.m_buf = field;
	m.m_arg = m.m_buflen = strlen(field);
	m.m_arg1 = 0;
	m.m_nseg = 1;
	return(msg_send(fd, &m));
}

/*
 * field()
 *	Parse a x/y/z field and allow array-type access
 */
static
field(char *str, int idx)
{
	int x;
	char *p;

	/*
	 * Walk forward to the n'th field
	 */
	p = str;
	for (x = 0; x < idx; ++x) {
		p = strchr(p, '/');
		if (!p) {
			return(0);
		}
		++p;
	}

	/*
	 * Return value.  Following '/' or '\0' will stop atoi()
	 */
	return(atoi(p));
}

/*
 * modes()
 *	Convert the <sys/fs.h> access bits into the stat.h ones
 */
static
modes(int v)
{
	int x = 0;

	if (v & ACC_READ) x |= S_IREAD;
	if (v & ACC_WRITE) x |= S_IWRITE;
	if (v & ACC_EXEC) x |= S_IEXEC;
	return(x);
}

/*
 * fstat()
 *	Stat an open file
 */
fstat(int fd, struct stat *s)
{
	char *sbuf, *p;
	int mode;
	port_t port;

	if ((port = __fd_port(fd)) < 0) {
		return(-1);
	}
	sbuf = rstat(port, (char *)0);
	if (!sbuf) {
		return(-1);
	}

#define F(stfield, name, defvalue) \
	p = fieldval(sbuf, name); \
	if (p) s->stfield = atoi(p); \
	else s->stfield = defvalue;

	/*
	 * Do translation of simple numeric fields
	 */
	F(st_dev, "dev", 0);
	F(st_ino, "inode", 1);
	F(st_nlink, "links", 1);
	F(st_rdev, "dev", 0);
	F(st_size, "size", 0);
	F(st_atime, "atime", 0);
	F(st_mtime, "mtime", 0);
	F(st_ctime, "ctime", 0);
	F(st_blksize, "block", NBPG);

	/*
	 * Set UID/GID
	 */
	p = fieldval(sbuf, "owner");
	if (p) {
		s->st_gid = field(p, 0);
		s->st_uid = field(p, 1);
	} else {
		s->st_gid = s->st_uid = 0;
	}

	/*
	 * Decode "type"
	 */
	p = fieldval(sbuf, "type");
	if (!p) {
		mode = S_IFREG;
	} else if (!strcmp(p, "d")) {
		mode = S_IFDIR;
	} else if (!strcmp(p, "c")) {
		mode = S_IFCHR;
	} else if (!strcmp(p, "b")) {
		mode = S_IFBLK;
	} else if (!strcmp(p, "fifo")) {
		mode = S_IFIFO;
	} else {
		mode = S_IFREG;
	}

	/*
	 * Map the default access fields into "other"
	 */
	p = fieldval(sbuf, "perm");
	if (p) {
		mode |= modes(field(p, 0));
		mode |= modes(field(p, 1)) << 3;
		mode |= modes(field(p, 2)) << 6;
		s->st_mode = mode;
	} else {
		s->st_mode |= ((S_IREAD|S_IWRITE) << 6);
	}
	return(0);
}

/*
 * stat()
 *	Open file and get its fstat() information
 */
stat(char *f, struct stat *s)
{
	int fd, x;

	fd = open(f, O_READ);
	if (fd < 0) {
		return(-1);
	}
	x = fstat(fd, s);
	close(fd);
	return(x);
}

/*
 * __isatty()
 *	Internal version which works on a port, not a fd
 */
__isatty(port_t port)
{
	char *p;

	p = rstat(port, "type");
	if (p && (p[0] == 'c') && (p[1] == '\0')) {
		return(1);
	}
	return(0);
}

/*
 * isatty()
 *	Tell if given port talks to a TTY-like device
 */
isatty(int fd)
{
	port_t port;

	if ((port = __fd_port(fd)) < 0) {
		return(0);
	}
	return(__isatty(port));
}
