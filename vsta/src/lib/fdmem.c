/*
 * fdmem.c
 *	FDL routines for a memory-based file descriptor
 *
 * Used to allow file-type I/O onto a memory data structure.  BSD would
 * do this using IOSTRG in the buffered file layer, but FDL seems well-
 * suited for it in VSTa, so we'll try it this way.
 */
#include <sys/fs.h>
#include <std.h>
#include <unistd.h>
#include <fdl.h>

/*
 * State kept on an active buffer
 */
struct state {
	char *s_buf;		/* Buffer, viewed as bytes */
	uint s_len;		/* # bytes */
	uint s_hd, s_tl,	/* Head/tail of circular consumption */
		s_cnt;		/*  ...amount in ring */
	uint s_mybuf;		/* Flag that we malloc()'ed the buf */
};

/*
 * next()
 *	Advance to next index value, with circular wrap
 */
inline static uint
next(uint oldidx, struct state *s)
{
	if (oldidx >= s->s_len) {
		return(0);
	}
	return(oldidx+1);
}

/*
 * mem_read()
 *	Read bytes from position
 */
static int
mem_read(struct port *port, void *buf, uint nbyte)
{
	uint cnt;
	struct state *s = port->p_data;
	char *p;

	cnt = 0;
	p = buf;
	while (nbyte && s->s_cnt) {
		*p++ = (s->s_buf)[s->s_tl];
		s->s_tl = next(s->s_tl, s);
		cnt += 1;
		nbyte -= 1;
		s->s_cnt -= 1;
	}
	return(cnt);
}

/*
 * mem_write()
 *	Write onto memory buffer
 */
static int
mem_write(struct port *port, void *buf, uint nbyte)
{
	uint cnt;
	struct state *s = port->p_data;
	char *p;

	cnt = 0;
	p = buf;
	while (nbyte && (s->s_cnt < s->s_len)) {

		/*
		 * Store byte and advance
		 */
		(s->s_buf)[s->s_hd] = *p++;
		s->s_hd = next(s->s_hd, s);
		cnt += 1;
		nbyte -= 1;
	}
	return(cnt);
}

/*
 * mem_seek()
 *	Set position in buffer
 *
 * An error, since it's a circular memory buffer with no absolute
 * position.
 */
static
mem_seek(struct port *port, off_t off, int whence)
{
	return(__seterr(EINVAL));
}

/*
 * mem_close()
 *	Close up and free state
 */
static
mem_close(struct port *port)
{
	struct state *s = port->p_data;

	if (s->s_mybuf) {
		free(s->s_buf);
	}
	free(s);
	return(0);
}

/*
 * fdmem()
 *	Given memory range, open file descriptor view onto the memory
 */
int
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
	 * Record state for this kind of FD.  Note that we set up
	 * head/tail so they can reflect both a full and an empty
	 * buffer.  mybuf is 0 for either case, so that failed
	 * malloc()'s can clean up through the close() vector
	 * correctly.
	 */
	p->p_data = s;
	s->s_mybuf = 0;
	if (buf == 0) {
		buf = malloc(len);
		if (buf == 0) {
			close(fd);
			return(-1);
		}
		s->s_mybuf = 1;
		s->s_cnt = 0;
	} else {
		s->s_cnt = len;
	}
	s->s_hd = s->s_tl = 0;
	s->s_buf = buf;
	s->s_len = len;
	return(fd);
}
