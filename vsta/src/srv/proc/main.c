/*
 * main.c
 *	Main processing loop for /proc
 */
#include "proc.h"
#include <sys/fs.h>
#include <sys/perm.h>
#include <sys/pstat.h>
#include <sys/namer.h>
#include <hash.h>
#include <stdlib.h>
#include <syslog.h>

static struct hash *filehash;

/*
 * proc_seek()
 *	Set file position
 */
void
proc_seek(struct msg *m, struct file *f)
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
	int i;

	/*
	 * Get data structure
	 */
	if ((f = malloc(sizeof(struct file))) == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}

	/*
	 * Hash under the sender's handle
	 */
        if (hash_insert(filehash, m->m_sender, f)) {
		free(f);
		msg_err(m->m_sender, ENOMEM);
		return;
	}

	f->f_active = 0;
	f->f_pos = 0L;
	f->f_pid = -1;
	f->f_nperm = 0;
	f->f_ops = &root_ops;
	for (i = 0; i < (m->m_buflen / sizeof(struct perm)); i++) {
		struct perm *pm = &((struct perm *)m->m_buf)[i];
		if (!PERM_DISABLED(pm) && PERM_ACTIVE(pm)) {
			f->f_perms[f->f_nperm++] = *pm;
		}
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
	 * Bulk copy
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
	/*
	 * Remove from client hash, release ref to our
	 * current node.
	 */
	(void)hash_delete(filehash, m->m_sender);
	free(f);
}

/*
 * proc_main()
 *	Endless loop to receive and serve requests
 */
static void
proc_main(port_t rootport)
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
	case FS_OPEN:
		(*(f->f_ops->open))(&msg, f);
		break;
	case FS_SEEK:		/* Set new file position */
		(*(f->f_ops->seek))(&msg, f);
		break;
	case FS_READ:		/* Read file */
		(*(f->f_ops->read))(&msg, f, x);
		break;
	case FS_WRITE:		/* Write file */
		(*(f->f_ops->write))(&msg, f, x);
		break;
	case FS_STAT:		/* Tell about file */
		(*(f->f_ops->stat))(&msg, f);
		break;
	case FS_WSTAT:
		(*(f->f_ops->wstat))(&msg, f);
		break;
	default:		/* Unknown */
		syslog(LOG_INFO, "unhandled type %d", msg.m_op);
		msg_err(msg.m_sender, EINVAL);
		break;
	}
	goto loop;
}

/*
 * main()
 *	Startup of proc server
 */
int
main(void)
{
	port_t rootport;
	port_name fsname;
	int x;

	/*
	 * Initialize syslog
	 */
	openlog("proc", LOG_PID, LOG_DAEMON);

	/*
	 * Allocate data structures we'll need
	 */
        filehash = hash_alloc(16);
	if (filehash == 0) {
		syslog(LOG_ERR, "file hash not allocated");
		exit(1);
        }

	/*
	 * Last check is that we can register with the given name.
	 */
	rootport = msg_port((port_name)0, &fsname);
	x = namer_register("proc", fsname);
	if (x < 0) {
		syslog(LOG_ERR, "unable to register with namer");
		exit(1);
	}

	/*
	 * Start serving requests for the filesystem
	 */
	syslog(LOG_INFO, "proc filesystem started");
	proc_main(rootport);

	/*
	 * Should never return from here!
	 */
	return(1);
}
