/*
 * isr.c
 *	Handler for interrupt events
 */
#include <sys/fs.h>
#include <sys/sched.h>
#include <syslog.h>
#include "rs232.h"
#include "fifo.h"

extern uint iobase;
extern int txwaiters, rxwaiters;
extern struct fifo *inbuf, *outbuf;
extern uchar dtr, dsr, rts, cts, dcd, ri;

volatile uint txbusy;	/* UART sending data right now? */
ulong overruns;		/* Count of overruns */

#define BUFLEN (RS232_MAXBUF)
static volatile uint	/* Transmit and receive FIFOs */
	txhd, txtl,
	rxhd, rxtl;
static char txbuf[BUFLEN],
	rxbuf[BUFLEN];
static pid_t helper;	/* PID of helper I/O thread */

/*
 * do_tx()
 *	Kick off a transmit of the next char
 */
static inline void
do_tx(void)
{
	uint x;

	x = txtl;
	outportb(iobase + DATA, txbuf[x]);
	if (++x >= BUFLEN) {
		x = 0;
	}
	txtl = x;
	txbusy = 1;
}

/*
 * run_helper()
 *	Interface from front-line ISR to user-oriented queues
 */
void
run_helper(void)
{
	uint x;

	/*
	 * More receive data?  Move it into the regular
	 * FIFO, then wake any waiters.
	 */
	if (rxhd != rxtl) {
		while (rxtl != rxhd) {
			x = rxtl;
			fifo_put(inbuf, rxbuf[x++]);
			if (x >= BUFLEN) {
				x = 0;
			}
			rxtl = x;
		}
		if (!fifo_empty(inbuf) && rxwaiters) {
			dequeue_rx();
		}
	}

	/*
	 * More room for transmission?  Move some over.
	 */
	if (txwaiters && fifo_empty(outbuf)) {
		dequeue_tx();
	}
	while (!fifo_empty(outbuf)) {
		x = txhd + 1;
		if (x >= BUFLEN) {
			x = 0;
		}
		if (x == txtl) {
			break;
		}
		txbuf[txhd] = fifo_get(outbuf);
		txhd = x;
	}
	if ((txhd != txtl) && !txbusy) {
		do_tx();
	}
}

/*
 * helper_daemon()
 *	Wait to be kicked by the ISR, and queue a request
 */
static void
helper_daemon(void)
{
	struct msg m;
	port_t mainport;

	/*
	 * Connect to main server
	 */
	mainport = msg_connect(rs232port_name, 0);

	for (;;) {
		/*
		 * Wait to be nudged
		 */
		mutex_thread(0);

		/*
		 * Send a helper request
		 */
		m.m_op = RS232_HELPER;
		m.m_nseg = m.m_arg = 0;
		(void)msg_send(mainport, &m);
	}
}

/*
 * rs232_isr()
 *	Called to process an interrupt event from the port
 */
inline static void
rs232_isr(void)
{
	uchar c, nudge = 0;
	uint x;

	for (;;) {
		/*
		 * Decode next reason
		 */
		c = inportb(iobase + IIR) & IIR_IMASK;
		switch (c) {

		/*
		 * Line state, just clear
		 */
		case IIR_RLS:
			c = inportb(iobase + LSR);
			if (c & LSR_OE) {
				overruns += 1;
			}
			break;

		/*
		 * Modem state
		 */
		case IIR_MLSC:
			c = inportb(iobase + MSR);
			dsr = (c & MSR_DSR) ? 1 : 0;
			cts = (c & MSR_CTS) ? 1 : 0;
			dcd = (c & MSR_DCD) ? 1 : 0;
			ri = (c & MSR_RI) ? 1 : 0;
			break;

		/*
		 * All done for this ISR
		 */
		case IIR_NOPEND:
			goto out;

		case IIR_RXTOUT:	/* Receiver ready */
		case IIR_RXRDY:
			c = inportb(iobase + DATA);
#if defined(DEBUG) && defined(KDB)
			if (c == '\32') {
				extern void dbg_enter(void);

				dbg_enter();
				break;
			}
#endif
			/*
			 * Queue in ISR data buffer
			 */
			x = rxhd;
			if (++x >= BUFLEN) {
				x = 0;
			}
			if (x != rxtl) {
				rxbuf[rxhd] = c;
				rxhd = x;
			}
			nudge = 1;
			break;

		case IIR_TXRDY:		/* Transmitter ready */
			if (txhd != txtl) {
				do_tx();
			} else {
				txbusy = 0;
			}
			nudge = 1;
			break;
		}
	}
out:
	/*
	 * If we exchanged any data, kick the helper
	 */
	if (nudge) {
		mutex_thread(helper);
	}
}

/*
 * start_tx()
 *	Start transmitter
 */
void
start_tx(void)
{
	mutex_thread(helper);
}

/*
 * isr_daemon()
 *	Dedicated thread for taking incoming interrupts
 *
 * Does the front-line data gathering, then hands it off to the
 * main thread to deal with interacting with other processes.
 */
static void
isr_daemon(port_t isrport)
{
	int x;
	struct msg m;

	/*
	 * Take a shot at real-time priority
	 */
	(void)sched_op(SCHEDOP_SETPRIO, PRI_RT);

	/*
	 * Take interrupts, process
	 */
	for (;;) {
		/*
		 * Get next message.  Accept only M_ISR.
		 */
		x = msg_receive(isrport, &m);
		if (m.m_op != M_ISR) {
			msg_err(m.m_sender, EPERM);
			continue;
		}

		/*
		 * Process it
		 */
		rs232_isr();
	}
}

/*
 * rs232_enable()
 *	Enable rs232 interrupts
 */
void
rs232_enable(int irq)
{
	port_t isrport;

	/*
	 * Start with port set up for hard-wired RS-232
	 */
	outportb(iobase + MCR, MCR_DTR|MCR_RTS|MCR_IENABLE);
	dtr = 1;
	rts = 1;
	rs232_getinsigs();

	/*
	 * Allocate a unique port with its own thread to watch
	 * the incoming ISR stream.
	 */
	isrport = msg_port(0, 0);

	/*
	 * Tell system about our I/O vector
	 */
	if (enable_isr(isrport, irq)) {
		syslog(LOG_ERR, "IRQ %d allocation", irq);
		exit(1);
	}

	/*
	 * Launch threads dedicated to this port
	 */
	(void)tfork(isr_daemon, isrport);
	helper = tfork(helper_daemon, 0);

	/*
	 * Allow all interrupt sources
	 */
	outportb(iobase + IER,
		IER_ERXRDY|IER_ETXRDY|IER_ERLS|IER_EMSC);
}
