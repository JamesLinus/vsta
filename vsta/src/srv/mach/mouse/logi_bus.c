/* 
 * logi_bus.c
 *	Interrupt driven driver for the NEC bus mouse.
 *
 * Copyright (C) 1993 by G.T.Nicol, all rights reserved.
 *
 * A lot of this code is just taken from the Linux source.
 */
#include <stdio.h>
#include <syslog.h>
#include <time.h>
#include <mach/io.h>
#include "mouse.h"

/*
 * Our function table
 */
static mouse_function_table logitech_bus_functions = {
	NULL,				/* mouse_poller_entry_point */
	logitech_bus_interrupt,		/* mouse_interrupt */
	NULL,				/* mouse_update_period */
};

/*
 * logitech_bus_interrupt()
 *	Handle a mouse interrupt.
 */
void
logitech_bus_interrupt(void)
{
	short new_x, new_y;
	uchar dx, dy, buttons, changed = 0;
	mouse_pointer_data_t *p = &mouse_data.pointer_data;

	new_x = p->x;
	new_y = p->y;

	LOGITECH_BUS_INT_OFF();
	outportb(LOGITECH_BUS_CONTROL_PORT, LOGITECH_BUS_READ_X_LOW);
	dx = (inportb(LOGITECH_BUS_DATA_PORT) & 0xf);
	outportb(LOGITECH_BUS_CONTROL_PORT, LOGITECH_BUS_READ_X_HIGH);
	dx |= (inportb(LOGITECH_BUS_DATA_PORT) & 0xf) << 4;
	outportb(LOGITECH_BUS_CONTROL_PORT, LOGITECH_BUS_READ_Y_LOW);
	dy = (inportb(LOGITECH_BUS_DATA_PORT) & 0xf);
	outportb(LOGITECH_BUS_CONTROL_PORT, LOGITECH_BUS_READ_Y_HIGH);
	buttons = inportb(LOGITECH_BUS_DATA_PORT);
	dy |= (buttons & 0xf) << 4;
	buttons = ((buttons >> 5) & 0x07);

	/*
	 *  If they've changed, update  the current coordinates
	 */
	if (dx || dy) {
		new_x += dx;
		new_y += dy;

		/*
		 *  Make sure we honour the bounding box
		 */
		if (new_x < p->bx1)
			new_x = p->bx1;
		if (new_x > p->bx2)
			new_x = p->bx2;
		if (new_y < p->by1)
			new_y = p->by1;
		if (new_y > p->by2)
			new_y = p->by2;

		/*
		 *  Set up the new mouse position
		 */
		p->x = new_x;
		p->y = new_y;
		changed = 1;
	}

	/*
	 * Not sure about this... but I assume that the LOGITECH mouse is the
	 * same as the NEC one.
	 */
	switch (buttons) {	/* simulate a 3 button mouse here */
	case 4:
		buttons = MOUSE_LEFT_BUTTON;
		break;
	case 1:
		buttons = MOUSE_RIGHT_BUTTON;
		break;
	case 0:
		buttons = MOUSE_MIDDLE_BUTTON;
		break;
	default:
		buttons = 0;
	};

	/*
	 * Record button change, update button
	 */
	changed |= (buttons != p->buttons);
	p->buttons = buttons;

	/*
	 * Enable interrupts, and notify if any change
	 */
	LOGITECH_BUS_INT_ON();
	if (changed) {
		mouse_changed();
	}
}

/*
 * logitech_bus_initialise()
 *    Initialise the mouse system.
 */
int
logitech_bus_initialise(int argc, char **argv)
{
	int loop;
	mouse_data_t *m = &mouse_data;

	/*
	 *  Initialise the system data.
	 */
	m->functions = logitech_bus_functions;
	m->pointer_data.x = 320;
	m->pointer_data.y = 320;
	m->pointer_data.buttons = 0;
	m->pointer_data.bx1 = 0;
	m->pointer_data.by1 = 0;
	m->pointer_data.bx2 = 639;
	m->pointer_data.by2 = 399;
	m->irq_number = LOGITECH_BUS_IRQ;
	m->update_frequency = 0;

	/*
	 * Parse our args...
	 */
	for (loop = 1; loop < argc; loop++) {
		if (strcmp(argv[loop], "-x_size") == 0) {
			if (++loop == argc) {
				syslog(LOG_ERR, "bad -x_size parameter");
				break;
			}
			m->pointer_data.x = atoi(argv[loop]) / 2;
			m->pointer_data.bx2 = atoi(argv[loop]);
		} else if (strcmp(argv[loop], "-y_size") == 0) {
			if (++loop == argc) {
				syslog(LOG_ERR, "bad -y_size parameter");
				break;
			}
			m->pointer_data.y = atoi(argv[loop]) / 2;
			m->pointer_data.by2 = atoi(argv[loop]);
		}
	}

	/*
	 * Get our hardware ports.
	 */
	if (enable_io(LOGITECH_LOW_PORT, LOGITECH_HIGH_PORT) < 0) {
		syslog(LOG_ERR, "unable to enable I/O ports for mouse");
		return (-1);
	}

	/*
	 * Check for the mouse.
	 */
	outportb(LOGITECH_BUS_CONFIG_PORT, LOGITECH_BUS_CONFIG_BYTE);
	outportb(LOGITECH_BUS_ID_PORT, LOGITECH_BUS_ID_BYTE);
	__msleep(100);
	if (inportb(LOGITECH_BUS_ID_PORT) != LOGITECH_BUS_ID_BYTE) {
		return(-1);
	}
	m->functions.mouse_poller_entry_point = NULL;
	m->enable_interrupts = TRUE;

	/*
	 * Enable mouse and go!
	 */
	outportb(LOGITECH_BUS_CONFIG_PORT, LOGITECH_BUS_CONFIG_BYTE);
	LOGITECH_BUS_INT_ON();

	syslog(LOG_INFO, "Logitech bus mouse detected and installed");
	return (0);
}
