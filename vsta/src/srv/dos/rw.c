/*
 * rw.c
 *	Routines for operating on the data in a file
 */
#include <sys/fs.h>
#include <abc.h>
#include "dos.h"
#include <std.h>
#include <ctype.h>
#include <sys/assert.h>

/*
 * do_write()
 *	Local routine to loop over a buffer and write it to a file
 *
 * Returns 0 on success, 1 on error.
 */
static int
do_write(struct clust *c, uint pos, char *buf, uint cnt)
{
	uint bufoff, step, blk, boff;
	void *handle;

	/*
	 * Loop across each block, putting our data into place
	 */
	bufoff = 0;
	while (cnt > 0) {
		/*
		 * Calculate how much to take out of current block
		 */
		boff = pos & (BLOCKSIZE-1);
		step = BLOCKSIZE - boff;
		if (step >= cnt) {
			step = cnt;
		}

		/*
		 * Map current block
		 */
		blk = pos / BLOCKSIZE;
		handle = find_buf(BOFF(c->c_clust[blk]), CLSIZE,
			((boff == 0) && (step == BLOCKSIZE)) ? 0 : ABC_FILL);
		if (!handle) {
			return(1);
		}
		lock_buf(handle);

		/*
		 * Copy data, mark buffer dirty, free it
		 */
		bcopy(buf + bufoff, index_buf(handle, 0, CLSIZE) + boff,
			step);
		dirty_buf(handle, 0);
		unlock_buf(handle);

		/*
		 * Advance counters
		 */
		pos += step;
		bufoff += step;
		cnt -= step;
	}
	return(0);
}

/*
 * write_zero()
 *	Fill in zeroes when new write position is beyond old EOF
 */
static void
write_zero(struct node *n, ulong oldlen, ulong newlen)
{
	char zero[1024];
	ulong count = newlen - oldlen, step;

	/*
	 * Here's the zeroes we'll drop down
	 */
	bzero(zero, sizeof(zero));

	/*
	 * Write in zeroes one buffer full at a time until we've
	 * caught up with our new file end.
	 */
	while (count > 0) {
		if (count < sizeof(zero)) {
			step = count;
		} else {
			step = sizeof(zero);
		}
		(void)do_write(n->n_clust, oldlen, zero, step);
		oldlen += step;
		count -= step;
	}
}

/*
 * dos_write()
 *	Write to an open file
 */
void
dos_write(struct msg *m, struct file *f)
{
	struct node *n = f->f_node;
	ulong newlen, oldlen;

	/*
	 * Can only write to a true file, and only if open for writing.
	 */
	if ((n->n_type == T_DIR) || !(f->f_perm & ACC_WRITE)) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * See if the file's going to be able to hold all the data.  We
	 * do not necessarily need to allocate space if we're rewriting
	 * an existing file.
	 */
	newlen = f->f_pos + m->m_buflen;
	oldlen = n->n_len;
	if (newlen > oldlen) {
		/*
		 * Grow the size of the file to encompass the
		 * starting position for this write.  Make sure
		 * the initial contents is zero.  The DOS filesystem
		 * is *not* very efficient with sparse files....
		 */
		if (clust_setlen(n->n_clust, newlen)) {
			msg_err(m->m_sender, ENOSPC);
			return;
		}
		n->n_len = newlen;
		write_zero(n, oldlen, newlen);
	}
	n->n_flags |= N_DIRTY;

	/*
	 * Copy out the buffer
	 */
	if (do_write(n->n_clust, f->f_pos, m->m_buf, m->m_buflen)) {
		msg_err(m->m_sender, strerror());
		return;
	}
	m->m_arg = m->m_buflen;
	f->f_pos += m->m_buflen;
	m->m_buflen = m->m_arg1 = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}

/*
 * pack_name()
 *	Pack a DOS name into a UNIX-ish format
 */
void
pack_name(struct directory *d, char *file)
{
	char *p, *name = d->name, *ext = d->ext;

	for (p = name; (p < name+8) && (*p != ' '); ++p) {
		*file++ = tolower(*p);
	}
	p = ext;
	ASSERT_DEBUG(*p, "pack_name: null in extension");
	if (*p != ' ') {
		*file++ = '.';
		for ( ; (p < ext+3) && (*p != ' '); ++p) {
			*file++ = tolower(*p);
		}
	}
	*file = '\0';
}

/*
 * unicode_char_copy()
 *	Copy a single run of chars
 *
 * Returns 0 if all characters were copied, 1 if end of string was seen
 */
static int
unicode_char_copy(char *buf, char *name, int len)
{
	int x;

	for (x = 0; x < len; ++x) {
		if (name[1] != '\0') {
			*buf++ = '_';
		} else {
			if ((*buf++ = name[0]) == '\0') {
				return(1);
			}
		}
		name += 2;
	}
	return(0);
}

