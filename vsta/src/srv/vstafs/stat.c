/*
 * stat.c
 *	Do the stat function
 *
 * We also lump the chmod/chown stuff here as well
 */
#include "vstafs.h"
#include <sys/perm.h>
#include <stdio.h>
#include <std.h>

extern char *perm_print(struct prot *);

/*
 * getrev()
 *	Given cluster addr for a fs_file, return its revision number
 */
static ulong
getrev(daddr_t d)
{
	struct openfile *o;
	ulong rev;
	struct fs_file *fs;

	o = get_node(d);
	fs = getfs(o, 0);
	rev = fs->fs_rev;
	deref_node(o);
	return(rev);
}

/*
 * vfs_stat()
 *	Do stat
 */
void
vfs_stat(struct msg *m, struct file *f)
{
	char *revs, typec, buf[MAXSTAT], buf2[32];
	struct fs_file *fs;
	struct buf *b;
	uint len;

	/*
	 * Verify access
	 */
	fs = getfs(f->f_file, &b);
	if (!(f->f_perm & (ACC_READ | ACC_CHMOD)) &&
			(f->f_perms[0].perm_uid != fs->fs_owner)) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * Calculate length
	 */
	if (fs->fs_type == FT_DIR) {
		ulong idx;

		typec = 'd';
		idx = sizeof(struct fs_file); 
		len = 0;
		lock_buf(b);
		while (idx < fs->fs_len) {
			uint ent_len;
			struct fs_dirent *d;

			/*
			 * End loop when can't get more
			 */
			if (!bmap(b, fs, idx, sizeof(struct fs_dirent),
					(char **)&d, &ent_len)) {
				break;
			}
			if (ent_len < sizeof(struct fs_dirent)) {
				break;
			}
			if ((d->fs_clstart != 0) && !(d->fs_name[0] & 0x80)) {
				len += 1;
			}
			idx += sizeof(struct fs_dirent);
		}
		unlock_buf(b);
		revs = "";
	} else {
		typec = 'f';
		len = fs->fs_len - sizeof(struct fs_file);
		sprintf(buf2, "rev=%U\nprev=%U\n",
			fs->fs_rev,
			fs->fs_prev ? getrev(fs->fs_prev) : 0);
		revs = buf2;
	}
	sprintf(buf, "size=%u\ntype=%c\nowner=%d\ninode=%u\n"
		"ctime=%u\nmtime=%u\n%s",
		len, typec, fs->fs_owner, fs->fs_blks[0].a_start,
		fs->fs_ctime, fs->fs_mtime, revs);
	strcat(buf, perm_print(&fs->fs_prot));
	m->m_buf = buf;
	m->m_arg = m->m_buflen = strlen(buf);
	m->m_nseg = 1;
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

/*
 * vfs_wstat()
 *	Allow writing of supported stat messages
 */
void
vfs_wstat(struct msg *m, struct file *f)
{
	char *field, *val;
	struct fs_file *fs;
	struct buf *b;

	/*
	 * Can't modify files unless have write permission
	 */
	fs = getfs(f->f_file, &b);
	if (!(f->f_perm & (ACC_CHMOD)) &&
			(f->f_perms[0].perm_uid != fs->fs_owner)) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * See if common handling code can do it
	 */
	if (do_wstat(m, &fs->fs_prot, f->f_perm, &field, &val) == 0) {
		dirty_buf(b, 0);
		return;
	}

	/*
	 * Permit the mtime to be set
	 */
	if (!strcmp(field, "mtime")) {
		/*
		 * Convert to number, write to file attribute
		 */
		fs->fs_mtime = atoi(val);
		dirty_buf(b, 0);
		m->m_nseg = m->m_arg = m->m_arg1 = 0;
		msg_reply(m->m_sender, m);
		return;
	}

	/*
	 * Not a field we support...
	 */
	msg_err(m->m_sender, EINVAL);
}

/*
 * vfs_fid()
 *	Return ID for file
 */
void
vfs_fid(struct msg *m, struct file *f)
{
	struct fs_file *fs;
	struct openfile *o;

	/*
	 * Only *files* get an ID (and thus can be mapped shared)
	 */
	o = f->f_file;
	fs = getfs(o, 0);
	if (fs->fs_type == FT_DIR) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * arg is the inode value; arg1 is the size in pages
	 */
	m->m_arg = fs->fs_blks[0].a_start;
	m->m_arg1 = btop(fs->fs_len);
	m->m_nseg = 0;
	msg_reply(m->m_sender, m);

	/*
	 * Flag that this file may be hashed
	 */
	o->o_flags |= O_HASHED;
}
