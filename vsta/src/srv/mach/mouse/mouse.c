/* 
 * mouse.c
 *    A fairly universal mouse driver.
 *
 * Copyright (C) 1993 by G.T.Nicol, all rights reserved.
 */
#include <sys/fs.h>
#include <stdio.h>
#include <std.h>
#include <syslog.h>
#include <string.h>
#include "mouse.h"

static long sleeper;	/* Who's waiting for mouse events? */
static int oldbut = -1;	/* Last seen state of mouse buttons */

/*
 * We use the following table to bootstrap the system. The command
 * line is searched for the -type argument, and that is used to index
 * into this table. The initialisation code is called, and that
 * sets uo the rest of the system parameters.
 */
struct {
	char *driver_name;
	int (*driver_initialise)(int, char **);
} mouse_drivers[] = {
	{ "pc98_bus",		pc98_bus_initialise},
	{ "microsoft_bus",	ms_bus_initialise},
	{ "logitech_bus",	logitech_bus_initialise},
	{ "serial",		ibm_serial_initialise},
	{ "ps2aux",		ps2aux_initialise},
	{ NULL,               NULL,}
};

/*
 * We use the following table to hold various bits and peices of data
 * about the mouse.
 */
mouse_data_t mouse_data = {
   {
      NULL,                              /* mouse_poller_entry_point      */
      NULL,                              /* mouse_interrupt               */
      NULL,                              /* mouse_update_period           */
   },
   {
      0,                                 /* X coordinate                  */
      0,                                 /* Y coordinate                  */
      0,                                 /* Pressed buttons               */
   },
   -1,                                   /* irq_number                    */
   -1,                                   /* update_frequency              */
   FALSE,                                /* enable_interrupts             */
   -1,                                    /* type_id                       */
};

/*
 * mouse_initialise()
 *	Parse the command line and initialise the mouse driver
 */
void
mouse_initialise(int argc, char **argv)
{
	int loop, param = -1;

	/*
	 *  Look for a -type parameter. If none is found, die.
	 */
	for (loop = 1; loop < argc; loop++) {
		if (!strcmp(argv[loop], "-type") && (loop++ < argc)) {
			param = loop;
		}
	}
	if (param == -1) {
		syslog(LOG_ERR, "no mouse type specified - exiting");
		exit(1);
	}

	/*
	 * Use the parameter to look up the driver in the table.
	 * Die if not found.
	 */
	for (loop = 0; mouse_drivers[loop].driver_name != NULL; loop++) {
		if (!strcmp(mouse_drivers[loop].driver_name, argv[param]))
			break;
	}
	if (mouse_drivers[loop].driver_initialise == NULL) {
		syslog(LOG_ERR, "no driver for '%s' type mouse--exiting",
			argv[param]);
		exit(1);
	}
	mouse_data.type_id = loop;

	/*
	 * Initialise the driver
	 */
	if ((*mouse_drivers[loop].driver_initialise) (argc, argv) == -1) {
		syslog(LOG_ERR, "unable to initialise--exiting");
		exit(1);
	}
}

/*
 * update_changes()
 *	Record current mouse state
 */
void
update_changes(void)
{
	mouse_pointer_data_t *p = &mouse_data.pointer_data;

	oldbut = p->buttons;
	p->dx = p->dy = 0;
}

/*
 * check_changes()
 *	Tell if any mouse state has changed
 */
static int
check_changes(void)
{
	mouse_pointer_data_t *p = &mouse_data.pointer_data;

	return((oldbut != p->buttons) || p->dx || p->dy);
}

/*
 * mouse_changed()
 *	Notified when mouse events have occurred
 */
void
mouse_changed(void)
{
	struct msg m;

	/*
	 * Nothing of note, or nobody cares, just get out
	 */
	if (!sleeper || !check_changes()) {
		return;
	}

	/*
	 * Wake up our sleeper
	 */
	m.m_arg = 1;
	m.m_nseg = m.m_arg1 = 0;
	msg_reply(sleeper, &m);
	sleeper = 0;
}

/*
 * mouse_read()
 *	Serve read requests from the mouse device
 *
 * Reading is simply a way for clients to block until something changes
 * on the mouse.  A read length of 0 is a query of status (never blocks),
 * and any other value causes a block until the mouse "has changed".
 *
 * Return value is 1 if there's a change, 0 otherwise.  No data is
 * actually returned, and nseg will thus be 0.
 */
void
mouse_read(struct msg *m, struct file *f)
{
	int changed;

	/*
	 * Figure out if anything interesting has happened
	 */
	changed = check_changes();

	/*
	 * If we're going to send a message right now, fill in
	 * the body.  Hit the current requester, and any previous
	 * sleeper.
	 */
	if (changed || !m->m_arg) {
		m->m_nseg = m->m_arg1 = 0;
		m->m_arg = (changed ? 1 : 0);
		msg_reply(m->m_sender, m);
		if (changed && sleeper) {
			msg_reply(sleeper, m);
			sleeper = 0;
		}

		/*
		 * All done
		 */
		return;
	}

	/*
	 * If we need to sleep, but somebody's already there,
	 * return an error.
	 */
	if (sleeper) {
		msg_err(m->m_sender, EBUSY);
		return;
	}

	/*
	 * "We'll be in touch"
	 */
	sleeper = m->m_sender;
}

/*
 * mouse_update()
 *	Mouse changes have been detected; update and wakeup
 *
 * This entry is used when a second thread is used to detect mouse
 * changes.  The thread detects them, then bundles up the new state
 * and sends it to this interface.
 */
void
mouse_update(mouse_pointer_data_t *m)
{
	mouse_pointer_data_t *p = &mouse_data.pointer_data;

	p->dx += m->dx;
	p->dy += m->dy;
	p->buttons = m->buttons;
	mouse_changed();
}
