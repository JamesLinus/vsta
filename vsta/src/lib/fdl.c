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
#include <hash.h>
#include <std.h>
#include <string.h>
#include <unistd.h>

extern char *rstat(port_t, char *);
static off_t do_seek(struct port *, long, int);

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

#ifndef SRV
/*
 * __fd_iocount()
 *	Tell I/O count of given FDL port
 */
ulong
__fd_iocount(int fd)
{
	struct port *port;

	if ((port = __port(fd)) == 0) {
		return(-1);
	}
	return(port->p_iocount);
}

/*
 * __fd_set_iocount()
 *	Set I/O count value
 */
void
__fd_set_iocount(int fd, ulong val)
{
	struct port *port;

	if ((port = __port(fd)) == 0) {
		return;
	}
	port->p_iocount = val;
}

/*
 * balign_io()
 *	Try to do block I/O after running into alignment problems
 *
 * Try and work around with block aligned requests
 * instead.  We assume that blocks are arranged as powers of 2.
 * We also do this I/O slowly - one block at a time.  This is the
 * penalty we extract for having to do this at all!
 *
 * We attempt to manage requests to write non block-aligned data to servers
 * that only provide block access by running a read/modify/write cycle.
 * If by some strange quirk of fate you have write permissions, but no read
 * permissions then this doesn't work - you shouldn't be doing such antisocial
 * writes anyway!
 */
static int
balign_io(struct port *port, void *buf, uint nbyte, int do_read)
{
	int offs, x, apos, count, block_size, buf_st;
	char *blk_stat;
	uchar *abuf;
	struct msg m;
	
	/*
	 * Determine the block size to try to align to!  Assume
	 * 512 bytes unless given a definitive answer by the server
	 */
	blk_stat = rstat(port->p_port, "blksize");
	if (blk_stat) {
		block_size = atoi(blk_stat);
	} else {
		block_size = 512;
	}

	/*
	 * Get a working buffer
	 */
	if ((abuf = (uchar *)malloc(block_size * sizeof(uchar))) == 0) {
		return(x);
	}

	/*
	 * Keep track of where we're starting from
	 */
	apos = port->p_pos & ~(block_size - 1);
	count = apos - port->p_pos;
	buf_st = -count;

	do {
		if (do_read) {
			/*
			 * Read the aligned data
			 */
			m.m_op = FS_ABSREAD | M_READ;
			m.m_buf = abuf;
			m.m_buflen = block_size;
			m.m_arg = block_size;
			m.m_arg1 = apos;
			m.m_nseg = 1;
			port->p_iocount += 1;
			x = msg_send(port->p_port, &m);
			if (x < 0) {
				continue;
			}

			offs = (count >= 0) ? count : 0;
			count += x;
			if (count > nbyte) {
				count = nbyte;
			} else if (count < 0) {
				/*
				 * Shouldn't be able to happen so flag error
				 */
				x = -1;
				continue;
			}

			/*
			 * Copy the relevant non-aligned data into the
			 * requester's buffer
			 */
			bcopy(&abuf[buf_st], &((uchar *)buf)[offs],
				count - offs);
		} else {
			/*
			 * Only read the data if we need to modify
			 * it and write it back - if the next write is
			 * a complete block don't bother
			 */
			if ((count < 0) || (count + block_size > nbyte)) {
				/*
				 * Read the aligned data
				 */
				m.m_op = FS_ABSREAD | M_READ;
				m.m_buf = abuf;
				m.m_buflen = block_size;
				m.m_arg = block_size;
				m.m_arg1 = apos;
				m.m_nseg = 1;
				port->p_iocount += 1;
				x = msg_send(port->p_port, &m);
				if (x < 0) {
					continue;
				}

				offs = (count >= 0) ? count : 0;
				count += x;
				if (count > nbyte) {
					count = nbyte;
				} else if (count < 0) {
					/*
					 * Shouldn't be able to happen
					 * so flag error
					 */
					x = -1;
					continue;
				}
			} else {
				/*
				 * Fake that we read a block
				 */
				offs = count;
				count += block_size;
				x = block_size;
			}

			/*
			 * Copy the relevant non-aligned data from the
			 * requester's buffer
			 */
			bcopy(&((uchar *)buf)[offs], &abuf[buf_st],
			       count - offs);

			/*
			 * Write the now aligned data
			 */
			m.m_op = FS_ABSWRITE;
			m.m_buf = abuf;
			m.m_buflen = block_size;
			m.m_arg = block_size;
			m.m_arg1 = apos;
			m.m_nseg = 1;
			port->p_iocount += 1;
			x = msg_send(port->p_port, &m);
			if (x < 0) {
				continue;
			}
		}
		buf_st = 0;
		apos += block_size;
	} while ((count < nbyte) && (x > 0));
		
	/*
	 * Completed the I/O, free up the buffer and re-establish
	 * where we think we should be!
	 */
	free(abuf);
	if (count < 0) {
		count = 0;
	}
	port->p_pos += count;
	do_seek(port, port->p_pos, SEEK_SET);

	if (count > 0) {
		x = count;
	}
	return(x);
}
#endif /* !SRV */

/*
 * do_io()
 *	Read vs. write is virtually identical; share the code
 */
