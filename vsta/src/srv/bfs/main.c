/*
 * Filename:	main.c
 * Developed:	Dave Hudson <dave@humbug.demon.co.uk>
 * Originated:	Andy Valencia
 * Last Update: 11th May 1994
 * Implemented:	GNU GCC version 2.5.7
 *
 * Description: Main message handling and startup routines for the bfs
 *		filesystem (boot file system)
 */


#include <fcntl.h>
#include <fdl.h>
#include <hash.h>
#include <mnttab.h>
#include <std.h>
#include <stdio.h>
#include <sys/namer.h>
#include <sys/perm.h>
#include <syslog.h>
#include "bfs.h"


int blkdev;			/* Device this FS is mounted upon */
port_t rootport;		/* Port we receive contacts through */
struct super *sblock;		/* Our filesystem's superblock */
void *shandle;			/*  ...handle for the block entry */
static struct hash *filehash;	/* Handle->filehandle mapping */


extern valid_fname(char *, int);
extern port_t path_open(char *, int);


/*
 * Protection for all BFS files: everybody can read, only
 * group 1.1 (sys.sys) can write.
 */
static struct prot bfs_prot = {
	2,
	ACC_READ|ACC_EXEC,
	{1, 1},
	{0, ACC_WRITE}
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
	f->f_inode = ino_find(ROOTINODE);
	ino_ref(f->f_inode);
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
	if (f->f_inode->i_num != ROOTINODE)
		ino_ref(f->f_inode);

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
	(void)hash_delete(filehash, m->m_sender);
	if (f->f_rename_id) {
		cancel_rename(f);
	}
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
	char *buf2 = 0;
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
	case M_CONNECT :	/* New client */
		new_client(&msg);
		break;
		
	case M_DISCONNECT :	/* Client done */
		dead_client(&msg, f);
		break;
		
	case M_DUP :		/* File handle dup during exec() */
		dup_client(&msg, f);
		break;

	case M_ABORT :		/* Aborted operation */
		/*
		 * Clear any pending renames
		 */
		if (f->f_rename_id) {
			cancel_rename(f);
		}

		/*
		 * We're synchronous, so presumably everything else
		 * is all done and this abort is old news.
		 */
		msg_reply(msg.m_sender, &msg);
		break;

	case FS_OPEN :		/* Look up file from directory */
		if (!valid_fname(msg.m_buf, msg.m_buflen)) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		bfs_open(&msg, f);
		break;

	case FS_READ :		/* Read file */
		bfs_read(&msg, f);
		break;

	case FS_WRITE :		/* Write file */
		bfs_write(&msg, f);
		break;
		
	case FS_SEEK :		/* Set new file position */
		bfs_seek(&msg, f);
		break;
		
	case FS_REMOVE :	/* Get rid of a file */
		bfs_remove(&msg, f);
		break;
		
	case FS_STAT :		/* Tell about file */
		bfs_stat(&msg, f);
		break;
		
	case FS_RENAME :	/* Rename a file */
		bfs_rename(&msg, f);
		break;

	default :		/* Unknown */
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
	printf("Usage: bfs -p <portpath> <fsname>\n" \
	       "   or: bfs <filepath> <fsname>\n");
	exit(1);
}

/*
 * main()
 *	Startup of a boot filesystem
 *
 * A BFS instance expects to start with a command line:
 *	$ bfs <block instance> <filesystem name>
 */
void
main(int argc, char *argv[])
{
	port_t port;
	port_name fsname;
	int x;
	char *namer_name;

	/*
	 * Initialize syslog
	 */
	openlog("bfs", LOG_PID, LOG_DAEMON);

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
		int retries;

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
			syslog(LOG_ERR, "couldn't connect to block device %s",
			       argv[2]);
			exit(1);
		}
		blkdev = __fd_alloc(port);
		if (blkdev < 0) {
			syslog(LOG_ERR, "%s %s", argv[2], strerror());
			exit(1);
		}
	} else {
		usage();
	}

	/*
	 * Allocate data structures we'll need
	 */
        filehash = hash_alloc(NCACHE / 4);
	if (filehash == 0) {
		syslog(LOG_ERR, "file hash allocation failed");
		exit(1);
        }

	/*
	 * Initialise the block handler
	 */
	binit();

	/*
	 * Block device is open; read in the first block and verify
	 * that it looks like a superblock.
	 */
	shandle = bget(0);
	if (!shandle) {
		syslog(LOG_ERR, "superblock read failed for %s", argv[1]);
		exit(1);
	}
	sblock = bdata(shandle);
	if (sblock->s_magic != SMAGIC) {
		syslog(LOG_ERR, "bad superblock on %s", argv[1]);
		exit(1);
	}

	/*
	 * OK, we have a superblock to work with now.  We can now initialise
	 * the directory and inode data
	 */
	ino_init();

	/*
	 * Block device looks good.  Last check is that we can register
	 * with the given name.
	 */
	rootport = msg_port((port_name)0, &fsname);
	x = namer_register(namer_name, fsname);
	if (x < 0) {
		syslog(LOG_ERR, "can't register with namer");
		exit(1);
	}

	syslog(LOG_INFO, "filesystem established");

	/*
	 * Start serving requests for the filesystem
	 */
	bfs_main();
}
