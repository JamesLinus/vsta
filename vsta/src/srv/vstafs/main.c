/*
 * main.c
 *	Main loop for message processing
 */
#include "vstafs.h"
#include <sys/fs.h>
#include <sys/perm.h>
#include <sys/namer.h>
#include <hash.h>
#include <fcntl.h>
#include <std.h>
#include <stdio.h>
#include <mnttab.h>
#include <sys/assert.h>
#include <syslog.h>
#include <fdl.h>
#include <getopt.h>
#include "alloc.h"

extern int valid_fname(char *, int);

int blkdev;		/* Device this FS is mounted upon */
port_t rootport;	/* Port we receive contacts through */
static struct hash	/* Handle->filehandle mapping */
	*filehash;
static struct fs
	*fsroot;	/* Snapshot of root sector */
static int fflag,	/* Pull in fsck-pending changes */
	pflag;		/* Use path_open syntax */

/*
 * This "open" file just sits around as an easy way to talk about
 * the root filesystem.
 */
static struct openfile *rootdir;

/*
 * vfs_seek()
 *	Set file position
 */
static void
vfs_seek(struct msg *m, struct file *f)
{
	if (m->m_arg < 0) {
		msg_err(m->m_sender, EINVAL);
		return;
	}
	f->f_pos = m->m_arg+OFF_DATA;
	m->m_buflen = m->m_arg = m->m_arg1 = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}

/*
 * new_client()
 *	Create new per-connect structure
 */
static void
new_client(struct msg *m)
{
	struct file *f;
	struct perm *perms;
	int nperms;
	struct fs_file *fs;

	/*
	 * Access dope on root dir
	 */
	fs = getfs(rootdir, 0);
	if (!fs) {
		msg_err(m->m_sender, ENOMEM);
		return;
	}

	/*
	 * Get data structure
	 */
	if ((f = malloc(sizeof(struct file))) == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}

	/*
	 * Fill in fields.
	 */
	perms = (struct perm *)m->m_buf;
	nperms = (m->m_buflen)/sizeof(struct perm);
	f->f_file = rootdir;
	f->f_pos = OFF_DATA;
	bcopy(perms, f->f_perms, nperms * sizeof(struct perm));
	f->f_nperm = nperms;
	f->f_rename_id = 0;

	/*
	 * Calculate perms on root dir
	 */
	f->f_perm = perm_calc(f->f_perms, f->f_nperm, &fs->fs_prot);

	/*
	 * Hash under the sender's handle
	 */
        if (hash_insert(filehash, m->m_sender, f)) {
		free(f);
		msg_err(m->m_sender, ENOMEM);
		return;
	}

	/*
	 * It's a win, so ref the directory node
	 */
	ref_node(rootdir);

	/*
	 * Return acceptance
	 */
	msg_accept(m->m_sender);
}

/*
 * dup_client()
 *	Duplicate current file access onto new session
 */
static void
dup_client(struct msg *m, struct file *fold)
{
	struct file *f;

	/*
	 * Get data structure
	 */
	if ((f = malloc(sizeof(struct file))) == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}

	/*
	 * Fill in fields.  Note that our buffer is the
	 * information on the permissions our client
	 * possesses.  For an M_CONNECT, the message is
	 * from the kernel, and trusted.
	 */
	bcopy(fold, f, sizeof(struct file));

	/*
	 * Hash under the sender's handle
	 */
        if (hash_insert(filehash, m->m_arg, f)) {
		free(f);
		msg_err(m->m_sender, ENOMEM);
		return;
	}

	/*
	 * Return acceptance
	 */
	ref_node(f->f_file);
	m->m_arg = m->m_arg1 = m->m_buflen = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}

/*
 * dead_client()
 *	Someone has gone away.  Free their info.
 */
static void
dead_client(struct msg *m, struct file *f)
{
	extern void vfs_close();

	ASSERT(hash_delete(filehash, m->m_sender) == 0,
		"dead_client: mismatch");
	if (f->f_rename_id) {
		cancel_rename(f);
	}
	vfs_close(f);
	free(f);
}

