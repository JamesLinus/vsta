/*
 * control.c
 *	Control the line settings of the serial port
 */
#include "rs232.h"

extern int irq, iobase;
extern int baud, databits, stopbits, parity;
extern uchar dtr, dsr, rts, cts, dcd, ri;
extern int uart, rx_fifo_threshold, tx_fifo_threshold;

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
 * rs232_getinsigs()
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

/*
 * rs232_setrxfifo()
 *	Set the receiver UART FIFO threshold
 *
 * Returns 0 on success, non-zero otherwise.  If we enter with threshold
 * equal to 0 we attempt to pick an appropriate threshold for the baud rate
 * currently in use
 */
int
rs232_setrxfifo(int threshold)
{
	uchar b;

	if (threshold == 0) {
		/*
		 * Work out what to set
		 */
		if (uart == UART_16550A) {
			if (baud < 2400) {
				threshold = 1;
			} else {
				threshold = 4;
			}
		} else {
			/*
			 * We're not doing anything wrong, we're just not
			 * changing anything either
			 */
			rx_fifo_threshold = 1;
			tx_fifo_threshold = 1;
			return(0);
		}
	} else {
		/*
		 * We can only set the FIFO threshold on a 16550A
		 */
		if (uart != UART_16550A) {
			return(-1);
		}
	}

	switch(threshold) {
	case 1:
		b = FIFO_TRIGGER_1;
		break;
	case 4:
		b = FIFO_TRIGGER_4;
		break;
	case 8:
		b = FIFO_TRIGGER_8;
		break;
	case 14:
		b = FIFO_TRIGGER_14;
		break;
	default:
		/*
		 * We must have had a non valid value
		 */
		return(-1);
	}

	/*
	 * OK we had a good value, set the threshold details and reset the
	 * contents of the FIFOs - changing them whilst data's flowing would
	 * be a little daft!
	 */
	outportb(iobase + FIFO,
		b | FIFO_ENABLE | FIFO_RCV_RST | FIFO_XMT_RST);
	rx_fifo_threshold = threshold;
	tx_fifo_threshold = 16;

	return(0);
}

/*
 * rs232_iduart()
 *	Work out what sort of UART we have
 *
 * Returns non-zero if we can't find or determine the type of UART in the
 * system.  Returns zero and sets "uart" appropriately if we can work out
 * what we've got
 *
 * A lot of this code is based from the Linux serial driver written by
 * Ted Ts'o
 */
int
rs232_iduart(int test_uart)
{
	uchar x, t1, t2, t3;

	if (test_uart) {
		/*
		 * Check that there's anything to talk to at the base address
		 */
		t1 = inportb(iobase + IER);
		outportb(iobase + IER, 0);
		t2 = inportb(iobase + IER);
		outportb(iobase + IER, t1);
		if (t2) {
			return(1);
		}

		/*
		 * If requested, check that there is a UART at the base
		 * address.  We allow this test to be ignored to cope with
		 * some nasty modems that don't have internal loopback
		 * capability
		 */
		t3 = inportb(iobase + MCR);
		outportb(iobase + MCR, t3 | MCR_LOOPBACK);
		t2 = inportb(iobase + MSR);
		outportb(iobase + MCR, MCR_LOOPBACK | MCR_IENABLE | MCR_RTS);
		t1 = inportb(iobase + MSR)
			& (MSR_DCD | MSR_RI | MSR_DSR | MSR_CTS);
		outportb(iobase + MCR, t3);
		outportb(iobase + MSR, t2);

		if (t1 != (MSR_DCD | MSR_CTS)) {
			return(1);
		}
	}
	
	if (uart == UART_UNKNOWN) {
		/*
		 * Look to see if we can find any sort of FIFO response
		 */
		outportb(iobase + FIFO, FIFO_ENABLE);
		x = (inportb(iobase + IIR) & IIR_FIFO_MASK) >> 6;

		switch(x) {
		case 0:
			/*
			 * No FIFO response is a 16450 or 8250.  The 8250
			 * doesn't have a scratchpad register though.  We
			 * make this test attempt to restore the original
			 * scratchpad state
			 */
			uart = UART_16450;
			t3 = inportb(iobase + SCRATCH);
			outportb(iobase + SCRATCH, 0xa5);
			t1 = inportb(iobase + SCRATCH);
			outportb(iobase + SCRATCH, 0x5a);
			t2 = inportb(iobase + SCRATCH);
			outportb(iobase + SCRATCH, t3);
			if ((t1 != 0xa5) || (t2 != 0x5a)) {
				uart = UART_8250;
			}
			break;
		case 1:
			/*
			 * This would be a pretty unique wierd response
			 */
			uart = UART_UNKNOWN;
			return(1);
		case 2:
			/*
			 * This is the sort of broken response we get from an
			 * early 16550 part with a broken FIFO
			 */
			uart = UART_16550;
			break;
		case 3:
			/*
			 * We have a 16550A - working FIFOs
			 */
			uart = UART_16550A;
			break;
		}
	}
	
	return(0);
}
