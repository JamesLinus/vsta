/*
 * stat.c
 *	Do the stat function
 *
 * We also lump the chmod/chown stuff here as well
 */
#include <vstafs/vstafs.h>
#include <vstafs/buf.h>
#include <sys/perm.h>

extern char *perm_print();

/*
 * vfs_stat()
 *	Do stat
 */
void
vfs_stat(struct msg *m, struct file *f)
{
	char buf[MAXSTAT];
	struct fs_file *fs;
	struct buf *b;
	uint len;

	/*
	 * Verify access
	 */
	if (!(f->f_perm & ACC_READ)) {
		msg_err(m->m_sender, EPERM);
		return;
	}
	fs = getfs(f->f_file, &b);
	if (!fs) {
		msg_err(m->m_sender, ENOMEM);
		return;
	}

	/*
	 * Calculate length
	 */
	if (fs->fs_type == FT_DIR) {
		ulong idx;

		idx = sizeof(struct fs_file); 
		len = 0;
		lock_buf(b);
		while (idx < fs->fs_len) {
			uint ent_len;
			struct fs_dirent *d;

			/*
			 * End loop when can't get more
			 */
			if (!bmap(fs, idx, sizeof(struct fs_dirent),
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
	} else {
		len = fs->fs_len - sizeof(struct fs_file);
	}
	sprintf(buf, "size=%d\ntype=%c\nowner=%d\ninode=%ud\n",
		len, f->f_file ? 'f' : 'd', fs->fs_owner,
		fs->fs_blks[0].a_start);
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

	/*
	 * Get file's node
	 */
	fs = getfs(f->f_file, 0);

	/*
	 * See if common handling code can do it
	 */
	if (do_wstat(m, &fs->fs_prot, f->f_perm, &field, &val) == 0)
		return;

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

	/*
	 * Only *files* get an ID (and thus can be mapped shared)
	 */
	fs = getfs(f->f_file, 0);
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
}
