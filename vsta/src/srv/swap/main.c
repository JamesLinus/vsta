/*
 * main.c
 *	Main function handler for swap I/O
 */
#include <sys/perm.h>
#include <namer/namer.h>
#include <swap/swap.h>
#include <lib/hash.h>
#include <lib/alloc.h>
#include <sys/ports.h>
#include <stdio.h>
#include <std.h>

extern void swap_rw(), swap_stat(), swapinit(), swap_alloc(), swap_free(),
	swap_add();

port_t rootport;	/* Port we receive contacts through */
static struct hash	/* Handle->filehandle mapping */
	*filehash;

/*
 * Protection for all SWAP files: any sys can read, only sys/sys
 * can write.
 */
static struct prot swap_prot = {
	2,
	0,
	{1,		1},
	{ACC_READ,	ACC_WRITE}
};

/*
 * swap_seek()
 *	Set file position
 */
static void
swap_seek(struct msg *m, struct file *f)
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
new_client(struct msg *m, uint len)
{
	struct file *f;
	struct perm *perms;
	int uperms, nperms;

	/*
	 * See if they're OK to access
	 */
	perms = (struct perm *)m->m_buf;
	nperms = len/sizeof(struct perm);
	uperms = perm_calc(perms, nperms, &swap_prot);
	if ((uperms & m->m_arg) != m->m_arg) {
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
	 * Fill in fields
	 */
	f->f_pos = 0L;
	f->f_perms = uperms;

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
	f->f_pos = fold->f_pos;
	f->f_perms = fold->f_perms;

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
	extern void swap_close();

	(void)hash_delete(filehash, m->m_sender);
	free(f);
}

/*
 * swap_main()
 *	Endless loop to receive and serve requests
 */
static void
swap_main()
{
	struct msg msg;
	int x;
	struct file *f;

loop:
	/*
	 * Receive a message, log an error and then keep going.  Note
	 * that since there's no buffer, we will get all data in
	 * terms of handles.
	 */
	x = msg_receive(rootport, &msg);
	if (x < 0) {
		perror("swap: msg_receive");
		goto loop;
	}

	/*
	 * Has to fit in one buf
	 */
	if (msg.m_nseg > 1) {
		msg_err(msg.m_sender, EINVAL);
		goto loop;
	}

	/*
	 * Categorize by basic message operation
	 */
	f = hash_lookup(filehash, msg.m_sender);
	switch (msg.m_op) {
	case M_CONNECT:		/* New client */
		new_client(&msg, x);
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
		msg.m_nseg = msg.m_arg = msg.m_arg1 = 0;
		msg_reply(msg.m_sender, &msg);
		break;
	case FS_ABSREAD:	/* Set position, then read */
	case FS_ABSWRITE:	/* Set position, then write */
		if (!f || (msg.m_arg1 < 0)) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		f->f_pos = msg.m_arg1;

		/* VVV fall into VVV */

	case FS_READ:		/* Read swap */
	case FS_WRITE:		/* Write swap */
		swap_rw(&msg, f, x);
		break;
	case FS_SEEK:		/* Set new file position */
		swap_seek(&msg, f);
		break;
	case FS_STAT:		/* Tell about swap */
		swap_stat(&msg, f);
		break;
	case SWAP_ADD:		/* Add new swap */
		swap_add(&msg, f, x);
		break;
	case SWAP_ALLOC:	/* Allocate some swap */
		swap_alloc(&msg, f);
		break;
	case SWAP_FREE:		/* Free some swap */
		swap_free(&msg, f);
		break;
	default:		/* Unknown */
		msg_err(msg.m_sender, EINVAL);
		break;
	}
	goto loop;
}

/*
 * main()
 *	Startup of swap support
 */
main()
{
#ifdef DEBUG
	{ port_t kbd, cons;
	  kbd = msg_connect(PORT_KBD, ACC_READ);
	  __fd_alloc(kbd);
	  cons = msg_connect(PORT_CONS, ACC_WRITE);
	  __fd_alloc(cons);
	  __fd_alloc(cons);
	}
#endif
	/*
	 * Our name, always
	 */
	(void)set_cmd("swap");

	/*
	 * Allocate data structures we'll need
	 */
        filehash = hash_alloc(16);
	if (filehash == 0) {
		perror("file hash");
		exit(1);
        }
	swapinit();

	/*
	 * Register as THE system swap task
	 */
	rootport = msg_port(PORT_SWAP, 0);
	if (rootport < 0) {
		fprintf(stderr, "SWAP: can't register name\n");
		exit(1);
	}

	/*
	 * Start serving requests for the filesystem
	 */
	swap_main();
}
