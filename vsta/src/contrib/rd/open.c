#include <alloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/param.h>
#include <sys/perm.h>
#include <sys/fs.h>
#include "rd.h"

extern struct prot rd_prot;
extern struct node ramdisks[];

void 
rd_open(struct msg *m, struct file *f) 
{
	struct node *n = f->f_node;	
	struct node *n2;
	char *name;
	int i;

	if(!IS_DIR(n) || (strlen(m->m_buf) >= NAMESZ)) {
		msg_err(m->m_sender,EINVAL);
		return;
	}
	name = m->m_buf;
	
	for(i = 0; i < MAX_DISKS; i++) {
		n2 = &ramdisks[i];
				
		if(!strcmp(name,n2->n_name) && (n2->n_flags & NODE_VALID)) {
		
			f->f_node = n2;	
			f->f_pos = 0;
			m->m_buflen = m->m_nseg = m->m_arg = m->m_arg1 = 0;
			msg_reply(m->m_sender, m);
			return;
		}

	}

	msg_err(m->m_sender,EINVAL); 
}
