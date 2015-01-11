/*
 * fdmem.c
 *	FDL routines for a memory-based file descriptor
 *
 * Used to allow file-type I/O onto a memory data structure.  BSD would
 * do this using IOSTRG in the buffered file layer, but FDL seems well-
 * suited for it in VSTa, so we'll try it this way.
 */
#include <sys/fs.h>
#include <fdl.h>
#include <std.h>

/*
 * State kept on an active buffer
 */
struct state {
	intfun s_read,		/* Read/write/close vectors */
		s_write,
		s_close;
	void *s_seek;		/* Seek fn pointer */
	void *s_cookie;		/* Cookie to pass */
};

/*
 * call_read()
 *	Read bytes from position
 */
static int
call_read(struct port *port, void *buf, uint nbyte)
{
	struct state *s = port->p_data;

	if (s->s_read) {
		return((*s->s_read)(s->s_cookie, buf, nbyte));
	}
	return(__seterr(EIO));
}

/*
 * call_write()
 *	Write bytes out
 */
static int
call_write(struct port *port, void *buf, uint nbyte)
{
	struct state *s = port->p_data;

	if (s->s_write) {
		return((*s->s_write)(s->s_cookie, buf, nbyte));
	}
	return(__seterr(EIO));
}

/*
 * call_seek()
 *	Set position in buffer
 */
static off_t
call_seek(struct port *port, off_t off, int whence)
{
	struct state *s = port->p_data;
	intfun fn;

	if (s->s_seek) {
		fn = s->s_seek;
		(*fn)(s->s_cookie, off, whence);
		return(0);
	}
	return(__seterr(EIO));
}

/*
 * call_close()
 *	Close up and free state
 */
static int
call_close(struct port *port)
{
	struct state *s = port->p_data;
	int x;

	if (s->s_close) {
		x = (*s->s_close)(s->s_cookie);
	} else {
		x = 0;
	}
	free(s);
	return(x);
}

/*
 * _fdcall()
 *	Create FDL entry with callbacks to user functions
 */
int
_fdcall(void *cookie, intfun readfn, intfun writefn,
	void *seekfn, intfun closefn)
{
	int fd;
	struct port *p;
	struct state *s;

	/*
	 * Get buffer
	 */
	s = malloc(sizeof(struct state));
	if (s == 0) {
		return(-1);
	}
	s->s_cookie = cookie;
	s->s_read = readfn;
	s->s_write = writefn;
	s->s_seek = seekfn;
	s->s_close = closefn;

	/*
	 * Get an open fd and underlying port structure
	 */
	fd = __fd_alloc((port_t)-1);
	if (fd < 0) {
		free(s);
		return(-1);
	}
	p = __port(fd);

	/*
	 * Wire on our own routines
	 */
	p->p_read = call_read;
	p->p_write = call_write;
	p->p_seek = (void *)call_seek;
	p->p_close = call_close;
	p->p_data = s;

	/*
	 * There you are
	 */
	return(fd);
}
