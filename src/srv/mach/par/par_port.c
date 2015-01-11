/*
 * par_port.c
 *	Low level handling of the parallel port. Originally it was written
 *	in C++ and then converted to C. struct par_port is the printer
 *	"object", the functions with prefix "par_" are the
 *	"member functions".
 */
#include "par_port.h"
#include <stdio.h>
#include <stdlib.h>
extern unsigned inportb(int portid);
extern void outportb(int portid, int value);
extern int enable_io(int start, int end);



/*
 * status register (PS..Parallel Status) Read-Only
 * bit 7 6 5 4 3 2 1 0
 *     | | | | |
 *     | | | | +-------- /ERROR		0...error
 *     | | | +---------- SLCT		1...printer is online
 *     | | +------------ PE		1...paper empty
 *     | +-------------- /ACK		0...printer can receive next character
 *     +---------------- /BUSY		0...busy
 */
#define PS_ERROR (1 << 3)
#define PS_SLCT  (1 << 4)
#define PS_PE    (1 << 5)
#define PS_ACK   (1 << 6)
#define PS_BUSY  (1 << 7)

/*
 * control register (PC..Parallel Control) Read/Write
 * bit 7 6 5 4 3 2 1 0
 *           | | | | |
 *           | | | | +-- STROBE		1...data are valid
 *           | | | +---- AUTO FEED	1...automatic LF after CR
 *           | | +------ /INIT		0...reset printer
 *           | +-------- SLCT IN	1...select input(put printer online)
 *           +---------- IRQ Enable	1...generate interrupt when /ACK
 *					    goes low
 */
#define PC_STROBE   (1 << 0)
#define PC_AUTOFEED (1 << 1)
#define PC_INIT     (1 << 2)
#define PC_SLCTIN   (1 << 3)
#define PC_IRQEN    (1 << 4)



/*
 * par_init()
 *	"Constructor", returns zero on success, nonzero otherwise.
 */
int
par_init(struct par_port *self, int port_no)
{
	switch (port_no) {
	case 0:
		self->data    = 0x3BC;
		self->status  = 0x3BD;
		self->control = 0x3BE;
		break;
	case 1:
		self->data    = 0x378;
		self->status  = 0x379;
		self->control = 0x37A;
		break;
	case 2:
		self->data    = 0x278;
		self->status  = 0x279;
		self->control = 0x27A;
		break;
	default:
		return (-1);	/* return error */
	}
	self->mask = PC_SLCTIN | PC_INIT;
	self->quiet = 0;
	return enable_io(self->data, self->control);
}

/*
 * par_reset()
 *	Hard reset the printer. One could add code so that if one sends
 *	"par" SIGHUP, the printer would be reset (might be handy).
 */
void
par_reset(struct par_port *self)
{
	outportb(self->control, 0);
	__usleep(1);		/* XXX How long should this be? */
	outportb(self->control, self->mask);
}

/*
 * par_isready()
 *	The polling loop for sending one character. Implemented as busy
 *	waiting.
 *
 *	The count of retries in the loop is the timeout value
 *	for sending characters while the printer is filling its
 *	buffer. If the value is to small, printing will get
 *	extremely slow. If it is too high, unneccessary CPU time
 *	will get burned in the very moment the printer buffer
 *	fills up.
 */
parallel_status
par_isready(struct par_port *self)
{
	int cnt;
	int printer_status;

	for (cnt = 0; cnt < 1000; cnt++) {
		printer_status = inportb(self->status);
		if (printer_status & PS_BUSY) {
			/*
			 * /BUSY went high, indicating printer is no
			 * longer busy.
			 */
			self->last_error = "no errors";
			return P_OK;
		}
	}

	/*
	 * Printer timed out, checkout why,
	 */
	if (printer_status & PS_PE) {
		self->last_error = "paper empty";
		return P_ERROR;
	}
	if (!(printer_status & PS_SLCT)) {
		self->last_error = "printer offline";
	    	return P_ERROR;
	}
	if (!(printer_status & PS_ERROR)) {
		self->last_error = "printer error";
    		return P_ERROR;
	}

	self->last_error = "timeout";
	return P_TIMEOUT;
}

/*
 * par_putc()
 *	Send one character.
 *	According to my printers timing specs there should be a 0.5usec
 *	delay after each outportb(). I can "tune" my box to the
 *	"speed" of 3 waitstates on 8 bit I/O, which means one
 *	outportb() takes at least 0.5usec, if the multi I/O card
 *	doesn't slow it down even more. Therefore I don't see a
 *	need for additional delay loops.
 *	(1 / 8Mhz * 4 = 5E-7 sec)
 */
void par_putc(struct par_port *self, char c)
{
	outportb(self->data, c);
	outportb(self->control, self->mask | PC_STROBE);
	outportb(self->control, self->mask);
}
