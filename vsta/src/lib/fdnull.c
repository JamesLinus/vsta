/*
 * fdmem.c
 *	FDL routines for null device
 *
 * The /dev/null server is fine, but requires a full turnaround from
 * us to him, and back.  Since we know what he's going to do anyway,
 * we short-circuit the process if we recogznie the null device.
 */
#include <sys/types.h>
#include <fdl.h>

/*
 * devnull_read()
 *	Read bytes from position
 */
static
devnull_read(struct port *port, void *buf, uint nbyte)
{
	return(0);
}

/*
 * devnull_write()
 *	Write onto memory buffer
 */
static
devnull_write(struct port *port, void *buf, uint nbyte)
{
	return(nbyte);
}

/*
 * devnull_seek()
 *	Set position in buffer
 */
static
devnull_seek(struct port *port, off_t off, int whence)
{
	return(off);
}

/*
 * devnull_close()
 *	Close up and free state
 */
static
devnull_close(struct port *port)
{
	return(0);
}

/*
 * fdnull()
 *	Set up fd for null service
 */
void
fdnull(struct port *p)
{
	/*
	 * Wire on our own routines
	 */
	p->p_read = devnull_read;
	p->p_write = devnull_write;
	p->p_seek = (void *)devnull_seek;
	p->p_close = devnull_close;

	/*
	 * Record state for this kind of FD--no state
	 */
	p->p_data = 0;
}