/*
 * unicode_copy()
 *	Copy the (unicode encoded) name in a VSE into a buffer
 *
 * The offset into "buf" is deduced from the dirVSE "id" value
 */
static void
unicode_copy(struct dirVSE *dv, char *buf)
{
	uchar id = dv->dv_id & VSE_ID_MASK;

	/*
	 * Index to appropriate location
	 */
	ASSERT_DEBUG(id < VSE_ID_MAX, "unicode_copy: bad ID");
	buf += (id - 1) * VSE_NAME_SIZE;

	/*
	 * Assemble parts of name
	 */
	if (unicode_char_copy(buf, dv->dv_name1, VSE_1SIZE) == 0) {
		if (unicode_char_copy(buf+VSE_1SIZE,
				dv->dv_name2, VSE_2SIZE) == 0) {
			(void)unicode_char_copy(buf+(VSE_1SIZE+VSE_2SIZE),
				dv->dv_name3, VSE_3SIZE);
		}
	}
}

/*
 * short_checksum()
 *	Calculate the checksum value for a short FAT filename
 */
uchar
short_checksum(char *f1, char *f2)
{
	int x;
	uchar sum = 0;

	for (x = 0; x < 8; ++x) {
		sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + f1[x];
	}
	for (x = 0; x < 3; ++x) {
		sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + f2[x];
	}
	return(sum);
}

/*
 * assemble_vfat_name()
 *	Extract the necessary number of VFAT sub-entries, convert to name
 *
 * Returns 1 if the name was extracted OK, 0 otherwise
 * Extracts the VSE's in any order, and expects a short name to follow.
 * Uses the checksum to verify that the VSE's match the actual dir
 * entry.
 * The initial dirVSE is supplied, since pretty much any caller has to
 * have seen it as a regular directory entry first, at which point they
 * call into here.
 */
int
assemble_vfat_name(char *name, struct directory *d, intfun nextd, void *statep)
{
	struct dirVSE *dv = (struct dirVSE *)d;
	uint endid = 0, mask = 0, last, id;
	uchar sum = dv->dv_sum;
	struct directory dirtmp;

	/*
	 * Assemble VSE's until we run into the short name.  Leave if
	 * we detect that our VSE's are not all present, or if we
	 * detect that they don't correspond to the short name which
	 * follows.
	 */
	for (;;) {
		/*
		 * Record ID of last VSE, mask to just offset part of ID
		 */
		id = dv->dv_id;
		last = id & VSE_ID_LAST;
		id &= VSE_ID_MASK;
		if (last) {
			endid = id;
		}

		/*
		 * Record bit mask of ID's seen
		 */
		mask |= (1 << (id - 1));

		/*
		 * Copy name into place.  Place a null termination
		 * after the last VSE.  If the VSE was not fully
		 * populated with characters, the VSE would have
		 * contained an embedded null, and this is redundant.
		 * But not harm done.
		 */
		unicode_copy(dv, name);
		if (last) {
			name[id * VSE_NAME_SIZE] = '\0';
		}

		/*
		 * Get next dir entry, leave when we stop assembling
		 * VSE's.  Stop using supplied buffer, since we're
		 * now going to modify it.
		 */
		d = &dirtmp;
		dv = (struct dirVSE *)d;
		if ((*nextd)(d, statep)) {
			return(1);
		}
		if (d->attr != DA_VFAT) {
			break;
		}

		/*
		 * If the checksum mismatched, then we've been
		 * assembling some old, moldy VSE's.  Leave.
		 */
		if (dv->dv_sum != sum) {
			return(1);
		}
	}

	/*
	 * Checksum of short entry must match
	 */
	if (short_checksum(d->name, d->ext) != sum) {
		return(1);
	}

	/*
	 * We must see all the VSE's present, too
	 */
	if (mask != ((1 << endid) - 1)) {
		return(1);
	}

	/*
	 * Advance past short entry, too, and return successful assembly
	 * of name.
	 */
	return(0);
}

/*
 * State for iterating across directory entries in a file
 */
struct dirstate {
	struct node *ds_node;	/* File node */
	ulong *ds_posp;		/*  ...(pointer to) position within */
};

/*
 * next_dir()
 *	Get next dir entry out of file
 */
static int
next_dir(struct directory *d, void *statep)
{
	struct dirstate *state = statep;

	if (dir_copy(state->ds_node, *state->ds_posp, d)) {
		return(1);
	}
	*state->ds_posp += 1;
	return(0);
}

/*
 * dos_readdir()
 *	Do reads on directory entries
 */
