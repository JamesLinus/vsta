#ifndef _FDL_H
#define _FDL_H
/*
 * fdl.h
 *	Externally-visible definitions for File Descriptor Layer
 */
#include <sys/types.h>

/*
 * Per-connection state information
 */
struct port {
	port_t p_port;	/* Port connection # */
	void *p_data;	/* Per-port-type state */
	intfun p_read,	/* Read/write/etc. functions */
		p_write,
		p_close,
		p_seek;
	uint p_refs;	/* # FD's mapping to this port # */
};

#endif /* _FDL_H */
