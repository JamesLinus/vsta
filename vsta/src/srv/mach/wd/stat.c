/*
 * stat.c
 *	Do the stat function
 */
#include <sys/param.h>
#include <sys/perm.h>
#include <sys/fs.h>
#include "wd.h"

extern char *perm_print();
extern struct wdparms parm[];
extern struct disk disks[];
extern struct prot wd_prot;

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
		if (part == WHOLE_DISK) {
			p = &disks[unit].d_prot;
		} else {
			p = &disks[unit].d_parts[part].p_prot;
		}
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
	char buf[MAXSTAT];
	uint size, node, part1;
	char type;
	struct prot *p;

	if (f->f_node == ROOTDIR) {
		size = NWD;
		node = 0;
		type = 'd';
	} else {
		uint part, unit;

		node = f->f_node;
		unit = NODE_UNIT(node);
		part = NODE_SLOT(node);
		if (part == WHOLE_DISK) {
			size = parm[unit].w_size;
		} else {
			size = disks[unit].d_parts[part].p_len;
		}
		size *= SECSZ;
		type = 's';
	}
	p = find_prot(f->f_node);
	sprintf(buf,
	 "size=%d\ntype=%c\nowner=1/1\ninode=%d\n", size, type, node);
	strcat(buf, perm_print(p));
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
