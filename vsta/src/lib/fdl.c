/*
 * fdl.c
 *	"File Descriptor Layer"
 *
 * Routines for mapping file descriptors to ports.
 *
 * This is needed for a couple reasons.  First, since we provide
 * POSIX-ish TTY semantics from user mode, we need to be able to
 * intercept read()/write()/etc. and do things in library code.
 * Second, our kernel implementation of ports assumes each port
 * is a seperate connection to a server, with its own file position
 * and such.  To get the semantics of a dup(2) syscall, we need to
 * be able to direct two file descriptors into the same port.
 *
 * Everything in computer science can be fixed with an extra layer
 * of indirection.  May as well make it live in user mode.
 */
#include <sys/fs.h>
#include <fdl.h>
#include <lib/hash.h>
#include <std.h>
#include <unistd.h>

/*
 * For saving state of fdl on exec()
 */
struct save_fdl {
	int s_fd;	/* File descriptor number */
	port_t s_port;	/* Its port */
	ulong s_pos;	/* Its position */
};

#define NFD (32)		/* # FD's directly mapped to ports */

/*
 * Mapping of first NFD of them, and hash for rest
 */
static struct port *fdmap[NFD];
static struct hash *fdhash = 0;
static int fdnext = NFD;

/* For advancing fdnext */
#define INC(v) {v += 1; if (v <= 0) { v = NFD; }}

/*
 * __port()
 *	Map a file descriptor value into a port
 *
 * We support an arbitrary number of file descriptors.  The first
 * NFD of them live in a simple array.  The rest are looked up
 * through a hash table.
 */
struct port *
__port(int fd)
{
	if (fd < NFD) {
		return(fdmap[fd]);
	}
	if (!fdhash) {
		return(0);
	}
	return(hash_lookup(fdhash, (ulong)fd));
}

/*
 * __fd_port()
 *	Map file descriptor to VSTa port value
 *
 * Used by outsiders who want to send their own messages
 */
port_t
__fd_port(int fd)
{
	struct port *port;

	if ((port = __port(fd)) == 0) {
		return(-1);
	}
	return(port->p_port);
}

/*
 * do_write()
 *	Build a write message for a generic port
 */
static
do_write(struct port *port, void *buf, uint nbyte)
{
	struct msg m;
	int x;

	m.m_op = FS_WRITE;
	m.m_buf = buf;
	m.m_buflen = nbyte;
	m.m_arg = nbyte;
	m.m_arg1 = 0;
	m.m_nseg = 1;
	x = msg_send(port->p_port, &m);
	if (x > 0) {
		port->p_pos += x;
	}
	return(x);
}

/*
 * do_read()
 *	Build a read message for a generic port
 */
static
do_read(struct port *port, void *buf, uint nbyte)
{
	struct msg m;
	int x;

	m.m_op = FS_READ|M_READ;
	m.m_buf = buf;
	m.m_buflen = nbyte;
	m.m_arg = nbyte;
	m.m_arg1 = 0;
	m.m_nseg = 1;
	x = msg_send(port->p_port, &m);
	if (x > 0) {
		port->p_pos += x;
	}
	return(x);
}

/*
 * do_seek()
 *	Build a seek message
 */
static
do_seek(struct port *port, long off, int whence)
{
	struct msg m;
	ulong l;
	int x;

	switch (whence) {
	case SEEK_SET:
		l = off;
		break;
	case SEEK_CUR:
		l = port->p_pos + off;
		break;
	case SEEK_END:
		l = atoi(rstat(port->p_port, "size"));
		l += off;
		break;
	default:
		return(-1);
	}
	m.m_op = FS_SEEK;
	m.m_buflen = 0;
	m.m_arg = l;
	m.m_arg1 = 0;
	m.m_nseg = 0;
	x = msg_send(port->p_port, &m);
	if (x > 0) {
		port->p_pos = l;
	}
	return(x);
}

/*
 * do_close()
 *	Send a disconnect
 */
