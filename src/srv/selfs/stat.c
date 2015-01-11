/*
 * stat.c
 *	Do the stat function
 */
#include "selfs.h"
#include <sys/param.h>
#include <sys/fs.h>
#include <string.h>
#include <stdio.h>
#include <syslog.h>

/*
 * modestr()
 *	Return a string value for f_mode
 */
static char *
modestr(uint mode)
{
	switch (mode) {
	case MODE_ROOT:
		return "root";
	case MODE_CLIENT:
		return "client";
	case MODE_SERVER:
		return "server";
	default:
#ifdef DEBUG
		syslog(LOG_DEBUG, "unknown client mode %u", mode);
#endif
		return "unknown";
	}
}

/*
 * selfs_stat()
 *	Do stat
 */
void
selfs_stat(struct msg *m, struct file *f)
{
	char buf[MAXSTAT];

	sprintf(buf, "mode=%s\nclid=%lu\nkey=%lu\n",
		modestr(f->f_mode), m->m_sender, f->f_key);
	m->m_buf = buf;
	m->m_arg = m->m_buflen = strlen(buf);
	m->m_nseg = 1;
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}
