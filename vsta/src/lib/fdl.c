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
 * fd_port()
 *	Map a file descriptor value into a port
 *
 * We support an arbitrary number of file descriptors.  The first
 * NFD of them live in a simple array.  The rest are looked up
 * through a hash table.
 */
static struct port *
fd_port(uint fd)
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

	if ((port = fd_port(fd)) == 0) {
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

	m.m_op = FS_WRITE;
	m.m_buf = buf;
	m.m_buflen = nbyte;
	m.m_arg = nbyte;
	m.m_arg1 = 0;
	m.m_nseg = 1;
	return(msg_send(port->p_port, &m));
}

/*
 * do_read()
 *	Build a read message for a generic port
 */
static
do_read(struct port *port, void *buf, uint nbyte)
{
	struct msg m;

	m.m_op = FS_READ|M_READ;
	m.m_buf = buf;
	m.m_buflen = nbyte;
	m.m_arg = nbyte;
	m.m_arg1 = 0;
	m.m_nseg = 1;
	return(msg_send(port->p_port, &m));
}

/*
 * do_seek()
 *	Build a seek message
 */
static
do_seek(struct port *port, ulong off, int whence)
{
	struct msg m;

	m.m_op = FS_SEEK;
	m.m_buflen = 0;
	m.m_arg = off;
	m.m_arg1 = whence;
	m.m_nseg = 0;
	return(msg_send(port->p_port, &m));
}

/*
 * do_close()
 *	Send a disconnect
 *
 * close() itself does most of the grunt-work; this is a hook for
 * layers which need additional cleanup.
 */
static
do_close(struct port *port)
{
	return(0);
}

/*
 * do_open()
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
	if (__isatty(port->p_port)) {
		extern int __tty_read(), __tty_write();

		port->p_read = __tty_read;
		port->p_write = __tty_write;
	} else {
		port->p_read = do_read;
		port->p_write = do_write;
	}
	return(0);
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
	int x;
	struct port *port;

	/*
	 * Allocate the port data structure
	 */
	if ((port = malloc(sizeof(struct port))) == 0) {
		return(0);
	}

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
		 * On first use of high slots, allocate the hash
		 */
		if (!fdhash) {
			fdhash = hash_alloc(NFD);
			if (!fdhash) {
				free(port);
				return(0);
			}
		}

		/*
		 * Scan until we find an open value
		 */
		while (hash_lookup(fdhash, fdnext)) {
			INC(fdnext);
		}
		x = fdnext; INC(fdnext);
		hash_insert(fdhash, (ulong)x, port);
	} else {
		fdmap[x] = port;
	}

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
 * read()
 *	Do a read() "syscall"
 */
read(int fd, void *buf, uint nbyte)
{
	struct port *port;

	if ((port = fd_port(fd)) == 0) {
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

	if ((port = fd_port(fd)) == 0) {
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

	if ((port = fd_port(fd)) == 0) {
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
	if ((port = fd_port(fd)) == 0) {
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
	 * Disconnect from its server
	 */
	error = msg_disconnect(port->p_port);

	/*
	 * Let the layer do stuff if it wishes
	 */
	(void)((*(port->p_close))(port));

	/*
	 * Free the port memory
	 */
	free(port);

	return(error);
}
