/* 
 * ms_bus.c
 *	Interrupt driven driver for the NEC bus mouse.
 *
 * Copyright (C) 1993 by G.T.Nicol, all rights reserved.
 *
 * A lot of this code is just taken from the Linux source.
 */
#include <stdio.h>
#include <syslog.h>
#include "mouse.h"

/*
 * Our function table
 */
static mouse_function_table ms_bus_functions = {
	NULL,			/* mouse_poller_entry_point */
	ms_bus_interrupt,	/* mouse_interrupt */
	NULL,			/* mouse_update_period */
};

/*
 * ms_bus_interrupt()
 *	Handle a mouse interrupt.
 */
void
ms_bus_interrupt(void)
{
	short new_x, new_y;
	uchar dx, dy, buttons, changed = 0;
	mouse_pointer_data_t *p = &mouse_data.pointer_data;

	new_x = p->x;
	new_y = p->y;

	outportb(MS_BUS_CONTROL_PORT, MS_BUS_COMMAND_MODE);
	outportb(MS_BUS_DATA_PORT, (inportb(MS_BUS_DATA_PORT) | 0x20));

	outportb(MS_BUS_CONTROL_PORT, MS_BUS_READ_X);
	dx = inportb(MS_BUS_DATA_PORT);

	outportb(MS_BUS_CONTROL_PORT, MS_BUS_READ_Y);
	dy = inportb(MS_BUS_DATA_PORT);

	outportb(MS_BUS_CONTROL_PORT, MS_BUS_READ_BUTTONS);
	buttons = ~(inportb(MS_BUS_DATA_PORT)) & 0x07;

	outportb(MS_BUS_CONTROL_PORT, MS_BUS_COMMAND_MODE);
	outportb(MS_BUS_DATA_PORT, (inportb(MS_BUS_DATA_PORT) & 0xdf));

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
	 * Not sure about this... but I assume that the MS mouse is the
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
		break;
	}

	/*
	 * Record change, update, and notify of button or position
	 * changes
	 */
	changed |= (buttons != p->buttons);
	p->buttons = buttons;
	if (changed) {
		mouse_changed();
	}
}

/*
 * ms_bus_initialise()
 *	Initialise the mouse system.
 */
int
ms_bus_initialise(int argc, char **argv)
{
	int mouse_seen = FALSE;
	int loop, dummy;
	mouse_data_t *m = &mouse_data;

	/*
	 *  Initialise the system data.
	 */
	m->functions = ms_bus_functions;
	m->pointer_data.x = 320;
	m->pointer_data.y = 320;
	m->pointer_data.buttons = 0;
	m->pointer_data.bx1 = 0;
	m->pointer_data.by1 = 0;
	m->pointer_data.bx2 = 639;
	m->pointer_data.by2 = 399;
	m->irq_number = MS_BUS_IRQ;
	m->update_frequency = 0;

	/*
	 * Parse our args...
	 */
	for (loop = 1; loop < argc; loop++) {
		if (strcmp(argv[loop], "-x_size") == 0) {
			if(++loop == argc){
				syslog(LOG_ERR, "bad -x_size parameter");
				break;
			}
			m->pointer_data.x = atoi(argv[loop]) / 2;
			m->pointer_data.bx2 = atoi(argv[loop]);
		}
		if (strcmp(argv[loop], "-y_size") == 0) {
			if(++loop == argc){
				syslog(LOG_ERR, "bad -y_size parameter");
				break;
			}
			m->pointer_data.y   = atoi(argv[loop])/2;
			m->pointer_data.by2 = atoi(argv[loop]);
		}
	}

	/*
	 * Get our hardware ports.
	 */
	if (enable_io(MICROSOFT_LOW_PORT, MICROSOFT_HIGH_PORT) < 0) {
		syslog(LOG_ERR, "unable to enable I/O ports for mouse");
		return (-1);
	}

	/*
	 * Check for the mouse.
	 */
	if (inportb(MS_BUS_ID_PORT) == 0xde) {
		__msleep(100);
		dummy = inportb(MS_BUS_ID_PORT);
		for (loop = 0; loop < 4; loop++) {
			__msleep(100);
			if (inportb(MS_BUS_ID_PORT) == 0xde) {
				__msleep(100);
				if (inportb(MS_BUS_ID_PORT) == dummy) {
					mouse_seen = TRUE;
				} else {
					mouse_seen = FALSE;
				}
			} else {
				mouse_seen = FALSE;
			}
		}
	}

	if (mouse_seen == FALSE) {
		return(-1);
	}

	m->functions.mouse_poller_entry_point = NULL;
	m->enable_interrupts = TRUE;

	/*
	 * Enable mouse and go!
	 */
	outportb(MS_BUS_CONTROL_PORT, MS_BUS_START);
	MS_BUS_INT_ON();

	syslog(LOG_INFO, "Microsoft bus mouse detected and installed");
	return (0);
}
