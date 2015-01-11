/*
 * dir.c
 *	Do readdir() operation
 */
#include <alloc.h>
#include <stdio.h>
#include <string.h>
#include <sys/fs.h>
#include "fd.h"


#define MAXDIRIO (1024)		/* Max number of bytes in one dir read */


/*
 * add_ent()
 *	Add another entry to the dir buffer if there's room
 *
 * Return 1 if the entry doesn't fit, 0 if it does
 */
static int
add_ent(char *buf, int drv, int sz, uint len)
{
	uint left, x;
	char ent[16];

	if (sz) {
		sprintf(ent, "fd%d_%d", drv, sz / 1024);
	} else {
		sprintf(ent, "fd%d", drv);
	}
	left = len - strlen(buf);
	x = strlen(ent);
	if (left < (x + 2)) {
		return(1);
	}
	strcat(buf, ent);
	strcat(buf, "\n");
	return(0);
}


/*
 * fd_readdir()
 *	Fill in buffer with list of supported names
 */
void
fd_readdir(struct msg *m, struct file *f)
{
	uint len, nent = 0, x, y, entries;
	char *buf;
	struct floppy *flp;

	/*
	 * Get a buffer
	 */
	len = m->m_arg;
	if (len > MAXDIRIO) {
		len = MAXDIRIO;
	}

	buf = (char *)malloc(len);
	if (buf == NULL) {
		msg_err(m->m_sender, ENOMEM);
		return;
	}
	buf[0] = '\0';

	/*
	 * Skip entries until we reach our current position
	 */
	entries = 0;
	for (x = 0; x < NFD; x++) {
		flp = &floppies[x];
		
		/*
		 * Skip drives that aren't present
		 */
		if (flp->f_state == F_NXIO) {
			continue;
		}

		/*
		 * Always have one entry!
		 */
		if (entries >= f->f_pos) {
			nent++;
			if (add_ent(buf, x, 0, len)) {
				goto done;
			}
		}
		entries++;

		/*
		 * Scan the densities table
		 */
		for (y = 0; flp->f_posdens[y] != -1; y++) {
			if (entries >= f->f_pos) {
				int dens = flp->f_posdens[y];
				nent++;
				if (add_ent(buf, x,
					    fdparms[dens].f_size, len)) {
					goto done;
				}
			}
			entries++;
		}
	}

done:
	/*
	 * Send results
	 */
	if (strlen(buf) == 0) {
		/*
		 * If we have nothing in our buffer return EOF
		 */
		m->m_nseg = m->m_arg = 0;
	} else {
		m->m_buf = buf;
		m->m_arg = m->m_buflen = strlen(buf);
		m->m_nseg = 1;
	}
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
	free(buf);
	f->f_pos += nent;
}


/*
 * fd_open()
 *	Move from root dir down into a device
 */
void
fd_open(struct msg *m, struct file *f)
{
	int drv, i;
	char *p = m->m_buf;
	char nm[16];
	struct floppy *fl;


	if (f->f_slot != ROOTDIR) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * All nodes start with our prefix, "fd"
	 */
	if ((strlen(p) < 3) || strncmp(p, "fd", 2)) {
		msg_err(m->m_sender, ESRCH);
		return;
	}

	/*
	 * Our next digit is the unit number
	 */
	drv = p[2] - '0';
	if ((drv > NFD) || (floppies[drv].f_state == F_NXIO)) {
		msg_err(m->m_sender, ESRCH);
		return;
	}

	fl = &floppies[drv];

	if (!p[3]) {
		/*
		 * We have the special node - return the details
		 */
		f->f_slot = SPECIALNODE;
		f->f_unit = drv;
		fl->f_density = fl->f_specialdens;
		fl->f_opencnt++;

		/*
		 * Pick up user parameters or invalidate the parameter info
		 * to signal that we want to probe the details
		 */
		if (fl->f_specialdens == DISK_USERDEF) {
			fl->f_parms = fl->f_userp;
		} else if (fl->f_lastuseddens == DISK_AUTOPROBE) {
			fl->f_parms.f_size = FD_PUNDEF;
		}
		m->m_arg = m->m_arg1 = m->m_nseg = 0;
		msg_reply(m->m_sender, m);
		return;
	}
	
	for(i = 0; fl->f_posdens[i] != -1; i++) {
		int dens = fl->f_posdens[i];

		sprintf(nm, "_%d", fdparms[dens].f_size / 1024);
		if (!strcmp(&p[3], nm)) {
			/*
			 * We've found our node - return details
			 */
			f->f_slot = dens;
			f->f_unit = drv;
			fl->f_state = F_OFF;
			fl->f_density = dens;
			fl->f_parms = fdparms[dens];
			fl->f_opencnt++;
			m->m_arg = m->m_arg1 = m->m_nseg = 0;
			msg_reply(m->m_sender, m);
			return;
		}
	}

	msg_err(m->m_sender, ESRCH);
}
