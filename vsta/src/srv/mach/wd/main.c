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
#include <sys/param.h>
#include <sys/assert.h>
#include <sys/syscall.h>
#include <std.h>
#include <syslog.h>
#include "wd.h"

extern void wd_rw(), wd_init(), rw_init(), wd_isr(),
	wd_stat(), wd_wstat(), wd_readdir(), wd_open(),
	rw_readpartitions();
extern int valid_fname();

static struct hash *filehash;	/* Map session->context structure */

port_t wdport;		/* Port we receive contacts through */
port_name wdname;	/*  ...its name */
uint partundef;		/* Can we take clients yet? */
char *secbuf;		/* Sector-aligned buffer for bootup */
extern uint first_unit;	/* Lowerst unit # configured */

/*
 * Per-disk state information
 */
struct disk disks[NWD];

/*
 * Top-level protection for WD hierarchy
 */
struct prot wd_prot = {
	2,
	0,
	{1, 1},
	{ACC_READ, ACC_WRITE|ACC_CHMOD}
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
	uperms = perm_calc(perms, nperms, &wd_prot);
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
	f->f_node = ROOTDIR;

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
	(void)hash_delete(filehash, m->m_sender);
	free(f);
}

/*
 * wd_main()
 *	Endless loop to receive and serve requests
 */
static void
wd_main()
{
	struct msg msg;
	int x;
	struct file *f;
loop:
	/*
	 * Receive a message, log an error and then keep going
	 */
	x = msg_receive(wdport, &msg);
	if (x < 0) {
		syslog(LOG_ERR, "msg_receive");
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
		if (partundef) {
			msg_err(msg.m_sender, EBUSY);
			break;
		}
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
		 * If active, flag operation as aborted.  If not,
		 * return abort answer immediately.
		 */
		if (f->f_list) {
			f->f_abort = 1;
		} else {
			msg.m_nseg = msg.m_arg = 0;
			msg_reply(msg.m_sender, &msg);
		}
		break;
	case M_ISR:		/* Interrupt */
		ASSERT_DEBUG(f == 0, "wd: session from kernel");
		wd_isr();
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

	case FS_READ:		/* Read the disk */
	case FS_WRITE:		/* Write the disk */
		wd_rw(&msg, f);
		break;

	case FS_STAT:		/* Get stat of file */
		wd_stat(&msg, f);
		break;
	case FS_WSTAT:		/* Writes stats */
		wd_wstat(&msg, f);
		break;
	case FS_OPEN:		/* Move from dir down into drive */
		if (!valid_fname(msg.m_buf, x)) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		wd_open(&msg, f);
		break;
	default:		/* Unknown */
		msg_err(msg.m_sender, EINVAL);
		break;
	}
	goto loop;
}

/*
 * main()
 *	Startup of the WD hard disk server
 */
int
main(argc, argv)
	int argc;
	char **argv;
{
	int i;

	/*
	 * Initialize syslog
	 */
	openlog("wd", LOG_PID, LOG_DAEMON);

	/*
	 * Allocate handle->file hash table.  8 is just a guess
	 * as to what we'll have to handle.
	 */
        filehash = hash_alloc(8);
	if (filehash == 0) {
		syslog(LOG_ERR, "file hash");
		exit(1);
        }

	/*
	 * We don't use ISA DMA, but we *do* want to be able to
	 * copyout directly into the user's buffer if they
	 * desire.  So we flag ourselves as DMA, but don't
	 * consume a channel.
	 */
	if (enable_dma(0) < 0) {
		syslog(LOG_ERR, "DMA");
		exit(1);
	}

	/*
	 * Enable I/O for the needed range
	 */
	if (enable_io(WD_LOW, WD_HIGH) < 0) {
		syslog(LOG_ERR, "I/O");
		exit(1);
	}

	/*
	 * Init our data structures.
	 */
	wd_init(argc, argv);
	rw_init();

	/*
	 * Get a port for the disk task
	 */
	wdport = msg_port((port_name)0, &wdname);

	/*
	 * Register as WD hard drive
	 */
	if (namer_register("disk/wd", wdname) < 0) {
		syslog(LOG_ERR, "can't register name\n");
		exit(1);
	}

	/*
	 * Tell system about our I/O vector
	 */
	if (enable_isr(wdport, WD_IRQ)) {
		syslog(LOG_ERR, "IRQ attach");
		exit(1);
	}

	/*
	 * Kick off I/O's to get the disk partition table entries.
	 * We will reject clients until this process is finished.
	 */
	partundef = 0;
	for (i = first_unit; i < NWD; i++) {
		partundef |= (1 << i);
	}
	rw_readpartitions(first_unit);

	/*
	 * Start serving requests for the filesystem
	 */
	wd_main();
	return(0);
}