inline static int
do_io(int op, struct port *port, void *buf, uint nbyte)
{
	struct msg m;
	int x;

	/*
	 * Send off request
	 */
	m.m_op = op;
	m.m_buf = buf;
	m.m_buflen = nbyte;
	m.m_arg = nbyte;
	m.m_arg1 = 0;
	m.m_nseg = 1;
	port->p_iocount += 1;
	x = msg_send(port->p_port, &m);

	/*
	 * Update file position on successful I/O
	 */
	if (x > 0) {
		port->p_pos += x;
		return(x);
	}

#ifdef SRV
	return(x);
#else
	/*
	 * Errors other than alignment finish here.
	 */
	if (strcmp(strerror(), EBALIGN)) {
		return(x);
	}

	/*
	 * Go ahead and try to align the I/O on the fly
	 */
	return(balign_io(port, buf, nbyte, (op != FS_WRITE)));
#endif /* SRV */
}

/*
 * do_write()
 *	Build a write message for a generic port
 */
static
do_write(struct port *port, void *buf, uint nbyte)
{
	return(do_io(FS_WRITE, port, buf, nbyte));
}

/*
 * do_read()
 *	Build a read message for a generic port
 */
static
do_read(struct port *port, void *buf, uint nbyte)
{
	return(do_io(FS_READ | M_READ, port, buf, nbyte));
}

/*
 * do_seek()
 *	Build a seek message
 */
static off_t
do_seek(struct port *port, long off, int whence)
{
	struct msg m;
	off_t l;
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
	port->p_iocount += 1;
	x = msg_send(port->p_port, &m);
	if (x >= 0) {
		port->p_pos = l;
		return(l);
	}
	return(-1);
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
#ifndef SRV
	char *p;

	/*
	 * Some special cases, based on file type.
	 */
	p = rstat(port->p_port, "type");

	/*
	 * null-type; emulate null handler within this process
	 */
	if (p && !strcmp(p, "null")) {
		fdnull(port);
		return(0);
	}

	/*
	 * "character"-type; use a POSIXish TTY interface
	 */
	if ((port->p_port != (port_t)-1) && p && !strcmp(p, "c")) {
		extern int __tty_read(), __tty_write();

		/*
		 * XXX this should just be another FDL in its
		 * own file, not this hackery.
		 */
		port->p_read = __tty_read;
		port->p_write = __tty_write;
	} else {
#endif
		port->p_read = do_read;
		port->p_write = do_write;
#ifndef SRV
	}
#endif

	/*
	 * Default handlers
	 */
	port->p_close = do_close;
	port->p_seek = do_seek;
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
 * dup2()
 *	Duplicate old FD onto a given descriptor
 */
dup2(int oldfd, int newfd)
{
	struct port *port;

	/*
	 * Short circuit a special, simple case
	 */
	if (oldfd == newfd) {
		return(0);
	}

	/*
	 * Get pointer to current port
	 */
	port = __port(oldfd);
	if (port == 0) {
		__seterr(EBADF);
		return(-1);
	}

	/*
	 * Close destination if open
	 */
	if (newfd < NFD) {
		if (fdmap[newfd]) {
			close(newfd);
		}
	} else {
		if (hash_lookup(fdhash, (ulong)newfd)) {
			close(newfd);
		}
	}

	/*
	 * Map destination onto existing port
	 */
	setfd(newfd, port);
	port->p_refs += 1;
	return(0);
}

/*
 * read()
 *	Do a read() "syscall"
 */
read(int fd, void *buf, uint nbyte)
{
	struct port *port;

	if ((port = __port(fd)) == 0) {
		__seterr(EBADF);
		return(-1);
	}

	if (nbyte == 0) {
		return(0);
	}
	
	return((*(port->p_read))(port, buf, nbyte));
}

/*
 * write()
 *	Do a write() "syscall"
 */
write(int fd, const void *buf, uint nbyte)
{
	struct port *port;

	if ((port = __port(fd)) == 0) {
		__seterr(EBADF);
		return(-1);
	}
	if (nbyte == 0) {
		return(0);
	}
	return((*(port->p_write))(port, buf, nbyte));
}

/*
 * lseek()
 *	Do a lseek() "syscall"
 */
off_t
lseek(int fd, off_t off, int whence)
{
	struct port *port;

	if ((port = __port(fd)) == 0) {
		__seterr(EBADF);
		return(-1);
	}
	return((*(port->p_seek))(port, off, whence));
}

/*
 * _fdclose()
 *	Close just the FDL data structure
 */
void
_fdclose(int fd)
{
	if (fd < NFD) {
		fdmap[fd] = 0;
	} else {
		(void)hash_delete(fdhash, (ulong)fd);
	}
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
		__seterr(EBADF);
		return(-1);
	}

	/*
	 * Delete from whatever slot it occupies
	 */
	_fdclose(fd);

	/*
	 * Decrement refs, return if there are more
	 */
	port->p_refs -= 1;
	if (port->p_refs > 0) {
		return(0);
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

/*
 * tallyfdl()
 *	Check to see if this is the highest valued fd, record if so
 */
static int
tallyfdl(long l, struct port *port, int *highestp)
{
	if (l > *highestp) {
		*highestp = l;
	}
	return(0);
}

/*
 * getdtablesize()
 *	Return size of file descriptor table
 *
 * Since we don't really have such a table and allocate them
 * sparsely, we instead have to fake it up.  This routine is used
 * primary in a loop to close all file descriptors; sometimes it
 * is used to see how many can be open.  Thus, we return the greater
 * of the highest open file descriptor value, and NFD.
 */
int
getdtablesize(void)
{
	int x, highest = 0;

	if (fdhash) {
		hash_foreach(fdhash, tallyfdl, &highest);
	}
	if (highest < NFD) {
		return(NFD);
	}
	return(highest);
}
