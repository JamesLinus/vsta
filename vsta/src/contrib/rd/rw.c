#include <alloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/param.h>
#include <sys/perm.h>
#include <sys/fs.h>
#include <fcntl.h>
#include "rd.h"

extern struct prot rd_prot;
extern struct node ramdisks[];

void rd_readdir(struct msg *m, struct file *f);
void rd_read(struct msg *m, struct file *f);
void rd_write(struct msg *m, struct file *f);

void
rd_rw(struct msg *m, struct file *f)
{	
	struct node *n = f->f_node;
	if(m->m_op == FS_READ)	{
	
		if(IS_DIR(n)) {
			rd_readdir(m,f);
			return;
		}
		
		rd_read(m,f);
		return;
	}
	else {
		if(IS_DIR(n) || (m->m_nseg != 1)) {
			msg_err(m->m_sender,EINVAL);
			return;
		}
		rd_write(m,f);
	}

	msg_err(m->m_sender,EINVAL);
	return;
}

void rd_read(struct msg *m, struct file *f) 
{
	struct node *n = f->f_node;

	if(f->f_pos >= n->n_size) {
		m->m_arg = m->m_arg1 = m->m_buflen = m->m_nseg = 0;
		msg_reply(m->m_sender, m);
		return;	
	}
	
	/* don't read past end of buffer */
 	if(m->m_arg > n->n_size - f->f_pos) {
	 	m->m_arg = n->n_size - f->f_pos;
 	}

 	m->m_buf = &n->n_buf[f->f_pos];
 	m->m_buflen = m->m_arg;
 	m->m_nseg = (m->m_arg ? 1 : 0);
 	m->m_arg1 = 0;
 	
 	f->f_pos += m->m_arg;
 	msg_reply(m->m_sender, m); 	
}
void rd_write(struct msg *m, struct file *f) 
{
	struct node *n = f->f_node;

	if(f->f_pos >= n->n_size) {
		m->m_arg = m->m_arg1 = m->m_buflen = m->m_nseg = 0;
		msg_reply(m->m_sender, m);
		return;	
	}
	
	/* don't read past end of buffer */
 	if(m->m_arg > n->n_size - f->f_pos) {
	 	m->m_arg = n->n_size - f->f_pos;
 	}

	seg_copyin(m->m_seg, m->m_nseg, n->n_buf + f->f_pos, m->m_arg);
	f->f_pos += m->m_arg;
	
	m->m_buflen = m->m_arg1 = m->m_nseg = 0;

	msg_reply(m->m_sender, m);
}

void
rd_readdir(struct msg *m, struct file *f)
{
	int len,x;
	char *buf;
	struct node *n = f->f_node;

	/*
	 * Get a buffer of the requested size, but put a sanity
	 * cap on it.
	 */
	len = m->m_arg;
	if (len > 256) {
		len = 256;
	}
	if ((buf = malloc(len+1)) == NULL) {
		msg_err(m->m_sender, strerror());
		return;
	}

	buf[0] = '\0';

	/*
	 * Assemble as many names as we have and will fit
	 */
	for (x = 0; x < len, f->f_pos < MAX_DISKS; ) {		
		struct node *n2;

		/*
		 * If the next entry won't fit, back up the file
		 * position and return what we have.
		 */
		n2 = &ramdisks[f->f_pos];
		if(n2->n_flags & NODE_VALID) {
			if ((x + strlen(n2->n_name) + 1) >= len) {
				break;
			}

			/*
			 * Add entry and a newline
			 */
			strcat(buf+x, n2->n_name);
			strcat(buf+x, "\n");
			x += (strlen(n2->n_name)+1);
		}
		f->f_pos += 1;
	}

	/*
	 * Send back results
	 */
	m->m_buf = buf;
	m->m_arg = m->m_buflen = x;
	m->m_nseg = (x ? 1 : 0);
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
	free(buf);
}
