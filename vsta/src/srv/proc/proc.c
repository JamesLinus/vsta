/*
 * proc.c
 *	Routines for moving downwards in the hierarchy
 */
#include "proc.h"
#include <sys/fs.h>
#include <sys/proc.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

extern char *perm_print();
extern int notify();

struct file_ops no_ops = {
	proc_inval,		/* open */
	proc_inval,		/* seek */
	proc_inval_rw,		/* read */
	proc_inval_rw,		/* write */
	proc_inval,		/* stat */
	proc_inval,		/* wstat */
};

/*
 * note_write()
 */
static void
note_write(struct msg *m, struct file *f, uint len)
{
	char *event;
	
	event = malloc(len + 1);
	seg_copyin(m->m_seg, m->m_nseg, event, len);
	event[len] = '\0';
	
	emulate_client_perms(f);
	notify(f->f_pid, 0, event);
	release_client_perms(f);

	m->m_buflen = m->m_arg1 = m->m_nseg = 0;
	m->m_arg = len;
	msg_reply(m->m_sender, m);
}

/*
 * note_stat()
 */
static void
note_stat(struct msg *m, struct file *f)
{
	char buf[MAXSTAT];

	sprintf(buf, "size=0\ntype=f\nowner=0\ninode=%ld\n", f->f_pid);
	strcat(buf, perm_print(&f->f_prot));

	m->m_buf = buf;
	m->m_arg = m->m_buflen = strlen(buf);
	m->m_nseg = 1;
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

struct file_ops note_ops = {
	proc_inval,		/* open */
	proc_inval,		/* seek */
	proc_inval_rw,		/* read */
	note_write,		/* write */
	note_stat,		/* stat */
	proc_inval,		/* wstat */
};

/*
 * notepg_write()
 */
static void
notepg_write(struct msg *m, struct file *f, uint len)
{
	char *event;
	
	event = malloc(len);
	seg_copyin(m->m_seg, m->m_nseg, event, len);
	event[len] = '\0';
	
printf("notepg_write %x pid %d '%s'\n", f, f->f_pid, event);
	
/*
 *	kills init, too!
 */
	emulate_client_perms(f);
	notify(f->f_pid, -1, event);
	release_client_perms(f);

	m->m_buflen = m->m_arg1 = m->m_nseg = 0;
	m->m_arg = len;
	msg_reply(m->m_sender, m);
}

/*
 * notepg_stat()
 */
static void
notepg_stat(struct msg *m, struct file *f)
{
	char buf[MAXSTAT];

	sprintf(buf, "nsize=0\ntype=f\nowner=0\ninode=%ld\n", f->f_pid);
	strcat(buf, perm_print(&f->f_prot));

	m->m_buf = buf;
	m->m_arg = m->m_buflen = strlen(buf);
	m->m_nseg = 1;
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

struct file_ops notepg_ops = {
	proc_inval,		/* open */
	proc_inval,		/* seek */
	proc_inval_rw,		/* read */
	notepg_write,		/* write */
	notepg_stat,		/* stat */
	proc_inval,		/* wstat */
};

/*
 * statename()
 *	Provide a string to define the current process run status
 */
static char *
statename(struct pstat_proc *p)
{
	if (p->psp_nonproc > 0) {
		return("ONPROC");
	} else if (p->psp_nrun > 0) {
		return("RUN");
	} else {
		return("SLP");
	}
}

/*
 * status_read()
 */
static void
status_read(struct msg *m, struct file *f, uint len)
{
	char buffer[80];
	char *buf;
	int x, cnt;

	/*
	 * Generate our "contents"
	 */
	buf = &buffer[0];
	if (f->f_active == 0) {
		if (proc_pstat(f) == 0) {
			f->f_active = 1;
		} else {
			f->f_active = 0;
			msg_err(m->m_sender, strerror());
			return;
		}
	}

	sprintf(buf, "%-6d %-8s %-6s %7d %5d/%-5d ", f->f_pid,
		f->f_proc.psp_cmd, statename(&f->f_proc),
		f->f_proc.psp_nthread, f->f_proc.psp_usrcpu,
		f->f_proc.psp_syscpu);
	for (x = 0; x < f->f_proc.psp_prot.prot_len; x++) {
		if (x != 0) {
			strcat(buf, ".");
		}
		sprintf(&buf[strlen(buf)], "%d",
			f->f_proc.psp_prot.prot_id[x]);
	}
	strcat(buf, "\n");
	buf += f->f_pos;
	
	/*
	 * Calculate # bytes to get
	 */
	cnt = m->m_arg;
	if (cnt >= strlen(buf)) {
		cnt = strlen(buf);
		f->f_active = 0;
	}
	
	/*
	 * EOF?
	 */
	if (cnt <= 0) {
		m->m_arg = m->m_arg1 = m->m_buflen = m->m_nseg = 0;
		msg_reply(m->m_sender, m);
		return;
	}

	/*
	 * Send back reply.
	 */
	m->m_buf = buf;
	m->m_arg = m->m_buflen = cnt;
	m->m_nseg = 1;
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
	f->f_pos += cnt;
}

/*
 * status_stat()
 */
static void
status_stat(struct msg *m, struct file *f)
{
	char buf[MAXSTAT];

	sprintf(buf, "size=0\ntype=f\nowner=0\ninode=%ld\n", f->f_pid);
	strcat(buf, perm_print(&f->f_prot));

	m->m_buf = buf;
	m->m_arg = m->m_buflen = strlen(buf);
	m->m_nseg = 1;
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

struct file_ops status_ops = {
	proc_inval,		/* open */
	proc_inval,		/* seek */
	status_read,		/* read */
	proc_inval_rw,		/* write */
	status_stat,		/* stat */
	proc_inval,		/* wstat */
};

/*
 * proc_open()
 *	Open a file in a proc directory, i.e., "note", "status", etc.
 */
static void
proc_open(struct msg *m, struct file *f)
{
	if (!strcmp(m->m_buf, "ctl")) {
		f->f_ops = &no_ops;
	} else if (!strcmp(m->m_buf, "note")) {
		f->f_ops = &note_ops;
	} else if (!strcmp(m->m_buf, "notepg")) {
		f->f_ops = &notepg_ops;
	} else if (!strcmp(m->m_buf, "status")) {
		f->f_ops = &status_ops;
	} else {
		msg_err(m->m_sender, ESRCH);
	}
	f->f_pos = 0;
	m->m_buflen = m->m_nseg = m->m_arg = m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

/*
 * proc_read()
 *	Read a proc, i.e., the list of files under each pid.
 */
static void
proc_read(struct msg *m, struct file *f, uint len)
{
	char buffer[80];
	char *buf;
	int x, cnt;
	
	/*
	 * Generate our "contents"
	 */
	x = 0;
	buf = &buffer[0];
	sprintf(buf, "ctl\nnote\nnotepg\nstatus\n");
	buf += f->f_pos;
	
	/*
	 * Calculate # bytes to get
	 */
	cnt = m->m_arg;
	if (cnt > strlen(buf)) {
		cnt = strlen(buf);
	}
	
	/*
	 * EOF?
	 */
	if (cnt <= 0) {
		m->m_arg = m->m_arg1 = m->m_buflen = m->m_nseg = 0;
		msg_reply(m->m_sender, m);
		return;
	}

	/*
	 * Send back reply.
	 */
	m->m_buf = buf;
	m->m_arg = m->m_buflen = cnt;
	m->m_nseg = 1;
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
	f->f_pos += cnt;
}

/*
 * proc_stat()
 *	Get status for the process directory entries
 */
static void
proc_stat(struct msg *m, struct file *f)
{
	char buf[MAXSTAT];

	sprintf(buf, "nsize=4\ntype=d\nowner=0\ninode=%ld\n", f->f_pid);
	strcat(buf, perm_print(&f->f_prot));

	m->m_buf = buf;
	m->m_arg = m->m_buflen = strlen(buf);
	m->m_nseg = 1;
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

struct file_ops proc_ops = {
	proc_open,		/* open */
	proc_seek,		/* seek */
	proc_read,		/* read */
	proc_inval_rw,		/* write */
	proc_stat,		/* stat */
	proc_inval,		/* wstat */
};

/*
 * kernel_read()
 */
static void
kernel_read(struct msg *m, struct file *f, uint len)
{
	char buffer[80];
	char *buf;
	int x, cnt;

	/*
	 * Generate our "contents"
	 */
	kernel_pstat(f);
	buf = &buffer[0];
	sprintf(buf, "%d %d %d %d %d %d", f->f_kern.psk_ncpu,
		f->f_kern.psk_memory, f->f_kern.psk_freemem,
		f->f_kern.psk_runnable, f->f_kern.psk_uptime.t_sec,
		f->f_kern.psk_uptime.t_usec);
	for (x = 0; x < f->f_proc.psp_prot.prot_len; x++) {
		if (x != 0) {
			strcat(buf, ".");
		}
		sprintf(&buf[strlen(buf)], "%d",
			f->f_proc.psp_prot.prot_id[x]);
	}
	strcat(buf, "\n");
	buf += f->f_pos;
	
	/*
	 * Calculate # bytes to get
	 */
	cnt = m->m_arg;
	if (cnt >= strlen(buf)) {
		cnt = strlen(buf);
		f->f_active = 0;
	}
	
	/*
	 * EOF?
	 */
	if (cnt <= 0) {
		m->m_arg = m->m_arg1 = m->m_buflen = m->m_nseg = 0;
		msg_reply(m->m_sender, m);
		return;
	}

	/*
	 * Send back reply.
	 */
	m->m_buf = buf;
	m->m_arg = m->m_buflen = cnt;
	m->m_nseg = 1;
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
	f->f_pos += cnt;
}

/*
 * kernel_stat()
 *	Get status for the kernel information
 */
static void
kernel_stat(struct msg *m, struct file *f)
{
	char buf[MAXSTAT];

	kernel_pstat(f);
	sprintf(buf,
		"nsize=1\ntype=f\nowner=0\ninode=%d\nperm=1\nacc=70/0\n"
		"mem=%u\nfree=%u\nnrun=%u\nuptime=%u\n",
		INT_MAX,
		f->f_kern.psk_memory, f->f_kern.psk_freemem,
		f->f_kern.psk_runnable, f->f_kern.psk_uptime.t_sec);
	m->m_buf = buf;
	m->m_arg = m->m_buflen = strlen(buf);
	m->m_nseg = 1;
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

struct file_ops kernel_ops = {
	proc_inval,		/* open */
	proc_inval,		/* seek */
	kernel_read,		/* read */
	proc_inval_rw,		/* write */
	kernel_stat,		/* stat */
	proc_inval,		/* wstat */
};

