/*
 * dbg_ser.c
 *	Routines to do a spin-oriented interface to a serial port
 */
#include <mach/param.h>

#ifdef SERIAL

#include "locore.h"

/*
 * 1 for COM1, 0 for COM2 (bleh)
 */
#define COM (1)

/*
 * I/O address of registers
 */
#define IOBASE (0x2F0 + COM*0x100)	/* Base of registers */
#define LINEREG (IOBASE+0xB)	/* Format of RS-232 data */
#define LOWBAUD (IOBASE+0x8)	/* low/high parts of baud rate */
#define HIBAUD (IOBASE+0x9)
#define LINESTAT (IOBASE+0xD)	/* Status of line */
#define DATA (IOBASE+0x8)	/* Read/write data here */
#define INTREG (IOBASE+0x9)	/* Interrupt control */
#define INTID (IOBASE+0xA)	/* Why "interrupted" */
#define MODEM (IOBASE+0xC)	/* Modem lines */

/*
 * init_cons()
 *	Initialize to 9600 baud on com port
 */
void
init_cons(void)
{
	outportb(LINEREG, 0x80);	/* 9600 baud */
	outportb(HIBAUD, 0);
	outportb(LOWBAUD, 0x0C);
	outportb(LINEREG, 3);		/* 8 bits, one stop */
}

/*
 * rs232_putc()
 *	Busy-wait and then send a character
 */
void
cons_putc(int c)
{
	while ((inportb(LINESTAT) & 0x20) == 0)
		;
	outportb(DATA, c & 0x7F);
}

#ifdef KDB
/*
 * cons_getc()
 *	Busy-wait and return next character
 */
int
cons_getc(void)
{
	while ((inportb(LINESTAT) & 1) == 0)
		;
	return(inportb(DATA) & 0x7F);
}
#endif /* KDB */

#endif /* SERIAL */
