/*
 * dir.c
 *	Do readdir() operation
 */
#include <sys/fs.h>
#include <sys/assert.h>
#include <stdio.h>
#include <std.h>
#include <llist.h>
#include "ne.h"

#define MAXDIRIO (1024)		/* Max # bytes in one dir read */

/*
 * ne_readdir()
 *	Fill in buffer with list of opened files
 */
void
ne_readdir(struct msg *m, struct file *f)
{
	char *buf;
	uint len, pos, bufcnt;
	struct llist *l;

	/*
	 * Get a buffer of the requested size, but put a sanity
	 * cap on it.
	 */
	len = m->m_arg;
	if (len > 256) {
		len = 256;
	}
	if ((buf = malloc(len+1)) == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}
	buf[0] = '\0';

	/*
	 * Assemble as many names as will fit, starting at
	 * given byte offset.  We assume the caller's position
	 * always advances in units of a whole directory entry.
	 */
	bufcnt = pos = 0;
	for (l = LL_NEXT(&files); l != &files; l = LL_NEXT(l)) {
		uint slen;
		char buf2[32];
		struct attach *a;

		/*
		 * Point to next file.  Get its length.
		 */
		a = l->l_data;
		sprintf(buf2, "%x", a->a_type);
		slen = strlen(buf2)+1;

		/*
		 * If we've reached an offset the caller hasn't seen
		 * yet, assemble the entry into the buffer.
		 */
		if (pos >= f->f_pos) {
			/*
			 * No more room in buffer--return results
			 */
			if (slen >= len) {
				break;
			}

			/*
			 * Put string with newline at end of buffer
			 */
			sprintf(buf + bufcnt, "%s\n", buf2);

			/*
			 * Update counters
			 */
			len -= slen;
			bufcnt += slen;
		}

		/*
		 * Update position
		 */
		pos += slen;
	}

	/*
	 * Send back results
	 */
	m->m_buf = buf;
	m->m_arg = m->m_buflen = bufcnt;
	m->m_nseg = ((bufcnt > 0) ? 1 : 0);
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
	free(buf);
	f->f_pos = pos;
}

/*
 * dir_lookup()
 *	Look up name in list of all files in this FS
 */
struct attach *
dir_lookup(char *name)
{
	struct llist *l;
	uint x;

	if (sscanf(name, "%x", &x) != 1) {
		return(0);
	}
	for (l = LL_NEXT(&files); l != &files; l = LL_NEXT(l)) {
		struct attach *a;

		a = l->l_data;
		if (a->a_type == x) {
			return(a);
		}
	}
	return(0);
}

/*
 * freeup()
 *	Free all memory associated with a file
 */
static void
freeup(struct attach *o)
{
	if (o == 0) {
		return;
	}
	if (o->a_entry) {
		ll_delete(o->a_entry);
	}
	free(o);
}

/*
 * dir_newfile()
 *	Create new entry in filesystem
 */
struct attach *
dir_newfile(struct file *f)
{
	struct attach *o;
	struct prot *p;

	/*
	 * Get new node
	 */
	o = malloc(sizeof(struct attach));
	if (o == 0) {
		return(0);
	}
	bzero(o, sizeof(struct attach));

	/*
	 * Insert in dir chain
	 */
	o->a_entry = ll_insert(&files, o);
	if (o->a_entry == 0) {
		freeup(o);
		return(0);
	}

	/*
	 * Use 0'th perm as our prot, require full match
	 */
	p = &o->a_prot;
	bzero(p, sizeof(*p));
	p->prot_len = PERM_LEN(&f->f_perms[0]);
	bcopy(f->f_perms[0].perm_id, p->prot_id, PERMLEN);
	p->prot_bits[p->prot_len-1] =
		ACC_READ|ACC_WRITE|ACC_CHMOD;
	o->a_owner = f->f_perms[0].perm_uid;
	return(o);
}

/*
 * ne_open()
 *	Main entry for processing an open message
 */
void
ne_open(struct msg *m, struct file *f)
{
	struct attach *o;
	uint x;
	char *p = m->m_buf;

	/*
	 * Have to be in root dir to open down into a file
	 */
	if (f->f_file) {
		msg_err(m->m_sender, ENOTDIR);
		return;
	}

	/*
	 * No subdirs in a ethernet filesystem, filenames are
	 * packet type values in hex.
	 */
	if ((m->m_arg & ACC_DIR) ||
			(sscanf(p, "%x", &x) != 1) ||
			(x > 0xffff)) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * Look up name
	 */
	o = dir_lookup(p);

	/*
	 * If it's a new file, allocate the entry now.  Note we don't
	 * require ACC_CREATE to do this; it makes it easier to just
	 * "od -b /net/ether/1234" and watch packets.
	 */
	if (!o) {
		/*
		 * Failure?
		 */
		if ((o = dir_newfile(f)) == 0) {
			msg_err(m->m_sender, ENOMEM);
			return;
		}

		/*
		 * Move to new node.  Give it the desired name
		 */
		o->a_type = x;
		f->f_file = o; o->a_refs += 1;
		ASSERT_DEBUG(o->a_refs > 0, "ne_open: overflow");
		f->f_perm = ACC_READ|ACC_WRITE|ACC_CHMOD;
		m->m_nseg = m->m_arg = m->m_arg1 = 0;
		msg_reply(m->m_sender, m);
		return;
	}

	/*
	 * Check permission
	 */
	x = perm_calc(f->f_perms, f->f_nperm, &o->a_prot);
	if ((m->m_arg & x) != m->m_arg) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * Move to this file
	 */
	f->f_file = o; o->a_refs += 1;
	ASSERT_DEBUG(o->a_refs > 0, "ne_open: overflow");
	f->f_perm = m->m_arg | (x & ACC_CHMOD);
	m->m_nseg = m->m_arg = m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

/*
 * ne_close()
 *	Do closing actions on a file
 */
void
ne_close(struct file *f)
{
	struct attach *o;

	/*
	 * No ref count on dir
	 */
	o = f->f_file;
	if (o == 0) {
		return;
	}

	/*
	 * Free a ref.  No more clients--free node.
	 */
	ASSERT_DEBUG(o->a_refs > 0, "ne_close: underflow");
	o->a_refs -= 1;
	if (o->a_refs == 0) {
		freeup(o);
		return;
	}
}
