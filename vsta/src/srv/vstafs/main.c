/*
 * main.c
 *	Main loop for message processing
 */
#include <vstafs/vstafs.h>
#include <sys/fs.h>
#include <sys/perm.h>
#include <namer/namer.h>
#include <lib/hash.h>
#include <stdio.h>
#include <fcntl.h>
#include <std.h>
#include <sys/assert.h>
#ifdef DEBUG
#include <sys/ports.h>
#endif

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
	int uperms, nperms;

	/*
	 * See if they're OK to access
	 */
	perms = (struct perm *)m->m_buf;
	nperms = (m->m_buflen)/sizeof(struct perm);

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
	f->f_file = rootdir;
	ref_node(rootdir);
	f->f_pos = OFF_DATA;
	bcopy(perms, f->f_perms, nperms * sizeof(struct perm));
	f->f_nperm = nperms;
	f->f_perm = fs_perms(f->f_perms, f->f_nperm, rootdir);

	/*
	 * Hash under the sender's handle
	 */
        if (hash_insert(filehash, m->m_sender, f)) {
		free(f);
		msg_err(m->m_sender, ENOMEM);
		return;
	}

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

	(void)hash_delete(filehash, m->m_sender);
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
		perror("vfs: msg_receive");
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
	case FS_FID:		/* File ID */
		vfs_fid(&msg, f);
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
	printf("Usage is: vfs -p <portname> <portpath> <fsname>\n");
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
		perror("vfs: secbuf");
		exit(1);
	}
	read_sec(ROOT_SEC, secbuf);
	fsroot = (struct fs *)secbuf;
	if (fsroot->fs_magic != FS_MAGIC) {
		printf("Bad magic number on filesystem\n");
		exit(1);
	}
	free(secbuf);
}

/*
 * main()
 *	Startup a filesystem
 */
main(int argc, char *argv[])
{
	int x;
	port_name fsname;
	char *namer_name;
#ifdef DEBUG
	int scrn, kbd;

	kbd = msg_connect(PORT_KBD, ACC_READ);
	(void)__fd_alloc(kbd);
	scrn = msg_connect(PORT_CONS, ACC_WRITE);
	(void)__fd_alloc(scrn);
	(void)__fd_alloc(scrn);
#endif
	/*
	 * Check arguments
	 */
	if (argc == 3) {
		blkdev = open(argv[1], O_RDWR);
		if (blkdev < 0) {
			perror(argv[1]);
			exit(1);
		}
		namer_name = argv[2];
	} else if (argc == 5) {
		port_name blkname;
		port_t port;
		int retries;

		/*
		 * Version of invocation where service is specified
		 */
		if (strcmp(argv[1], "-p")) {
			usage();
		}
		for (retries = 10; retries > 0; retries -= 1) {
			port = -1;
			blkname = namer_find(argv[2]);
			if (blkname >= 0) {
				port = msg_connect(blkname, ACC_READ|ACC_WRITE);
			}
			if (port < 0) {
				sleep(1);
			} else {
				break;
			}
		}
		if (port < 0) {
			printf("VFS: couldn't connect to block device.\n");
			exit(1);
		}
		if (mountport("/mnt", port) < 0) {
			perror("/mnt");
			exit(1);
		}
		if (chdir("/mnt") < 0) {
			perror("chdir /mnt");
			exit(1);
		}
		blkdev = open(argv[3], O_RDWR);
		if (blkdev < 0) {
			perror(argv[3]);
			exit(1);
		}
		namer_name = argv[4];
	} else {
		usage();
	}

	/*
	 * Allocate data structures we'll need
	 */
        filehash = hash_alloc(NCACHE/4);
	if (filehash == 0) {
		perror("file hash");
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
		fprintf(stderr, "VFS: can't register name: %s\n", namer_name);
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
	rootdir = alloc_node(ROOT_SEC);
	ASSERT(rootdir, "VFS: can't open root");

	/*
	 * Start serving requests for the filesystem
	 */
	vfs_main();
}
