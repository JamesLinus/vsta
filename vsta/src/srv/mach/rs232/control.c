/*
 * control.c
 *	Control the line settings of the serial port
 */
#include "rs232.h"

extern int irq, iobase;
extern int baud, databits, stopbits, parity;
extern uchar dtr, dsr, rts, cts, dcd, ri;

/*
 * rs232_linesetup()
 *	Setup all of the data, stop and parity bits
 *
 * We don't take any parameters as we're just going to use the values
 * already established
 */
static void
rs232_linesetup(void)
{
	uchar regval;
	static uchar pbits[5] = {
		0x0,
		CFCR_PENAB | CFCR_PEVEN,
		CFCR_PENAB | CFCR_PODD,
		CFCR_PENAB | CFCR_PZERO,
		CFCR_PENAB | CFCR_PONE
	};
	
	/*
	 * Establish the data bit count in bits 0 and 1
	 */
	regval = databits - 5;

	/*
	 * Establish the stop bit count in bit 2
	 */
	regval |= (stopbits ? CFCR_STOPB : 0);
	
	/*
	 * Establish the parity bit settings in bits 3, 4 and 5
	 */
	regval |= pbits[parity];
	
	outportb(iobase + CFCR, regval);
}

/*
 * rs232_baud()
 *	Set baud rate
 */
void
rs232_baud(int brate)
{
	uchar old_cfcr;
	int bits;

	if (brate == 0) {
		return;
	}
	bits = COMBRD(brate);
	old_cfcr = inportb(iobase + CFCR);
	outportb(iobase + CFCR, CFCR_DLAB);
	outportb(iobase + BAUDHI, (bits >> 8) & 0xFF);
	outportb(iobase + BAUDLO, bits & 0xFF);
	outportb(iobase + CFCR, old_cfcr);
	baud = brate;
}

/*
 * rs232_databits()
 *	Set the number of data bits to be used
 */
void
rs232_databits(int dbits)
{
	databits = dbits;
	rs232_linesetup();
}

/*
 * rs232_stopbits()
 *	Set the number of stop bits to be used
 *
 * If we only have 5 data bits then we only actually support 1.5 stop bits
 * and not 2, however we just use 2 to mean 1.5 in this case
 */
void
rs232_stopbits(int sbits)
{
	stopbits = sbits;
	rs232_linesetup();
}

/*
 * rs232_parity()
 *	Set the line parity protection type
 */
void
rs232_parity(int ptype)
{
	parity = ptype;
	rs232_linesetup();
}

/*
 * rs232_setdtr()
 *	Set the new DTR control line state
 */
void
rs232_setdtr(int newdtr)
{
	dtr = newdtr ? 1 : 0;
	outportb(iobase + MCR,
		 (inportb(iobase + MCR) & ~(MCR_DTR)) | (dtr ? MCR_DTR : 0));
}

/*
 * rs232_setrts()
 *	Set the new RTS control line state
 */
void
rs232_setrts(int newrts)
{
	rts = newrts ? 1 : 0;
	outportb(iobase + MCR,
		 (inportb(iobase + MCR) & ~(MCR_RTS)) | (rts ? MCR_RTS : 0));
}

/*
 * rs232_getinsigs(void)
 *	Read the dsr, cts, dcd and ri status lines
 */
void
rs232_getinsigs(void)
{
	uchar c;

	c = inportb(iobase + MSR);
	dsr = (c & MSR_DSR) ? 1 : 0;
	cts = (c & MSR_CTS) ? 1 : 0;
	dcd = (c & MSR_DCD) ? 1 : 0;
	ri = (c & MSR_RI) ? 1 : 0;

}
