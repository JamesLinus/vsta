/*
 * stat.c
 *	Do the stat function
 */
#include <sys/param.h>
#include <sys/perm.h>
#include <sys/fs.h>
#include <llist.h>
#include <stdio.h>
#include <std.h>
#include "ne.h"

extern char *perm_print();
extern struct prot ne_prot;

/* XXX swap bytes on i386 */
#define htons(x) (((x) & 0xFF) << 8 | (((x) >> 8) & 0xFF))

/*
 * ne_stat()
 *	Do stat
 *
 * All nodes have same permission
 */
void
ne_stat(struct msg *m, struct file *f)
{
	char buf[MAXSTAT];
	uint len;
	struct attach *o;
	struct llist *l;
	uchar *addr;

	/*
	 * Verify access
	 */
	if (!(f->f_perm & ACC_READ)) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * Calculate length
	 */
	o = f->f_file;
	if (!o) {
		extern struct llist files;

		/*
		 * Root dir--# files in dir
		 */
		len = 0;
		for (l = LL_NEXT(&files); l != &files; l = LL_NEXT(l)) {
			len += 1;
		}
		sprintf(buf, "size=%d\ntype=d\nowner=0\ninode=0\n", len);
	} else {
		/*
		 * File--its byte length
		 */
		len = 0;
		for (l = LL_NEXT(&o->a_writers); l != &o->a_writers;
				l = LL_NEXT(l)) {
			uint y;
			struct msg *m2;

			m2 = l->l_data;
			for (y = 0; y < m2->m_nseg; ++y) {
				len += m2->m_seg[y].s_buflen;
			}
		}
		addr = adapters[o->a_unit].a_addr;
		sprintf(buf,
		 "size=%d\ntype=f\nowner=%d\ninode=%u\n"
		 "macaddr=%02x.%02x.%02x.%02x.%02x.%02x\n",
			len, o->a_owner, (uint)o,
			addr[0], addr[1], addr[2],
			addr[3], addr[4], addr[5]
			);
	}
	strcat(buf, perm_print(o ? (&o->a_prot) : &ne_prot));
	m->m_buf = buf;
	m->m_arg = m->m_buflen = strlen(buf);
	m->m_nseg = 1;
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

/*
 * ne_wstat()
 *	Allow writing of supported stat messages
 */
void
ne_wstat(struct msg *m, struct file *f)
{
	struct attach *o;
	char *field, *val;

	/*
	 * Can't fiddle the root dir
	 */
	if (f->f_file == 0) {
		msg_err(m->m_sender, EINVAL);
	}

	/*
	 * See if common handling code can do it
	 */
	if (do_wstat(m, &ne_prot, f->f_perm, &field, &val) == 0) {
		return;
	}

	o = f->f_file;

	/*
	 * Process each kind of field we can write
	 */
	if (!strcmp(field, "type")) {
		/*
		 * Set attachment type
		 */
		sscanf(val, "%x", (uint *)&o->a_type);
		o->a_type = htons(o->a_type);
		o->a_typeset = 1;
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
