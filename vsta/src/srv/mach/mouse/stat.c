/* 
 * stat.c 
 *    Handle the stat() and wstat() calls for the mouse driver.
 *
 * Copyright (C) 1993 by G.T.Nicol, all rights reserved.
 *
 * This code is based on the code for cons, which is based on the code
 * for the original cons driver.
 */

#include <sys/assert.h>
#include <string.h>
#include "mouse.h"

extern char *perm_print();

int oldx, oldy;		/* Last position seen for mouse */

/*
 * mouse_stat()
 *     Handle the mouse stat() call.
 *
 * Simply build a message and return it to the sender.
 */
void
mouse_stat(struct msg * m, struct file * f)
{
	char buf[MAXSTAT];
	mouse_pointer_data_t *p = &mouse_data.pointer_data;

	sprintf(buf, "type=x\nowner=0\ninode=0\ngen=%d\n"
			"x=%d\ny=%d\ndx=%d\ndy=%d\n"
			"minx=%d\nmaxx=%d\nminy=%d\nmaxy=%d\n"
			"period=%d\n"
			"left=%d\nmiddle=%d\nright=%d\n"
		, mouse_accgen,
		p->x, p->y, p->x - oldx, p->y - oldy,
		p->bx1, p->bx2, p->by1, p->by2,
		mouse_data.update_frequency,
		(p->buttons & MOUSE_LEFT_BUTTON) ? 1 : 0,
		(p->buttons & MOUSE_MIDDLE_BUTTON) ? 1 : 0,
		(p->buttons & MOUSE_RIGHT_BUTTON) ? 1 : 0
	);
	update_changes();
	strcat(buf, perm_print(&mouse_prot));

	m->m_buf = buf;
	m->m_buflen = strlen(buf);
	m->m_nseg = 1;
	m->m_arg = m->m_arg1 = 0;

	msg_reply(m->m_sender, m);
}

/*
 * mouse_wstat()
 *     Handle the wstat() call for the mouse driver.
 */
void
mouse_wstat(struct msg * m, struct file * f)
{
	char *field, *val;
	mouse_pointer_data_t *p = &mouse_data.pointer_data;
	int valint;

	/*
	 *  See if common handling code can do it
	 */
	if (do_wstat(m, &mouse_prot, f->f_flags, &field, &val) == 0)
		return;

	/*
	 * Get integer representation
	 */
	if (val) {
		valint = atoi(val);
	} else {
		valint = 0;
	}

	/*
	 *  Process each kind of field we can write
	 */
	if (!strcmp(field, "gen")) {

		/*
		 * Set access-generation field
		 */
		if (val) {
			mouse_accgen = valint;
		} else {
			mouse_accgen += 1;
		}
		f->f_gen = mouse_accgen;

	/*
	 * Set mouse position fields
	 */
	} else if (!strcmp(field, "minx")) {
		p->bx1 = valint;
	} else if (!strcmp(field, "maxx")) {
		p->bx2 = valint;
	} else if (!strcmp(field, "miny")) {
		p->by1 = valint;
	} else if (!strcmp(field, "maxy")) {
		p->by2 = valint;
	} else if (!strcmp(field, "x")) {
		p->x = valint;
	} else if (!strcmp(field, "y")) {
		p->y = valint;

	/*
	 * Miscellaneous mouse parameters
	 */
	} else if (!strcmp(field, "period")) {
		mouse_data.update_frequency = valint;
		(*mouse_data.functions.mouse_update)(valint);

	/*
	 *  Not a field we support
	 */
	} else {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 *  Return success
	 */
	m->m_buflen = m->m_nseg = m->m_arg = m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}
