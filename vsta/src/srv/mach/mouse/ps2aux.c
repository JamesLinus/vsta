/* 
 * ps2aux.c
 *	Interrupt driven driver for PS/2 mouse
 *
 * Copyright (C) 1994 by David J. Hudson, all rights reserved.
 *
 * A lot of the code in this file is extracted from the other mouse drivers
 * written by Gavin Nicol and from the Linux ps2 mouse driver.  I hope I've
 * managed to keep the coding style consistent with the rest of the server!
 */
#include <stdio.h>
#include <syslog.h>
#include <std.h>
#include <mach/io.h>
#include "mouse.h"

/*
 * Flag that we're using a PS2-ish mouse with alternate decoding.
 * It looks bus mouse-like, but probes as a PS/2 controller.
 */
static int busmouse;

/*
 * Handle the queueing of commands
 */
struct ps2aux_rbuffer {
	uint head, tail;
	uchar data[PS2AUX_BUFFER_SIZE];
};

/*
 * Driver variables
 */
struct ps2aux_rbuffer *rbuffer = NULL;

/*
 * Our function table
 */
static mouse_function_table ps2aux_functions = {
	NULL,			/* mouse_poller_entry_point */
	ps2aux_interrupt,	/* mouse_interrupt */
	NULL,			/* mouse_update_period */
};

/*
 * ps2aux_poll_status()
 *	Check whether we have any problems with full input/output buffers
 *
 * We return zero if there's a problem, otherwise non-zero
 */
static int
ps2aux_poll_status(void)
{
	int retries = 0;
	uchar st;

	while (retries++ < PS2AUX_MAX_RETRIES) {
		st = inportb(PS2AUX_STATUS_PORT);
		if (!(st & (PS2AUX_OUTBUF_FULL | PS2AUX_INBUF_FULL))) {
			break;
		}
		if (st & PS2AUX_OUTBUF_FULL) {
			(void)inportb(PS2AUX_INPUT_PORT);
		}
		__msleep(100);
	}

	return(retries < PS2AUX_MAX_RETRIES);
}

/*
 * ps2aux_write_device()
 *	Write to the aux device
 */
static void
ps2aux_write_device(uchar val)
{
	(void)ps2aux_poll_status();
	outportb(PS2AUX_COMMAND_PORT, PS2AUX_WRITE_MAGIC);
	(void)ps2aux_poll_status();
	outportb(PS2AUX_OUTPUT_PORT, val);
}

/*
 * ps2aux_write_command()
 *	Write a command to the aux port
 */
static void
ps2aux_write_command(uchar cmd)
{
	(void)ps2aux_poll_status();
	outportb(PS2AUX_COMMAND_PORT, PS2AUX_COMMAND_WRITE);
	(void)ps2aux_poll_status();
	outportb(PS2AUX_OUTPUT_PORT, cmd);
}

/*
 * ps2aux_read_device()
 *	Read data back from the mouse port
 *
 * If we timeout we return zero, otherwise non-zero
 */
static int
ps2aux_read_device(uchar *val)
{
	int retries = 0;
	uchar st;

	while (retries++ < PS2AUX_MAX_RETRIES) {
		st = inportb(PS2AUX_STATUS_PORT);
		if (st & PS2AUX_OUTBUF_FULL) {
			break;
		}
		__msleep(100);
	}

	if (st & PS2AUX_OUTBUF_FULL) {
		*val = inportb(PS2AUX_INPUT_PORT); 
	}

	return(retries < PS2AUX_MAX_RETRIES);
}

/*
 * ps2aux_probe()
 *	Look to see if we can find a mouse on the aux port
 */
static int
ps2aux_probe(void)
{
	uchar ack;

	/*
	 * Send a reset and wait for an ack response!
	 */
	ps2aux_write_device(PS2AUX_RESET);
	if (!ps2aux_read_device(&ack) || (ack != PS2AUX_ACK)) {
		return (0);
	}

	/*
	 * Ensure that we see a bat (basic assurance test) response
	 */
	for (;;) {
		if (!ps2aux_read_device(&ack)) {
			return(0);
		}
		__msleep(100);

		/* XXX excess ACK? */
		if (ack == PS2AUX_ACK) {
			continue;
		}

		if (ack == PS2AUX_BAT) {
			break;
		}
	}

	/*
	 * Check that we get a pointing device ID back
	 */
	if (!ps2aux_read_device(&ack) || ack) {
		return (0);
	}

	return (1);
}

/*
 * ps2aux_interrupt()
 *	Handle a mouse interrupt.
 */
