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
	ulong p_pos;	/* Absolute byte offset in file */
};

/*
 * Internal routines
 */
extern uint __fdl_size(void);
extern void __fdl_save(char *, ulong);
extern char *__fdl_restore(char *);

#endif /* _FDL_H */
