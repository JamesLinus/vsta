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

extern int valid_fname(char *, int);

int blkdev;		/* Device this FS is mounted upon */
port_t rootport;	/* Port we receive contacts through */
static struct hash	/* Handle->filehandle mapping */
	*filehash;

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
	*f = *fold;

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
	switch (msg.m_op) {
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
 */
static void
usage(void)
{
	printf("Usage is: vfs -p <portpath> <fsname>\n");
	printf(" or: vfs <filepath> <fsname>\n");
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
	struct fs *fsroot;
	char *secbuf;

	/*
	 * Block device is open; read in the first block and verify
	 * that it looks like a superblock.
	 */
	secbuf = malloc(SECSZ);
	if (secbuf == 0) {
		syslog(LOG_ERR, "secbuf not allocated");
		exit(1);
	}
	read_sec(BASE_SEC, secbuf);
	fsroot = (struct fs *)secbuf;
	if (fsroot->fs_magic != FS_MAGIC) {
		syslog(LOG_ERR, "bad magic number on filesystem");
		exit(1);
	}
	free(secbuf);
	syslog(LOG_INFO, "%ld sectors", fsroot->fs_size);
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
	char *namer_name;

	/*
	 * Initialize syslog
	 */
	openlog("vstafs", LOG_PID, LOG_DAEMON);

	/*
	 * Check arguments
	 */
	if (argc == 3) {
		namer_name = argv[2];
		blkdev = open(argv[1], O_RDWR);
		if (blkdev < 0) {
			syslog(LOG_ERR, "%s %s", argv[1], strerror());
			exit(1);
		}
	} else if (argc == 4) {
		port_t port;
		int retries;
		extern int __fd_alloc(port_t);
		extern port_t path_open(char *, int);

		/*
		 * Version of invocation where service is specified
		 */
		namer_name = argv[3];
		if (strcmp(argv[1], "-p")) {
			usage();
		}
		for (retries = 10; retries > 0; retries -= 1) {
			port = path_open(argv[2], ACC_READ|ACC_WRITE);
			if (port < 0) {
				sleep(1);
			} else {
				break;
			}
		}
		if (port < 0) {
			syslog(LOG_ERR, "couldn't connect to block device");
			exit(1);
		}
		blkdev = __fd_alloc(port);
		if (blkdev < 0) {
			perror(argv[2]);
			exit(1);
		}
	} else {
		namer_name = 0;	/* For -Wall */
		usage();
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
	init_buf();
	init_node();
	init_block();

	/*
	 * Open access to the root filesystem
	 */
	rootdir = get_node(ROOT_SEC);
	ASSERT(rootdir, "VFS: can't open root");

	/*
	 * Start serving requests for the filesystem
	 */
	vfs_main();
	return(0);
}
