#ifndef _MACHIO_H
#define _MACHIO_H
/*
 * io.h
 *	Declarations relating to I/O on the i386
 */

#include <sys/types.h>
 
/*
 * Prototypes for a few short I/O routines in the C library
 */
extern uchar inportb(int port);
extern void outportb(int port, uchar data);
extern void repinsw(int port, void *buffer, int count);
extern void repoutsw(int port, void *buffer, int count);

#endif /* _MACHIO_H */