void
ps2aux_interrupt(void)
{
	int head, maxhead, diff;
	short dx, dy;
	mouse_pointer_data_t *p = &mouse_data.pointer_data;
	uchar buttons = p->buttons, changed;

	/*
	 * Arrange to read the pending information
	 */
	maxhead = (rbuffer->tail - 1) & (PS2AUX_BUFFER_SIZE - 1);
	head = rbuffer->head;
	rbuffer->data[head] = inportb(PS2AUX_INPUT_PORT);
	if (head != maxhead) {
		rbuffer->head = (rbuffer->head + 1) &
			(PS2AUX_BUFFER_SIZE - 1);
	}

	/*
	 * Have we got a full 3 bytes of position data?
	 * If so then start updating all of the positional stats
	 */
	diff = rbuffer->head - rbuffer->tail;
	if (diff < 0) {
		diff += PS2AUX_BUFFER_SIZE;
	}

	if (diff < 3) {
		return;
	}

#define BUF(idx) (rbuffer->data[(rbuffer->tail + (idx)) & \
		(PS2AUX_BUFFER_SIZE - 1)])

	/*
	 * Walk each set of three-byte packets
	 */
	while (diff >= 3) {

		/*
		 * Decode based on whether it's a strict PS/2 type of mouse,
		 * or a simpler busmouse-like encoding.
		 */
		if (busmouse) {

			/*
			 * Expect simple bus mouse interface
			 */
			buttons = BUF(1);
			dx = BUF(2);
			if (dx & 0x80) {
				dx -= 256;
			}
			dy = BUF(0);
			if (dy & 0x80) {
				dy -= 256;
			}
		} else {

			/*
			 * Standard PS/2 mouse
			 */
			buttons = BUF(0);
			if (!(buttons & PS2AUX_X_OVERFLOW)) {
				dx = BUF(1);
				if (buttons & PS2AUX_X_SIGN) {
					dx = dx - 256;
				}
			} else {
				dx = 255;
				if (buttons & PS2AUX_X_SIGN) {
					dx = -256;
				}
			}
			if (!(buttons & PS2AUX_Y_OVERFLOW)) {
				dy = BUF(2);
				if (buttons & PS2AUX_Y_SIGN) {
					dy = dy - 256;
				}
			} else {
				dy = 255;
				if (buttons & PS2AUX_Y_SIGN) {
					dy = -256;
				}
			}
		}

		/*
		 * Record consumption of this 3-byte packet, and
		 * update our dx/dy values.
		 */
		diff -= 3;
		rbuffer->tail =
			(rbuffer->tail + 3) & (PS2AUX_BUFFER_SIZE - 1);
		p->dx += dx;
		p->dy += dy;
		buttons &= 0x07;
	}
#undef BUF

	/*
	 * Map 1+3 -> 2, simulate middle button
	 */
	if ((buttons & MOUSE_LEFT_BUTTON) &&
			(buttons & MOUSE_RIGHT_BUTTON)) {
		buttons = MOUSE_MIDDLE_BUTTON;
	}

	/*
	 * Flag any change
	 */
	changed = p->dx || p->dy || (buttons != p->buttons);
	p->buttons = buttons;

	/*
	 * Notify if any change
	 */
	if (changed) {
		mouse_changed();
	}
}

/*
 * ps2aux_initialise()
 *	Initialise the mouse system.
 */
int
ps2aux_initialise(int argc, char **argv)
{
	int x;
	mouse_data_t *m = &mouse_data;

	/*
	 *  Initialise the system data.
	 */
	m->functions = ps2aux_functions;
	m->pointer_data.dx =
	m->pointer_data.dy = 0;
	m->pointer_data.buttons = 0;
	m->irq_number = PS2AUX_IRQ;
	m->update_frequency = 0;

	/*
	 * Parse options
	 */
	for (x = 1; x < argc; ++x) {
		if (!strcmp(argv[x], "-type")) {
			x += 1;
			continue;
		}
		if (!strcmp(argv[x], "-bus")) {
			busmouse = 1;
			continue;
		}
		fprintf(stderr, "%s: bad option '%s'\n", argv[0], argv[x]);
		exit(1);
	}

	/*
	 * Establish the receive ring buffer details
	 */
	rbuffer = malloc(sizeof(struct ps2aux_rbuffer));
	if (!rbuffer) {
		syslog(LOG_ERR, "unable to allocate ring buffer");
		return(-1);
	}

	/*
	 * Get our hardware ports
	 */
	if (enable_io(PS2AUX_LOW_PORT, PS2AUX_HIGH_PORT) < 0) {
		syslog(LOG_ERR, "unable to enable I/O ports for mouse");
		return(-1);
	}

	/*
	 * Check for the mouse
	 */
	if (!ps2aux_probe()) {
		return(-1);
	}

	/*
	 * We've found our pointing device, so now let's complete the
	 * initialisation of the driver
	 */
	rbuffer->head = 0;
	rbuffer->tail = 0;
	m->functions.mouse_poller_entry_point = NULL;
	m->enable_interrupts = TRUE;

	/*
	 * Enable mouse and go!
	 */
	outportb(PS2AUX_COMMAND_PORT, PS2AUX_ENABLE_CONTROLLER);
	ps2aux_write_device(PS2AUX_ENABLE_DEVICE);
	ps2aux_write_command(PS2AUX_ENABLE_INTERRUPTS);

	syslog(LOG_INFO, "PS/2 mouse detected and installed");
	return (0);
}