/*
 * vfs_main()
 *	Endless loop to receive and serve requests
 */
static void
vfs_main()
{
	struct msg msg;
	int x;
	struct file *f;

loop:
	/*
	 * Receive a message, log an error and then keep going
	 */
	x = msg_receive(rootport, &msg);
	if (x < 0) {
		syslog(LOG_ERR, "msg_receive");
		goto loop;
	}

	/*
	 * Categorize by basic message operation
	 */
	f = hash_lookup(filehash, msg.m_sender);
	switch (msg.m_op & MSG_MASK) {
	case M_CONNECT:		/* New client */
		new_client(&msg);
		break;
	case M_DISCONNECT:	/* Client done */
		dead_client(&msg, f);
		break;
	case M_DUP:		/* File handle dup during exec() */
		dup_client(&msg, f);
		break;
	case M_ABORT:		/* Aborted operation */
		/*
		 * We're synchronous, so presumably the operation
		 * is all done and this abort is old news.
		 */
		msg_reply(msg.m_sender, &msg);
		break;
	case FS_OPEN:		/* Look up file from directory */
		if (!valid_fname(msg.m_buf, msg.m_buflen)) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		vfs_open(&msg, f);
		break;

	case FS_ABSREAD:	/* Set position, then read */
		if (msg.m_arg1 < 0) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		f->f_pos = msg.m_arg1+OFF_DATA;
		/* VVV fall into VVV */
	case FS_READ:		/* Read file */
		vfs_read(&msg, f);
		break;

	case FS_ABSWRITE:	/* Set position, then write */
		if (msg.m_arg1 < 0) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		f->f_pos = msg.m_arg1+OFF_DATA;
		/* VVV fall into VVV */
	case FS_WRITE:		/* Write file */
		vfs_write(&msg, f);
		break;

	case FS_SEEK:		/* Set new file position */
		vfs_seek(&msg, f);
		break;
	case FS_REMOVE:		/* Get rid of a file */
		vfs_remove(&msg, f);
		break;
	case FS_STAT:		/* Tell about file */
		vfs_stat(&msg, f);
		break;
	case FS_WSTAT:		/* Modify file */
		vfs_wstat(&msg, f);
		break;
	case FS_FID:		/* File ID */
		vfs_fid(&msg, f);
		break;
	case FS_RENAME:		/* Rename file */
		vfs_rename(&msg, f);
		break;
	default:		/* Unknown */
		msg_err(msg.m_sender, EINVAL);
		break;
	}
	goto loop;
}

/*
 * usage()
 *	Tell how to use the thing
 *
 * Doesn't cover compatibility usage
 */
static void
usage(void)
{
	printf("Usage is: vstafs [-d <disk>] [-n <name>] [-p] [-f]\n");
	exit(1);
}

/*
 * verify_root()
 *	Read in root sector and apply a sanity check
 *
 * Exits on error.
 */
static void
verify_root(void)
{
	/*
	 * Block device is open; read in the first block and verify
	 * that it looks like a superblock.
	 */
	fsroot = malloc(SECSZ);
	if (fsroot == 0) {
		syslog(LOG_ERR, "secbuf not allocated");
		exit(1);
	}
	read_sec(BASE_SEC, fsroot);
	if (fsroot->fs_magic != FS_MAGIC) {
		syslog(LOG_ERR, "bad magic number on filesystem");
		exit(1);
	}
	syslog(LOG_INFO, "%ld sectors", fsroot->fs_size);
}

/*
 * free_pending_secs()
 *	Take sectors marked as "to be freed" and do the deed
 *
 * When fsck finds "lost" space, it would like to make it available
 * to the filesystem.  Because the free block list is so tedious
 * to maintain, it's simpler to let fsck merely queue the blocks to
 * vstafs, and let it be freed within the filesystem code here.
 */