static void
dos_readdir(struct msg *m, struct file *f)
{
	char *buf;
	uint len, x;
	struct directory d;
	struct node *n = f->f_node;
	char file[VSE_MAX_NAME+2];

	/*
	 * Get a buffer of the requested size, but put a sanity
	 * cap on it.
	 */
	len = m->m_arg;
	if (len > 4096) {
		len = 4096;
	}
	if ((buf = malloc(len+1)) == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}
	buf[0] = '\0';

	/*
	 * Skip "." and "..", which exist only in non-root directories
	 */
	if ((n != rootdir) && (f->f_pos == 0)) {
		f->f_pos = 2;
	}

	/*
	 * Assemble as many names as will fit
	 */
	for (x = 0; x < len; ) {
		uint c;
		ulong opos;

		/*
		 * Look at the slot at f_pos.  For reads of directories
		 * f_pos is simply the struct directory index.  Leave
		 * loop on failure, presumably from EOF.
		 */
		opos = f->f_pos;
		if (dir_copy(n, f->f_pos++, &d)) {
			break;
		}

		/*
		 * Leave after last entry, skip deleted entries
		 */
		c = (d.name[0] & 0xFF);
		if (!c) {
			break;
		}
		if (c == DN_DEL) {
			continue;
		}

		/*
		 * For VFAT, assemble the long name and skip the
		 * short one.
		 */
		if (d.attr == DA_VFAT) {
			struct dirstate state;

			state.ds_node = n;
			state.ds_posp = &f->f_pos;
			if (assemble_vfat_name(file, &d, next_dir, &state)) {
				/*
				 * On failure to assemble, we need to make
				 * sure we don't skip elements which wouldn't
				 * assemble with our original starting point,
				 * but are valid after skipping that (invalid)
				 * starting point.  Stepping one back from
				 * the end might be as safe, and a bit more
				 * efficient for this edge case?
				 */
				f->f_pos = opos+1;
				continue;
			}
		} else {
			/*
			 * Otherwise just convert the short name
			 */
			pack_name(&d, file);
		}

		/*
		 * If the next entry won't fit, back up the file
		 * position and return what we have.
		 */
		if ((x + strlen(file) + 1) >= len) {
			f->f_pos = opos;
			break;
		}

		/*
		 * Add entry and a newline
		 */
		strcat(buf+x, file);
		strcat(buf+x, "\n");
		x += (strlen(file)+1);
	}

	/*
	 * Send back results
	 */
	m->m_buf = buf;
	m->m_arg = m->m_buflen = x;
	m->m_nseg = ((x > 0) ? 1 : 0);
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
	free(buf);
}

/*
 * dos_read()
 *	Read bytes out of the current file or directory
 *
 * Directories get their own routine.
 */
void
dos_read(struct msg *m, struct file *f)
{
	int x, step, cnt, blk, boff;
	struct node *n = f->f_node;
	void *handle;
	char *buf;
	struct clust *c = n->n_clust;

	/*
	 * Directory
	 */
	if (n->n_type == T_DIR) {
		dos_readdir(m, f);
		return;
	}

	/*
	 * EOF?
	 */
	if (f->f_pos >= n->n_len) {
		m->m_arg = m->m_arg1 = m->m_buflen = m->m_nseg = 0;
		msg_reply(m->m_sender, m);
		return;
	}
	ASSERT_DEBUG(c->c_clust, "dos_read: len !clust");
	ASSERT_DEBUG(c->c_nclust > 0, "dos_read: clust !nclust");

	/*
	 * Calculate # bytes to get
	 */
	cnt = m->m_arg;
	if (cnt > (n->n_len - f->f_pos)) {
		cnt = n->n_len - f->f_pos;
	}

	/*
	 * Get a buffer big enough to do the job
	 * XXX user scatter-gather, this is a waste
	 */
	buf = malloc(cnt);
	if (buf == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}

	/*
	 * Loop across each block, putting our data into place
	 */
	for (x = 0; x < cnt; ) {
		/*
		 * Calculate how much to take out of current block
		 */
		boff = f->f_pos & (BLOCKSIZE-1);
		step = BLOCKSIZE - boff;
		if (step >= (cnt-x)) {
			step = (cnt-x);
		}

		/*
		 * Map current block
		 */
		blk = f->f_pos / BLOCKSIZE;
		ASSERT_DEBUG(blk < c->c_nclust, "dos_read: bad blk");
		handle = find_buf(BOFF(c->c_clust[blk]), CLSIZE, ABC_FILL);
		if (!handle) {
			free(buf);
			msg_err(m->m_sender, strerror());
			return;
		}
		lock_buf(handle);
		if ((blk + 1) < c->c_nclust) {
			(void)find_buf(BOFF(c->c_clust[blk + 1]),
				CLSIZE, ABC_FILL | ABC_BG);
		}
		bcopy(index_buf(handle, 0, CLSIZE) + boff, buf + x, step);
		f->f_pos += step;
		unlock_buf(handle);

		/*
		 * Advance to next chunk
		 */
		x += step;
	}

	/*
	 * Send back reply
	 */
	m->m_buf = buf;
	m->m_arg = m->m_buflen = cnt;
	m->m_nseg = (cnt ? 1 : 0);
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
	free(buf);
}
