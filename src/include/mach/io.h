#ifndef _MACHIO_H
#define _MACHIO_H
/*
 * io.h
 *	Declarations relating to I/O on the i386
 */

#include <sys/types.h>
 
#ifndef __GNUC__
extern uchar inportb(int port);
extern void outportb(int port, uchar data);
extern ushort inportw(int port);
extern void outportw(int port, ushort data);
#else
/*
 * On GNU C, we can inline assembly functions, with a significant
 * gain in speed and modest reduction in code size.
 */

/*
 * inportb()
 *	Get a byte from an I/O port
 */
inline extern uchar
inportb(int port)
{
	register uchar res;
	
	__asm__ __volatile__(
		"inb %%dx,%%al\n\t"
		: "=a" (res)
		: "d" (port));
	return(res);
}

/*
 * outportb()
 *	Write a byte to an I/O port
 */
inline extern void
outportb(int port, uchar data)
{
	__asm__ __volatile__(
		"outb %%al,%%dx\n\t"
		: /* No output */
		: "a" (data), "d" (port));
}

/*
 * inportw()
 *	Get a word from an I/O port
 */
inline extern ushort
inportw(int port)
{
	register ushort res;
	
	__asm__ __volatile__(
		"inw %%dx,%%ax\n\t"
		: "=a" (res)
		: "d" (port));
	return(res);
}

/*
 * outportw()
 *	Write a word to an I/O port
 */
inline extern void
outportw(int port, ushort data)
{
	__asm__ __volatile__(
		"outw %%ax,%%dx\n\t"
		: /* No output */
		: "a" (data), "d" (port));
}
#endif

extern void repinsw(int port, void *buffer, int count);
extern void repoutsw(int port, void *buffer, int count);

#endif /* _MACHIO_H */
