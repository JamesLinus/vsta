/*
 * stat.c
 *	Do the stat function
 *
 * We also lump the chmod/chown stuff here as well
 */
#include "cons.h"
#include <sys/param.h>
#include <sys/perm.h>
#include <sys/fs.h>
#include <std.h>
#include <stdio.h>

extern char *perm_print(struct prot *);

/*
 * cons_stat()
 *	Do stat
 */
void
cons_stat(struct msg *m, struct file *f)
{
	char buf[MAXSTAT];

	if (f->f_screen == ROOTDIR) {
		sprintf(buf, "size=%d\ntype=d\nowner=0\ninode=0\n", NVTY);
	} else {
		struct screen *s = SCREEN(f->f_screen);

		sprintf(buf,
"size=%d\ntype=c\nowner=0\ninode=%d\nrows=%d\ncols=%d\ngen=%d\n"
"quit=%d\nintr=%d\npgrp=%lu\nisig=%d\n",
			s->s_nbuf, f->f_screen, ROWS, COLS, s->s_gen,
			s->s_quit, s->s_intr, (ulong)(s->s_pgrp),
			s->s_isig);
	}
	strcat(buf, perm_print(&cons_prot));
	m->m_buf = buf;
	m->m_buflen = strlen(buf);
	m->m_nseg = 1;
	m->m_arg = m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

/*
 * cons_wstat()
 *	Allow writing of supported stat messages
 */
void
cons_wstat(struct msg *m, struct file *f)
{
	char *field, *val;
	struct screen *s = SCREEN(f->f_screen);

	/*
	 * See if common handling code can do it
	 */
	if (do_wstat(m, &cons_prot, f->f_flags, &field, &val) == 0)
		return;

	/*
	 * Process each kind of field we can write
	 */
	if (!strcmp(field, "gen")) {
		/*
		 * Set access-generation field
		 */
		if (val) {
			s->s_gen = atoi(val);
		} else {
			s->s_gen += 1;
		}
		f->f_gen = s->s_gen;
	} else if (!strcmp(field, "screen")) {
		/*
		 * Force screen switch
		 */
		select_screen(atoi(val));
	} else if (!strcmp(field, "quit")) {
		/*
		 * Select keystroke which sends "abort" event
		 */
		s->s_quit = atoi(val);
	} else if (!strcmp(field, "intr")) {
		/*
		 * ... "intr" event
		 */
		s->s_intr = atoi(val);
	} else if (!strcmp(field, "pgrp")) {
		/*
		 * What process group will receive the keyboard
		 * signal.
		 */
		s->s_pgrp = atoi(val);
		s->s_pgrp_lead = f;
	} else if (!strcmp(field, "isig")) {
		/*
		 * Look for event keys?
		 */
		s->s_isig = f->f_isig = (atoi(val) != 0);
	} else {
		/*
		 * Not a field we support...
		 */
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * Return success
	 */
	m->m_buflen = m->m_nseg = m->m_arg = m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}
