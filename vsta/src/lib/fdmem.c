/*
 * fdmem.c
 *	FDL routines for a memory-based file descriptor
 *
 * Used to allow file-type I/O onto a memory data structure.  BSD would
 * do this using IOSTRG in the buffered file layer, but FDL seems well-
 * suited for it in VSTa, so we'll try it this way.
 */
#include <sys/types.h>
#include <std.h>
#include <unistd.h>
#include <fdl.h>

/*
 * State kept on an active buffer
 */
struct state {
	char *s_buf;	/* Buffer, viewed as bytes */
	uint s_len;	/* # bytes */
};

/*
 * mem_read()
 *	Read bytes from position
 */
static
mem_read(struct port *port, void *buf, uint nbyte)
{
	uint cnt;
	struct state *s = port->p_data;
	char *p;

	cnt = 0;
	p = buf;
	while (nbyte && (port->p_pos < s->s_len)) {
		*p = (s->s_buf)[port->p_pos];
		p += 1;
		port->p_pos += 1;
		cnt += 1;
		nbyte -= 1;
	}
	return(cnt);
}

/*
 * mem_write()
 *	Write onto memory buffer
 */
static
mem_write(struct port *port, void *buf, uint nbyte)
{
	uint cnt;
	struct state *s = port->p_data;
	char *p;

	cnt = 0;
	p = buf;
	while (nbyte && (port->p_pos < s->s_len)) {
		(s->s_buf)[port->p_pos] = *p;
		p += 1;
		port->p_pos += 1;
		cnt += 1;
		nbyte -= 1;
	}
}

/*
 * mem_seek()
 *	Set position in buffer
 */
static
mem_seek(struct port *port, off_t off, int whence)
{
	off_t l;
	struct state *s;

	switch (whence) {
	case SEEK_SET:
		l = off;
		break;
	case SEEK_CUR:
		l = port->p_pos + off;
		break;
	case SEEK_END:
		s = port->p_data;
		l = s->s_len;
		l += off;
		break;
	default:
		return(-1);
	}
	port->p_pos = l;
	return(l);
}

/*
 * mem_close()
 *	Close up and free state
 */
static
mem_close(struct port *port)
{
	free(port->p_data);
	return(0);
}

/*
 * fdmem()
 *	Given memory range, open file descriptor view onto the memory
 */
fdmem(void *buf, uint len)
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
	p->p_read = mem_read;
	p->p_write = mem_write;
	p->p_seek = (void *)mem_seek;
	p->p_close = mem_close;

	/*
	 * Record state for this kind of FD
	 */
	p->p_data = s;
	s->s_buf = buf;
	s->s_len = len;
	return(fd);
}
