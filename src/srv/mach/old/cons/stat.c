/*
 * stat.c : Handle the stat() and wstat() calls for the console driver.
 * 
 * Original code by Andy Valencia. Modified by G.T.Nicol for the the updated
 * console driver.
 */
#include "cons.h"
#include <string.h>

extern char    *perm_print();
extern int      accgen;

/*
 * con_stat() 
 *     Handle the console stat() call. 
 *
 * Simply build a message and return it to the sender.
 */
void
con_stat(struct msg * m, struct file * f)
{
	char            buf[MAXSTAT];

	sprintf(buf,
	    "size=%d\ntype=c\nowner=0\ninode=0\nrows=%d\ncols=%d\ngen=%d\n",
	   con_max_rows * con_max_cols, con_max_rows, con_max_cols, accgen);
	strcat(buf, perm_print(&con_prot));

	m->m_buf = buf;
	m->m_buflen = strlen(buf);
	m->m_nseg = 1;
	m->m_arg = m->m_arg1 = 0;

	msg_reply(m->m_sender, m);
}

/*
 * con_wstat() 
 *     Handle the wstat() call for the console driver.
 */
void
con_wstat(struct msg * m, struct file * f)
{
	char           *field, *val;

	/*
	 * See if common handling code can do it
	 */
	if (do_wstat(m, &con_prot, f->f_flags, &field, &val) == 0)
		return;

	/*
	 * Process each kind of field we can write
	 */
	if (!strcmp(field, "gen")) {
		/*
		 * Set access-generation field
		 */
		if (val) {
			accgen = atoi(val);
		} else {
			accgen += 1;
		}
		f->f_gen = accgen;
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

