/*
 * main.c
 *	Main handling loop and startup
 */
#include <sys/namer.h>
#include "tmpfs.h"
#include <hash.h>
#include <llist.h>
#include <stdio.h>
#include <fcntl.h>
#include <std.h>
#include <syslog.h>

#define NCACHE (16)	/* Roughly, # clients */

extern void tmpfs_open(), tmpfs_read(), tmpfs_write(),
	tmpfs_remove(), tmpfs_stat(), tmpfs_close(), tmpfs_wstat();

static struct hash	/* Map of all active users */
	*filehash;
port_t rootport;	/* Port we receive contacts through */
struct llist		/* All files in filesystem */
	files;
char *zeroes;		/* BLOCKSIZE worth of 0's */

/*
 * tmpfs_seek()
 *	Set file position
 */
static void
tmpfs_seek(struct msg *m, struct file *f)
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

	/*
	 * Get data structure
	 */
	if ((f = malloc(sizeof(struct file))) == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}

	/*
	 * Fill in fields
	 */
	f->f_file = 0;
	f->f_pos = 0;
	f->f_nperm = nperms;
	bcopy(m->m_buf, &f->f_perms, nperms * sizeof(struct perm));
	f->f_perm = ACC_READ|ACC_WRITE;

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
	 * Fill in fields
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
	 * Add ref
	 */
	if (f->f_file) {
		f->f_file->o_refs += 1;
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
	tmpfs_close(f);
	free(f);
}

/*
 * tmpfs_main()
 *	Endless loop to receive and serve requests
 */
static void
tmpfs_main()
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
		syslog(LOG_ERR, "tmpfs: msg_receive");
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
		if ((msg.m_nseg != 1) || !valid_fname(msg.m_buf,
				msg.m_buflen)) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		tmpfs_open(&msg, f);
		break;

	case FS_ABSREAD:	/* Set position, then read */
		if (msg.m_arg1 < 0) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		f->f_pos = msg.m_arg1;
		/* VVV fall into VVV */
	case FS_READ:		/* Read file */
		tmpfs_read(&msg, f);
		break;

	case FS_ABSWRITE:	/* Set position, then write */
		if (msg.m_arg1 < 0) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		f->f_pos = msg.m_arg1;
		/* VVV fall into VVV */
	case FS_WRITE:		/* Write file */
		tmpfs_write(&msg, f);
		break;

	case FS_SEEK:		/* Set new file position */
		tmpfs_seek(&msg, f);
		break;
	case FS_REMOVE:		/* Get rid of a file */
		if ((msg.m_nseg != 1) || !valid_fname(msg.m_buf,
				msg.m_buflen)) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		tmpfs_remove(&msg, f);
		break;
	case FS_STAT:		/* Tell about file */
		tmpfs_stat(&msg, f);
		break;
	case FS_WSTAT:		/* Set stuff on file */
		tmpfs_wstat(&msg, f);
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
	printf("Usage is: tmpfs <fsname>\n");
	exit(1);
}

/*
 * main()
 *	Startup of a boot filesystem
 *
 * A TMPFS instance expects to start with a command line:
 *	$ tmpfs <block class> <block instance> <filesystem name>
 */
main(int argc, char *argv[])
{
	port_name fsname;
	char *namer_name;
	int x;

	/*
	 * Check arguments
	 */
	if (argc != 2) {
		usage();
	}

	/*
	 * Zero memory
	 */
	zeroes = malloc(BLOCKSIZE);
	if (zeroes == 0) {
		syslog(LOG_ERR, "tmpfs: zeroes");
		exit(1);
	}
	bzero(zeroes, BLOCKSIZE);

	/*
	 * Name we'll offer service as
	 */
	namer_name = argv[1];

	/*
	 * Allocate data structures we'll need
	 */
        filehash = hash_alloc(NCACHE/4);
	if (filehash == 0) {
		syslog(LOG_ERR, "tmpfs: file hash");
		exit(1);
        }
	ll_init(&files);

	/*
	 * Last check is that we can register with the given name.
	 */
	rootport = msg_port((port_name)0, &fsname);
	x = namer_register(namer_name, fsname);
	if (x < 0) {
		syslog(LOG_ERR, "tmpfs: can't register name\n");
		exit(1);
	}

	/*
	 * Start serving requests for the filesystem
	 */
	tmpfs_main();
}
