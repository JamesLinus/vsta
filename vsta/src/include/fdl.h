#ifndef _FDL_H
#define _FDL_H
/*
 * fdl.h
 *	Externally-visible definitions for File Descriptor Layer
 */
#include <sys/types.h>

typedef off_t (*__offt_fun)();

/*
 * Per-connection state information
 */
struct port {
	port_t p_port;		/* Port connection # */
	void *p_data;		/* Per-port-type state */
	intfun p_read,		/* Read/write/etc. functions */
		p_write,
		p_close;
	__offt_fun p_seek;
	uint p_refs;		/* # FD's mapping to this port # */
	ulong p_pos;		/* Absolute byte offset in file */
	ulong p_iocount;	/* I/O count, for select() */
};

/*
 * Internal routines
 */
extern uint __fdl_size(void);
extern void __fdl_save(char *, ulong);
extern char *__fdl_restore(char *);
extern port_t __fd_port(int);
extern int __fd_alloc(port_t);
extern struct port *__port(int);
extern ulong __fd_iocount(int);
extern void __fd_set_iocount(int, ulong);

#endif /* _FDL_H */
