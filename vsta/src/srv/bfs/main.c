/*
 * main.c
 *	Main handling loop and startup
 */
#include <sys/perm.h>
#include <sys/namer.h>
#include "bfs.h"
#include <hash.h>
#include <stdio.h>
#include <fcntl.h>
#ifdef DEBUG
#include <sys/ports.h>
#endif

extern void *malloc(), bfs_open(), bfs_read(), bfs_write(), *bdata(),
	*bget(), bfs_remove(), bfs_stat();
extern char *strerror();

int blkdev;		/* Device this FS is mounted upon */
port_t rootport;	/* Port we receive contacts through */
struct super		/* Our filesystem's superblock */
	*sblock;
void *shandle;		/*  ...handle for the block entry */
static struct hash	/* Handle->filehandle mapping */
	*filehash;

/*
 * Protection for all BFS files: everybody can read, only
 * group 1.1 (sys.sys) can write.
 */
static struct prot bfs_prot = {
	2,
	ACC_READ|ACC_EXEC,
	{1,		1},
	{0,		ACC_WRITE}
};

/*
 * bfs_seek()
 *	Set file position
 */
static void
bfs_seek(struct msg *m, struct file *f)
{
	if (m->m_arg < 0) {
		msg_err(m->m_sender, EINVAL);
		return;
	}
	f->f_pos = m->m_arg;
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
	uperms = perm_calc(perms, nperms, &bfs_prot);
	if ((m->m_arg & ACC_WRITE) && !(uperms & ACC_WRITE)) {
		msg_err(m->m_sender, EPERM);
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
	 * Fill in fields.  Note that our buffer is the
	 * information on the permissions our client
	 * possesses.  For an M_CONNECT, the message is
	 * from the kernel, and trusted.
	 */
	f->f_inode = ROOTINO;
	f->f_pos = 0L;
	f->f_write = (uperms & ACC_WRITE);

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
 *
 * This is more of a Plan9 clone operation.  The intent is
 * to not share a struct file, so that when you walk it down
 * a level or seek it, you don't affect the thing you cloned
 * off from.
 *
 * This is a kernel-generated message; the m_sender is the
 * current user; m_arg specifies a handle which will be used
 * if we complete the operation with success.
 */
static void
dup_client(struct msg *m, struct file *fold)
{
	struct file *f;
	extern void iref();

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
	f->f_inode = fold->f_inode;
	f->f_pos = fold->f_pos;
	f->f_write = fold->f_write;
	if (f->f_inode != ROOTINO)
		iref(f->f_inode);

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
	extern void bfs_close();

	(void)hash_delete(filehash, m->m_sender);
	bfs_close(f);
	free(f);
}

/*
 * bfs_main()
 *	Endless loop to receive and serve requests
 */
static void
bfs_main()
{
	struct msg msg;
	seg_t resid;
	char *buf2 = 0;
	int x;
	struct file *f;

loop:
	/*
	 * Receive a message, log an error and then keep going
	 */
	x = msg_receive(rootport, &msg);
	if (x < 0) {
		perror("bfs: msg_receive");
		goto loop;
	}

	/*
	 * If we've received more than a buffer of data, pull it in
	 * to a dynamic buffer.
	 */
	if (msg.m_nseg > 1) {
		buf2 = malloc(x);
		if (buf2 == 0) {
			msg_err(msg.m_sender, E2BIG);
			goto loop;
		}
		if (seg_copyin(msg.m_seg,
				msg.m_nseg, buf2, x) < 0) {
			msg_err(msg.m_sender, strerror());
			goto loop;
		}
		msg.m_buf = buf2;
		msg.m_buflen = x;
		msg.m_nseg = 1;
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
		bfs_open(&msg, f);
		break;
	case FS_READ:		/* Read file */
		bfs_read(&msg, f);
		break;
	case FS_WRITE:		/* Write file */
		bfs_write(&msg, f);
		break;
	case FS_SEEK:		/* Set new file position */
		bfs_seek(&msg, f);
		break;
	case FS_REMOVE:		/* Get rid of a file */
		bfs_remove(&msg, f);
		break;
	case FS_STAT:		/* Tell about file */
		bfs_stat(&msg, f);
		break;
	default:		/* Unknown */
		msg_err(msg.m_sender, EINVAL);
		break;
	}

	/*
	 * Free dynamic storage if in use
	 */
	if (buf2) {
		free(buf2);
		buf2 = 0;
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
	printf("Usage is: bfs -p <portname> <portpath> <fsname>\n");
	printf(" or: bfs <filepath> <fsname>\n");
	exit(1);
}
/*
 * main()
 *	Startup of a boot filesystem
 *
 * A BFS instance expects to start with a command line:
 *	$ bfs <block class> <block instance> <filesystem name>
 */
main(int argc, char *argv[])
{
	port_t port;
	port_name fsname;
	struct msg msg;
	int chan, fd, x;
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
			printf("BFS: couldn't connect to block device.\n");
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
	binit();
	iinit();
	dir_init();

	/*
	 * Block device is open; read in the first block and verify
	 * that it looks like a superblock.
	 */
	shandle = bget(0);
	if (!shandle) {
		perror("BFS superblock");
		exit(1);
	}
	sblock = bdata(shandle);
	if (sblock->s_magic != SMAGIC) {
		fprintf(stderr, "BFS: bad superblock on %s\n", argv[1]);
		exit(1);
	}

	/*
	 * Block device looks good.  Last check is that we can register
	 * with the given name.
	 */
	rootport = msg_port((port_name)0, &fsname);
	x = namer_register(namer_name, fsname);
	if (x < 0) {
		fprintf(stderr, "BFS: can't register name\n");
		exit(1);
	}

	/*
	 * Start serving requests for the filesystem
	 */
	bfs_main();
}
