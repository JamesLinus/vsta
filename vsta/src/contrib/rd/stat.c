#include <stdio.h>
#include <sys/param.h>
#include <sys/perm.h>
#include <sys/fs.h>
#include <fcntl.h>
#include "rd.h"

extern struct prot rd_prot;
extern struct node ramdisks[];

void
rd_stat(struct msg *m, struct file *f)
{
	char buf[MAXSTAT];
	struct node *n = f->f_node;
	int len;
	int i;
	
	/* verify access */	
	if(!(f->f_flags & ACC_READ)) {
		msg_err(m->m_sender, EPERM);
		return;
	}
	/* calc length */
	if(IS_DIR(n)) {
		
		len = 0;
		for(i = 0; i < MAX_DISKS; i++) {
			if(ramdisks[i].n_flags & NODE_VALID) {
				len++;
			}
		}
	}
	else {
		len = n->n_size;
	}
	sprintf(buf,"size=%d\ntype=%c\nowner=%d\ninode=%u\n",
		len,IS_DIR(n) ? 'd' : 'f', 0, n);
	strcat(buf,perm_print(&rd_prot));
	m->m_buf = buf;
	m->m_arg = m->m_buflen = strlen(buf);
	m->m_nseg = 1;
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

