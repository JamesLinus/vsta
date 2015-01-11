/*
 * root.c
 *	Routines for the root of the proc hierarchy
 */
#include "proc.h"
#include <sys/fs.h>
#include <stdlib.h>
#include <stdio.h>

void
proc_inval(struct msg *m, struct file *f)
{
	msg_err(m->m_sender, EINVAL);
}

void
proc_inval_rw(struct msg *m, struct file *f, uint x)
{
	msg_err(m->m_sender, EINVAL);
}

/*
 * root_open()
 *	Open a file in the root directory, i.e., a pid or the kernel stats
 */
static void
root_open(struct msg *m, struct file *f)
{
	if (!strcmp(m->m_buf, "kernel")) {
		f->f_pos = 0L;
		f->f_ops = &kernel_ops;
		m->m_buflen = m->m_nseg = m->m_arg = m->m_arg1 = 0;
		msg_reply(m->m_sender, m);
		return;
	}
	f->f_pid = atoi(m->m_buf);
	if (proc_pstat(f) == 0) {
		f->f_pos = 0L;
		f->f_ops = &proc_ops;
		m->m_buflen = m->m_nseg = m->m_arg = m->m_arg1 = 0;
		msg_reply(m->m_sender, m);
		return;
	}
	msg_err(m->m_sender, ESRCH);
}

/*
 * Read the root directory, i.e., the list of pids.  We actually did all the
 * pstat()s when first attached to the root.
 */
static void
root_read(struct msg *m, struct file *f, uint dummy)
{
	char *buf;
	int x, len;

	/*
	 * Get the latest pid list
	 */
	if (!f->f_active) {
		if ((f->f_active = proclist_pstat(f)) == -1) {
			f->f_active = 0;
			msg_err(m->m_sender, strerror());
		}
	}

	/*
	 * Get a buffer of the requested size, but put a sanity
	 * cap on it.
	 */
	len = MIN(m->m_arg, 256);
	if ((buf = malloc(len + 1)) == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}

	/*
	 * Copy as many pids as will fit into the buffer.
	 */
	buf[0] = '\0';
	if (!f->f_pos) {
		strcpy(buf, "kernel\n");
	}
	for (x = strlen(buf); x < len; ) {
		char pid[8];

		/*
		 * If EOF, return what we have.
		 */
		if (f->f_pos >= f->f_active) {
			f->f_active = 0;
			break;
		}

		/*
		 * If the next entry won't fit, return
		 * what we have.
		 */
		sprintf(pid, "%lu", f->f_proclist[f->f_pos]);
		if ((x + strlen(pid) + 1) >= len) {
			break;
		}

		/*
		 * Add entry and a newline.
		 */
		strcat(buf + x, pid);
		strcat(buf + x, "\n");
		x += (strlen(pid) + 1);
		f->f_pos++;
	}

	m->m_buf = buf;
	m->m_arg = m->m_buflen = x;
	m->m_nseg = (x ? 1 : 0);
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
	free(buf);
}

static void
root_stat(struct msg *m, struct file *f)
{
	char buf[MAXSTAT];

	if ((f->f_active = proclist_pstat(f)) == -1) {
		f->f_active = 0;
		msg_err(m->m_sender, strerror());
		return;
	}

	sprintf(buf, 
		"perm=1/1\nacc=7/0/0\nsize=%d\ntype=d\nowner=0\ninode=0\n",
		f->f_active);

	f->f_active = 0;
	m->m_buf = buf;
	m->m_arg = m->m_buflen = strlen(buf);
	m->m_nseg = 1;
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

struct file_ops root_ops = {
	root_open,
	proc_seek,	/* seek */
	root_read,	/* read */
	proc_inval_rw,	/* write */
	root_stat,	/* stat */
	proc_inval,	/* wstat */
};