static
do_close(struct port *port)
{
	/*
	 * Disconnect from its server
	 */
	return(msg_disconnect(port->p_port));
}

/*
 * __do_open()
 *	Called after a successful FS_OPEN
 *
 * This routine is called after a port and file descriptor have been
 * set up.  It handles the wiring of the default read/write handlers
 * for TTYs and generic ports.
 */
__do_open(struct port *port)
{
	port->p_close = do_close;
	port->p_seek = do_seek;
	if ((port->p_port != (port_t)-1) && __isatty(port->p_port)) {
		extern int __tty_read(), __tty_write();

		port->p_read = __tty_read;
		port->p_write = __tty_write;
	} else {
		port->p_read = do_read;
		port->p_write = do_write;
	}
	port->p_pos = 0L;
	return(0);
}

/*
 * setfd()
 *	Given a file descriptor #, attach to port
 */
static void
setfd(int fd, struct port *port)
{
	if (fd < 0) {
		abort();
	}
	if (fd >= NFD) {
		if (fdhash == 0) {
			fdhash = hash_alloc(NFD);
			if (!fdhash) {
				abort();
			}
		}
		(void)hash_insert(fdhash, fd, port);
	} else {
		fdmap[fd] = port;
	}
}

/*
 * allocfd()
 *	Allocate next free file descriptor value
 */
static
allocfd(void)
{
	int x;

	/*
	 * Scan low slots
	 */
	for (x = 0; x < NFD; ++x) {
		if (fdmap[x] == 0) {
			break;
		}
	}

	/*
	 * If no luck there, get a high slot
	 */
	if (x >= NFD) {
		/*
		 * No high values ever used, so we know it's free.
		 * Otherwise scan.
		 */
		if (fdhash) {
			/*
			 * Scan until we find an open value
			 */
			while (hash_lookup(fdhash, fdnext)) {
				INC(fdnext);
			}
		}
		x = fdnext; INC(fdnext);
	}
	return(x);
}

/*
 * fd_alloc()
 *	Allocate the next file descriptor
 *
 * If possible, allocate from the first NFD.  Otherwise create the
 * hash table on first allocation, and allocate from there.
 */
__fd_alloc(port_t portnum)
{
	struct port *port;
	int x;

	/*
	 * Allocate the port data structure
	 */
	if ((port = malloc(sizeof(struct port))) == 0) {
		return(0);
	}

	/*
	 * Get corresponding file descriptor
	 */
	x = allocfd();
	setfd(x, port);

	/*
	 * Fill in the port, add to the hash
	 */
	port->p_port = portnum;
	port->p_data = 0;
	port->p_refs = 1;
	__do_open(port);
	return(x);
}

/*
 * dup()
 *	Duplicate file descriptor to higher port #
 */
dup(int fd)
{
	int fdnew;
	struct port *port;

	/*
	 * Get handle to existing port
	 */
	port = __port(fd);
	if (port == 0) {
		__seterr(EBADF);
		return(-1);
	}

	/*
	 * Get new file descriptor value
	 */
	fdnew = allocfd();

	/*
	 * Map to existing port, as a new reference
	 */
	setfd(fdnew, port);
	port->p_refs += 1;

	return(fdnew);
}

/*
 * read()
 *	Do a read() "syscall"
 */
read(int fd, void *buf, uint nbyte)
{
	struct port *port;

	if ((port = __port(fd)) == 0) {
		return(-1);
	}
	return((*(port->p_read))(port, buf, nbyte));
}

/*
 * write()
 *	Do a write() "syscall"
 */
write(int fd, void *buf, uint nbyte)
{
	struct port *port;

	if ((port = __port(fd)) == 0) {
		return(-1);
	}
	return((*(port->p_write))(port, buf, nbyte));
}

/*
 * lseek()
 *	Do a lseek() "syscall"
 */
lseek(int fd, ulong off, int whence)
{
	struct port *port;

	if ((port = __port(fd)) == 0) {
		return(-1);
	}
	return((*(port->p_seek))(port, off, whence));
}

