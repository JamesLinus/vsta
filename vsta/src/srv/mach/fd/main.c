/*
 * main.c
 *	Main message handling
 */
#include <sys/msg.h>
#include <sys/perm.h>
#include <sys/fs.h>
#include <sys/namer.h>
#include <hash.h>
#include <stdio.h>
#include <sys/ports.h>
#include <sys/assert.h>
#include "fd.h"

#define MAXBUF (32*1024)

extern void fd_rw(), abort_io(), *malloc(), fd_init(), fd_isr(),
	fd_stat(), fd_wstat(), fd_readdir(), fd_open(), fd_close();
extern char *strerror();

static struct hash *filehash;	/* Map session->context structure */

port_t fdport;		/* Port we receive contacts through */
port_name fdname;	/*  ...its name */

/*
 * Default protection for floppy drives:  anybody can read/write, sys
 * can chmod them.
 */
struct prot fd_prot = {
	1,
	ACC_READ|ACC_WRITE,
	{1},
	{ACC_CHMOD}
};

/*
 * new_client()
 *	Create new per-connect structure
 */
static void
new_client(struct msg *m)
{
	struct file *f;
	struct perm *perms;
	int uperms, nperms, desired;

	/*
	 * See if they're OK to access
	 */
	perms = (struct perm *)m->m_buf;
	nperms = (m->m_buflen)/sizeof(struct perm);
	uperms = perm_calc(perms, nperms, &fd_prot);
	desired = m->m_arg & (ACC_WRITE|ACC_READ|ACC_CHMOD);
	if ((uperms & desired) != desired) {
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
	 * Fill in fields.
	 */
	bzero(f, sizeof(*f));
	f->f_sender = m->m_sender;
	f->f_flags = uperms;
	f->f_unit = ROOTDIR;

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
	 * Fill in fields.  Simply duplicate old file.
	 */
	ASSERT(fold->f_list == 0, "dup_client: busy");
	*f = *fold;
	f->f_sender = m->m_arg;

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
	m->m_arg = m->m_buflen = m->m_nseg = m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

/*
 * dead_client()
 *	Someone has gone away.  Free their info.
 */
static void
dead_client(struct msg *m, struct file *f)
{
	fd_close(m, f);
	(void)hash_delete(filehash, m->m_sender);
	free(f);
}

/*
 * fd_main()
 *	Endless loop to receive and serve requests
 */
static void
fd_main()
{
	struct msg msg;
	int x;
	struct file *f;
	char *buf = 0;

loop:
	/*
	 * Receive a message, log an error and then keep going
	 */
	x = msg_receive(fdport, &msg);
	if (x < 0) {
		perror("fd: msg_receive");
		goto loop;
	}

	/*
	 * Must fit in one buffer.  XXX scatter/gather might be worth
	 * the trouble for FS_RW() operations.
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
		 * Hunt down any active operation for this
		 * handle, and abort it.  Then answer with
		 * an abort acknowledge.
		 */
		if (f->f_list) {
			abort_io(f);
		}
		msg_reply(msg.m_sender, &msg);
		break;
	case M_ISR:		/* Interrupt */
		ASSERT_DEBUG(f == 0, "fd: session from kernel");
		fd_isr();
		break;
	case M_TIME:		/* Time event */
		fd_time(msg.m_arg);
		break;

	case FS_SEEK:		/* Set position */
		if (!f || (msg.m_arg < 0)) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		f->f_pos = msg.m_arg;
		msg.m_arg = msg.m_arg1 = msg.m_nseg = 0;
		msg_reply(msg.m_sender, &msg);
		break;

	case FS_ABSREAD:	/* Set position, then read */
	case FS_ABSWRITE:	/* Set position, then write */
		if (!f || (msg.m_arg1 < 0)) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		f->f_pos = msg.m_arg1;
		msg.m_op = ((msg.m_op == FS_ABSREAD) ? FS_READ : FS_WRITE);

		/* VVV fall into VVV */

	case FS_READ:		/* Read the floppy */
	case FS_WRITE:		/* Write the floppy */
		fd_rw(&msg, f);
		break;

	case FS_STAT:		/* Get stat of file */
		fd_stat(&msg, f);
		break;
	case FS_WSTAT:		/* Writes stats */
		fd_wstat(&msg, f);
		break;
	case FS_OPEN:		/* Move from dir down into drive */
		if (!valid_fname(msg.m_buf, x)) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		fd_open(&msg, f);
		break;
	default:		/* Unknown */
		msg_err(msg.m_sender, EINVAL);
		break;
	}
	if (buf) {
		free(buf);
		buf = 0;
	}
	goto loop;
}

/*
 * main()
 *	Startup of the floppy server
 */
main()
{
#ifdef DEBUG
	int scrn, kbd;

	kbd = msg_connect(PORT_KBD, ACC_READ);
	(void)__fd_alloc(kbd);
	scrn = msg_connect(PORT_CONS, ACC_WRITE);
	(void)__fd_alloc(scrn);
	(void)__fd_alloc(scrn);
#endif

	/*
	 * Allocate handle->file hash table.  8 is just a guess
	 * as to what we'll have to handle.
	 */
        filehash = hash_alloc(8);
	if (filehash == 0) {
		perror("file hash");
		exit(1);
        }

	/*
	 * We still have to program our own DMA.  This gives the
	 * system just enough information to shut down the channel
	 * on process abort.  It can also catch accidentally launching
	 * two floppy tasks.
	 */
	if (enable_dma(FD_DRQ) < 0) {
		perror("Floppy DMA");
		exit(1);
	}

	/*
	 * Init our data structures.  We must enable_dma() first, because
	 * init wires a bounce buffer, and you have to be a DMA-type
	 * server to wire memory.
	 */
	fd_init();

	/*
	 * Enable I/O for the needed range
	 */
	if (enable_io(FD_LOW, FD_HIGH) < 0) {
		perror("Floppy I/O");
		exit(1);
	}

	/*
	 * Get a port for the floppy task
	 */
	fdport = msg_port((port_name)0, &fdname);

	/*
	 * Register as floppy drives
	 */
	if (namer_register("disk/fd", fdname) < 0) {
		fprintf(stderr, "FD: can't register name\n");
		exit(1);
	}

	/*
	 * Tell system about our I/O vector
	 */
	if (enable_isr(fdport, FD_IRQ)) {
		perror("Floppy IRQ");
		exit(1);
	}

	/*
	 * Start serving requests for the filesystem
	 */
	fd_main();
}
