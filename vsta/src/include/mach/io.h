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
extern ushort inportw();
extern void outportw(int port, ushort data);
extern void repinsw(int port, void *buffer, int count);
extern void repoutsw(int port, void *buffer, int count);
extern int cli(void);
extern void sti(void);
#ifdef KERNEL
extern void set_cr0(ulong), set_cr2(ulong), set_cr3(ulong);
extern ulong get_cr0(void), get_cr2(void), get_cr3(void);
#endif /* KERNEL */

#endif /* _MACHIO_H */