/*
 * close()
 *	Close an open FD
 */
close(int fd)
{
	struct port *port;
	int error;

	/*
	 * Look up FD
	 */
	if ((port = __port(fd)) == 0) {
		return(-1);
	}

	/*
	 * Decrement refs, return if there are more
	 */
	port->p_refs -= 1;
	if (port->p_refs > 0) {
		return(0);
	}

	/*
	 * Delete from whatever slot it occupies
	 */
	if (fd < NFD) {
		fdmap[fd] = 0;
	} else {
		(void)hash_delete(fdhash, (ulong)fd);
	}

	/*
	 * Let the layer do stuff if it wishes
	 */
	error = (*(port->p_close))(port);

	/*
	 * Free the port memory
	 */
	free(port);

	return(error);
}

/*
 * __fdl_restore()
 *	Restore our fdl state from the array
 *
 * Only meant to be used during crt0 startup after an exec().
 */
char *
__fdl_restore(char *p)
{
	char *endp;
	uint x;
	struct port *port;
	struct save_fdl *s, *s2;
	ulong len;

	/*
	 * Extract length
	 */
	len = *(ulong *)p;
	endp = p + len;
	p += sizeof(ulong);

	while (p < endp) {
		/*
		 * Get next pair
		 */
		s = (struct save_fdl *)p;
		p += sizeof(struct save_fdl);
		if (s->s_fd == -1) {
			continue;
		}

		/*
		 * Allocate port structure
		 */
		port = malloc(sizeof(struct port));
		if (port == 0) {
			abort();
		}
		port->p_port = s->s_port;
		port->p_data = 0;
		port->p_refs = 1;
		port->p_pos = s->s_pos;

		/*
		 * Attach to this file descriptor
		 */
		setfd(s->s_fd, port);

		/*
		 * Now scan for all duplicate references, and
		 * map them to the same port.
		 */
		for (s2 = (struct save_fdl *)p; (char *)s2 < endp; ++s2) {
			if (s2->s_port == s->s_port) {
				setfd(s2->s_fd, port);
				s2->s_fd = -1;
				port->p_refs += 1;
			}
		}

		/*
		 * Let port initialize itself
		 */
		__do_open(port);
	}
	return(p);
}

/*
 * addfdl()
 *	Append a description of the file descriptor/port pair
 */
static
addfdl(long l, struct port *port, char **pp)
{
	struct save_fdl *s = (struct save_fdl *)*pp;

	s->s_fd = l;
	s->s_port = port->p_port;
	s->s_pos = port->p_pos;
	*pp += sizeof(struct save_fdl);
	return(0);
}

/*
 * __fdl_save()
 *	Save state into given byte area
 *
 * The area is assumed to be big enough; presumably they asked
 * __fdl_size() and haven't opened more stuff since.
 */
void
__fdl_save(char *p, ulong len)
{
	uint x;
	struct save_fdl *s;

	/*
	 * Store count first
	 */
	*(ulong *)p = len;
	p += sizeof(ulong);

	/*
	 * Assemble fdl stuff
	 */
	for (x = 0; x < NFD; ++x) {
		if (fdmap[x]) {
			s = (struct save_fdl *)p;
			s->s_fd = x;
			s->s_port = fdmap[x]->p_port;
			s->s_pos = fdmap[x]->p_pos;
			p += sizeof(struct save_fdl);
		}
	}
	if (fdhash) {
		hash_foreach(fdhash, addfdl, &p);
	}
}

/*
 * __fdl_size()
 *	Return bytes needed to save fdl state
 */
uint
__fdl_size(void)
{
	uint x, plen;

	/*
	 * Add in length of information for file descriptor layer
	 */
	plen = sizeof(ulong);
	for (x = 0; x < NFD; ++x) {
		if (fdmap[x]) {
			plen += sizeof(struct save_fdl);
		}
	}
	if (fdhash) {
		plen += (hash_size(fdhash) * sizeof(struct save_fdl));
	}
	return(plen);
}
