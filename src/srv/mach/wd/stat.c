/*
 * stat.c
 *	Do the stat function
 */
#include <sys/param.h>
#include <sys/perm.h>
#include <sys/fs.h>
#include <std.h>
#include <stdio.h>
#include "wd.h"

extern char *perm_print();

/*
 * find_prot()
 *	Given node, return pointer to right prot structure
 */
static struct prot *
find_prot(int node)
{
	struct prot *p;
	uint unit, part;

	if (node == ROOTDIR) {
		p = &wd_prot;
	} else {
		unit = NODE_UNIT(node);
		part = NODE_SLOT(node);
		p = &disks[unit].d_parts[part]->p_prot;
	}
	return(p);
}

/*
 * wd_stat()
 *	Do stat
 */
void
wd_stat(struct msg *m, struct file *f)
{
	char buf[MAXSTAT], buf2[MAXSTAT];
	uint size, node;
	char type;
	int isdir = (f->f_node == ROOTDIR);

	if (isdir) {
		int x;

		size = 0;
		node = 0;
		type = 'd';
		
		/*
		 * Work out the dir size
		 */
		for (x = 0; x < NWD; x++) {
			if (disks[x].d_configed) {
				int y;
				
				for (y = 0; y < MAX_PARTS; y++) {
					if (disks[x].d_parts[y]
					    && disks[x].d_parts[y]->p_val) {
						size++;
					}
				}
			}
		}
		sprintf(buf2, "irq=%d\nbaseio=0x%x\n", wd_irq, wd_baseio);
	} else {
		uint part, unit;

		node = f->f_node + UNITSTEP;
		unit = NODE_UNIT(f->f_node);
		part = NODE_SLOT(f->f_node);
		size = disks[unit].d_parts[part]->p_len;
		size *= SECSZ;
		type = 's';
		sprintf(buf2, "pextoffs=%d\nptype=%d\n",
			disks[unit].d_parts[part]->p_extoffs,
			disks[unit].d_parts[part]->p_type);
	}
	sprintf(buf,
		"size=%d\ntype=%c\nowner=1/1\ninode=%d\ndev=%d\n"
		"dma=%d\n",
		size, type, node, wdname, isdir ? 0 : 1);
	strcat(buf, buf2);
	strcat(buf, perm_print(find_prot(f->f_node)));
	m->m_buf = buf;
	m->m_buflen = strlen(buf);
	m->m_nseg = 1;
	m->m_arg = m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

/*
 * wd_wstat()
 *	Allow writing of supported stat messages
 */
void
wd_wstat(struct msg *m, struct file *f)
{
	char *field, *val;
	struct prot *p;

	/*
	 * Get pointer to right protection structure
	 */
	p = find_prot(f->f_node);

	/*
	 * See if common handling code can do it
	 */
	if (do_wstat(m, p, f->f_flags, &field, &val) == 0) {
		return;
	}

	/*
	 * Otherwise forget it
	 */
	msg_err(m->m_sender, EINVAL);
}
