/*
 * isr.c
 *	Handler for interrupt events
 */
#include <sys/msg.h>
#include <sys/types.h>
#include "rs232.h"
#include "fifo.h"

extern uint iobase;
extern int txwaiters, rxwaiters;
extern struct fifo *inbuf, *outbuf;

int txbusy;		/* UART sending data right now? */

/*
 * rs232_isr()
 *	Called to process an interrupt event from the port
 */
void
rs232_isr(struct msg *m)
{
	for (;;) {
		uchar why;
		char c;

		/*
		 * Decode next reason
		 */
		why = inportb(iobase + IIR) & IIR_IMASK;
		switch (why) {

		/*
		 * Line state, just clear
		 */
		case IIR_RLS:
			(void)inportb(iobase + LSR);
			break;

		/*
		 * Modem state, just clear
		 */
		case IIR_MLSC:
			(void)inportb(iobase + MSR);
			break;

		/*
		 * All done for this ISR
		 */
		case IIR_NOPEND:
			goto out;

		case IIR_RXTOUT:	/* Receiver ready */
		case IIR_RXRDY:
			c = inportb(iobase + DATA);
			fifo_put(inbuf, c);
			break;

		case IIR_TXRDY:		/* Transmitter ready */
			if (txwaiters && fifo_empty(outbuf)) {
				dequeue_tx();
			}
			if (!fifo_empty(outbuf)) {
				outportb(iobase + DATA,
					fifo_get(outbuf));
				txbusy = 1;
			} else {
				txbusy = 0;
			}
			break;
		}
	}
out:
	/*
	 * If we received any data, and somebody is waiting,
	 * call the hook to wake them up.
	 */
	if (!fifo_empty(inbuf) && rxwaiters) {
		dequeue_rx();
	}
}

/*
 * start_tx()
 *	Start transmitter
 */
void
start_tx(void)
{
	struct fifo *f = outbuf;

	if (fifo_empty(f) || txbusy) {
		return;
	}
	outportb(iobase + DATA, fifo_get(f));
	txbusy = 1;
}

/*
 * rs232_baud()
 *	Set baud rate
 *
 * Not really an ISR'ish thing, but it's the best place I could find.
 */
void
rs232_baud(int baud)
{
	uint bits;

	if (baud == 0) {
		return;
	}
	bits = COMBRD(baud);
	outportb(iobase + CFCR, CFCR_DLAB);
	outportb(iobase + BAUDHI, (bits >> 8) & 0xFF);
	outportb(iobase + BAUDLO, bits & 0xFF);
	outportb(iobase + CFCR, CFCR_8BITS);
}

/*
 * rs232_enable()
 *	Enable rs232 interrupts
 */
void
rs232_enable(void)
{
	/*
	 * Set up 16550 FIFO chip, if present
	 */
	outportb(iobase + FIFO, FIFO_ENABLE|FIFO_RCV_RST|
		FIFO_XMT_RST|FIFO_TRIGGER_4);
	__msleep(100);
	if ((inportb(iobase + IIR) & IIR_FIFO_MASK) == IIR_FIFO_MASK) {
		outportb(iobase + FIFO, FIFO_ENABLE|FIFO_TRIGGER_4);
	}

	/*
	 * Start with port set up for hard-wired RS-232
	 */
	outportb(iobase + MCR, MCR_DTR|MCR_RTS|MCR_IENABLE);

	/*
	 * Allow all interrupt sources
	 */
	outportb(iobase + IER,
		IER_ERXRDY|IER_ETXRDY|IER_ERLS|IER_EMSC);
}
