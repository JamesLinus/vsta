/*
 * dir.c
 *	Do readdir() operation
 */
#include <stdio.h>
#include <sys/fs.h>
#include <std.h>
#include "wd.h"

#define MAXDIRIO (1024)		/* Max # bytes in one dir read */

/*
 * add_ent()
 *	Add another entry to the buffer if there's room
 *
 * Returns 1 if it wouldn't fit, 0 if it fits and was added.
 */
static int
add_ent(char *buf, char *p, uint len)
{
	uint left, x;

	left = len - strlen(buf);
	x = strlen(p);
	if (left < (x+2)) {
		return(1);
	}
	strcat(buf, p);
	strcat(buf, "\n");
	return(0);
}

/*
 * wd_readdir()
 *	Fill in buffer with list of supported names
 */
void
wd_readdir(struct msg *m, struct file *f)
{
	uint x, y, entries, len, nent = 0;
	char *buf;
	struct disk *d;

	/*
	 * Get a buffer
	 */
	len = m->m_arg;
	if (len > MAXDIRIO) {
		len = MAXDIRIO;
	}
	buf = malloc(len);
	if (buf == 0) {
		msg_err(m->m_sender, ENOMEM);
		return;
	}
	buf[0] = '\0';

	/* 
	 * Skip entries until we arrive at the current position
	 */
	entries = 0;
	for (x = 0; x < NWD; ++x) {
		d = &disks[x];
		
		/*
		 * Skip disks not present
		 */
		if (!d->d_configed) {
			continue;
		}

		/*
		 * Scan partition table
		 */
		for (y = 0; y < MAX_PARTS; ++y) {
			if ((d->d_parts[y] == NULL)
					|| (d->d_parts[y]->p_val == 0)) {
				continue;
			}
			if (entries >= f->f_pos) {
				nent++;
				if (add_ent(buf, d->d_parts[y]->p_name, len)) {
					goto done;
				}
			}
			entries++;
		}
	}

done:
	if (strlen(buf) == 0) {
		/*
		 * If nothing was put in our buffer, return a EOF
		 */
		m->m_nseg = m->m_arg = m->m_arg1 = 0;
		msg_reply(m->m_sender, m);
	} else {
		/*
		 * Send results
		 */
		m->m_buf = buf;
		m->m_arg = m->m_buflen = strlen(buf);
		m->m_nseg = 1;
		m->m_arg1 = 0;
		msg_reply(m->m_sender, m);
	}
	free(buf);
	f->f_pos += nent;
}

/*
 * wd_open()
 *	Move from root dir down into a device
 */
void
wd_open(struct msg *m, struct file *f)
{
	uint unit, x;
	char *p = m->m_buf;

	/*
	 * Can only move from root to a node
	 */
	if (f->f_node != ROOTDIR) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * All nodes start with our prefix, "wd"
	 */
	if ((strlen(p) < 3) || strncmp(p, "wd", 2)) {
		msg_err(m->m_sender, ESRCH);
		return;
	}

	/*
	 * Next digit is always the unit number
	 */
	unit = p[2] - '0';
	if ((unit > NWD) || (!disks[unit].d_configed)) {
		msg_err(m->m_sender, ESRCH);
		return;
	}

	/*
	 * Otherwise scan names for a match
	 */
	for (x = 0; x < MAX_PARTS; ++x) {
		struct part *part;

		part = disks[unit].d_parts[x];
		if ((part == NULL) || (part->p_val == 0)) {
			continue;
		}
		if (!strcmp(part->p_name, p)) {
			/*
			 * Honor the intersection of what they're
			 * requesting and what they're allowed
			 * to have.
			 */
			f->f_flags &= m->m_arg;

			/*
			 * Here's their file
			 */
			f->f_node = MKNODE(unit, x);
			m->m_nseg = m->m_arg = m->m_arg1 = 0;
			msg_reply(m->m_sender, m);

			return;
		}
	}
	msg_err(m->m_sender, ESRCH);
}
