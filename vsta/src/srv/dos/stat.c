/*
 * stat.c
 *	Implement stat operations on an open file
 *
 * The date/time handling functions are derived from software
 * distributed in the Mach and BSD 4.4 software releases.  They
 * were originally written by Bill Jolitz.
 */
#include <sys/fs.h>
#include "dos.h"
#include <sys/param.h>
#include <syslog.h>
#include <stdio.h>
#include <string.h>
#include <std.h>

extern struct prot dos_prot;

/*
 * ytos()
 *	convert years to seconds (from 1990)
 */
static ulong
ytos(uint y)
{
	uint i;
	ulong ret;

	if (y < 1990) {
		syslog(LOG_WARNING, "year %d is less than 1990!\n", y);
	}
	ret = 0;
	for (i = 1990; i < y; i++) {
		if (i % 4) {
			ret += 365*24*60*60;
		} else {
			ret += 366*24*60*60;
		}
	}
	return(ret);
}

/*
 * mtos()
 *	convert months to seconds
 */
static ulong
mtos(uint m, int leap)
{
	uint i;
	ulong ret;

	ret = 0;
	for (i = 1; i < m; i++) {
		switch(i){
		case 1: case 3: case 5: case 7: case 8: case 10: case 12:
			ret += 31*24*60*60; break;
		case 4: case 6: case 9: case 11:
			ret += 30*24*60*60; break;
		case 2:
			if (leap) ret += 29*24*60*60;
			else ret += 28*24*60*60;
		}
	}
	return ret;
}


/*
 * cvt_time()
 *	Calculate time in seconds given DOS-encoded date/time
 *
 * Returns seconds since 1990, or 0L on failure.
 */
ulong
cvt_time(uint date, uint time)
{
	ulong sec;
	uint leap, t, yd;

	sec = (date >> 9) + 1980;
	leap = !(sec % 4); sec = ytos(sec);			/* year */
	yd = mtos((date >> 5) & 0xF,leap); sec += yd;		/* month */
	t = ((date & 0x1F)-1) * 24*60*60; sec += t;
		yd += t;					/* date */
	sec += (time >> 11) * 60*60;				/* hour */
	sec += ((time >> 5) & 0x3F) * 60;			/* minutes */
	sec += ((time & 0x1F) << 1);				/* seconds */

	return(sec);
}

/*
 * inum()
 *	Synthesize an "inode" number for the node
 */
uint
inum(struct node *n)
{
	extern struct node *rootdir;

	/*
	 * Root dir--no cluster, just give a value of 0
	 */
	if (n == rootdir) {
		return(0);
	}

	/*
	 * Dir--use value of first cluster
	 */
	if (n->n_type == T_DIR) {
		return(n->n_clust->c_clust[0]);
	}

	/*
	 * File in root dir--again, no cluster for root dir.  Just
	 * use cluster value 1 (they start at 2, so this is available),
	 * and or in our slot.
	 */
	if (n->n_dir == rootdir) {
		return ((1 << 16) | n->n_slot);
	} else {
		/*
		 * Others--high 16 bits is cluster of file's directory,
		 * low 16 bits is our slot number.
		 */
		return ((n->n_dir->n_clust->c_clust[0] << 16) | n->n_slot);
	}
}

/*
 * isize()
 *	Calculate size of file from its "inode" information
 */
static uint
isize(struct node *n)
{
	extern uint dirents;

	if (n == rootdir) {
		return(sizeof(struct directory)*dirents);
	}
	return(n->n_clust->c_nclust*clsize);
}

/*
 * dos_stat()
 *	Build stat string for file, send back
 */
void
dos_stat(struct msg *m, struct file *f)
{
	char result[MAXSTAT];
	struct node *n = f->f_node;
	struct directory d;

	/*
	 * Directories
	 */
	if (n != rootdir) {
		dir_copy(n->n_dir, n->n_slot, &d);
	} else {
		bzero(&d, sizeof(d));
	}
	if (n->n_type == T_DIR) {
		sprintf(result,
 "perm=1/1\nacc=5/0/2\nsize=%d\ntype=d\nowner=0\ninode=%d\nmtime=%ld\n",
			isize(n), inum(n),
			cvt_time(d.date, d.time));
	} else {
		/*
		 * Otherwise look up file and get dope
		 */
		sprintf(result,
 "perm=1/1\nacc=5/0/2\nsize=%d\ntype=f\nowner=0\ninode=%d\nmtime=%ld\n",
			n->n_len, inum(n),
			cvt_time(d.date, d.time));
	}
	m->m_buf = result;
	m->m_buflen = strlen(result);
	m->m_nseg = 1;
	m->m_arg = m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

/*
 * dos_fid()
 *	Return ID for file
 */
void
dos_fid(struct msg *m, struct file *f)
{
	struct node *n = f->f_node;

	/*
	 * Only *files* get an ID (and thus can be mapped shared)
	 */
	if (n->n_type == T_DIR) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * arg is the inode value; arg1 is the size in pages
	 */
	m->m_arg = inum(n);
	m->m_arg1 = btorp(isize(n));
	m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}

/*
 * dos_wstat()
 *	Write status of DOS file
 *
 * We support setting of modification time only, plus the usual
 * shared code to set access to the filesystem.
 */
void
dos_wstat(struct msg *m, struct file *f)
{
	char *field, *val;

	/*
	 * Common wstat handling code
	 */
	if (do_wstat(m, &dos_prot, f->f_perm, &field, &val) == 0) {
		return;
	}
	if (!strcmp(field, "atime") || !strcmp(field, "mtime")) {
		time_t t;

		/*
		 * Convert to number, write to dir entry
		 */
		t = atoi(val);
		dir_timestamp(f, t);
	} else if (!strcmp(field, "type")) {
		if (dir_set_type(f, val)) {
			msg_err(m->m_sender, EINVAL);
			return;
		}
	} else {
		/*
		 * Unsupported operation
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