static void
free_pending_secs(void)
{
	uint x;
	struct alloc *a;
	ulong freed = 0;

	x = 0;
	a = fsroot->fs_freesecs;
	for (; (x < BASE_FREESECS) && a->a_start; ++x, ++a) {
		if ((a->a_start > fsroot->fs_size) ||
				((a->a_start + a->a_len) > fsroot->fs_size)) {
			syslog(LOG_ERR, "Bad pending sectors: %U..%U\n",
				a->a_start, a->a_start + a->a_len - 1);
			break;
		}
		free_block(a->a_start, a->a_len);
		freed += a->a_len;
	}

	/*
	 * Clear pending blocks
	 */
	bzero(fsroot->fs_freesecs, sizeof(struct alloc) * BASE_FREESECS);
	write_sec(BASE_SEC, fsroot);

	/*
	 * Free memory for this buffer, just to be a good citizen
	 */
	free(fsroot);
	fsroot = 0;
	syslog(LOG_INFO, "%U pending sectors freed", freed);
}

/*
 * main()
 *	Startup a filesystem
 */
int
main(int argc, char *argv[])
{
	int x;
	port_name fsname;
	char *namer_name = 0;
	char *disk = 0;

	/*
	 * Initialize syslog
	 */
	openlog("vstafs", LOG_PID, LOG_DAEMON);

	/*
	 * Check arguments
	 */
	while ((x = getopt(argc, argv, "d:n:fp")) > 0) {
		switch (x) {
		case 'd':
			disk = optarg;
			break;
		case 'n':
			namer_name = optarg;
			break;
		case 'f':
			fflag = 1;
			break;
		case 'p':
			pflag = 1;
			break;
		default:
			usage();
		}
	}

	/*
	 * Trailing arguments are disk/namer entry (compat)
	 */
	if (!disk && (optind < argc)) {
		disk = argv[optind++];
	}
	if (!namer_name && (optind < argc)) {
		namer_name = argv[optind++];
	}

	/*
	 * Verify usage
	 */
	if ((optind < argc) || !disk || !namer_name) {
		usage();
	}

	/*
	 * Convert path_open() semantics
	 */
	if (pflag) {
		char *buf;

		buf = malloc(strlen(disk) + 4);
		sprintf(buf, "//%s", disk);
		disk = buf;
	}

	/*
	 * Try to open the device.  Sleep and retry, to be tolerant
	 * during bootup.
	 */
	for (x = 0; x < 10; ++x) {
		blkdev = open(disk, O_RDWR);
		if (blkdev < 0) {
			sleep(1);
		} else {
			break;
		}
	}
	if (blkdev < 0) {
		syslog(LOG_ERR, "couldn't connect to block device %s: %s",
			disk, strerror());
		exit(1);
	}

	/*
	 * Allocate data structures we'll need
	 */
        filehash = hash_alloc(NCACHE/4);
	if (filehash == 0) {
		syslog(LOG_ERR, "file hash not allocated");
		exit(1);
        }

	/*
	 * Apply sanity checks on filesystem
	 */
	verify_root();

	/*
	 * Register filesystem name
	 */
	rootport = msg_port((port_name)0, &fsname);
	x = namer_register(namer_name, fsname);
	if (x < 0) {
		syslog(LOG_ERR, "can't register name '%s'", namer_name);
		exit(1);
	}

	/*
	 * Init our data structures
	 */
	init_buf(__fd_port(blkdev), CORESEC);
	init_node();
	init_block();

	/*
	 * Open access to the root filesystem
	 */
	rootdir = get_node(ROOT_SEC);
	ASSERT(rootdir, "VFS: can't open root");

	/*
	 * Free any pending fsck-identified space
	 */
	if (fflag) {
		free_pending_secs();
	} else if (fsroot->fs_freesecs[0].a_start) {
		syslog(LOG_INFO, "free sectors pending");
	}

	/*
	 * Start serving requests for the filesystem
	 */
	vfs_main();
	return(0);
}
