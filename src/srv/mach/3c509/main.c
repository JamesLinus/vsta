/*
 * main.c
 *	Main message handling
 */
#include <sys/fs.h>
#include <sys/perm.h>
#include <sys/namer.h>
#include <sys/param.h>
#include <sys/assert.h>
#include <sys/syscall.h>
#include <hash.h>
#include <llist.h>
#include <stdio.h>
#include <std.h>
#include <syslog.h>
#include <signal.h>
#include "el3.h"

extern char *valid_fname(char *, int);

static struct hash *filehash;	/* Map session->context structure */

port_t el3port;		/* Port we receive contacts through */
port_name el3name;	/*  ...its name */

/*
 * Per-adapter state information
 */
struct adapter adapters[NEL3];

/*
 * Top-level protection for NE hierarchy
 */
struct prot el3_prot = {
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
	int nperms;

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
	bzero(f, sizeof(struct file));

	/*
	 * Fill in fields
	 */
	f->f_nperm = nperms;
	bcopy(m->m_buf, &f->f_perms, nperms * sizeof(struct perm));
	f->f_perm = perm_calc(f->f_perms, f->f_nperm, &el3_prot);

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
	struct attach *o;

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
	o = f->f_file;
	if (o) {
		o->a_refs += 1;
		ASSERT_DEBUG(o->a_refs > 0, "dup_client: overflow");
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
	el3_close(f);
	free(f);
}

/*
 * el3_main()
 *	Endless loop to receive and serve requests
 */
static void
el3_main()
{
	struct msg msg;
	int x;
	struct file *f;
loop:
	/*
	 * Receive a message, log an error and then keep going
	 */
	x = msg_receive(el3port, &msg);
	if (x < 0) {
		perror("el3: msg_receive");
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
		 * If active, remove from I/O queue.  Return completion.
		 */
		if (f->f_io) {
			ll_delete(f->f_io);
			f->f_io = 0;
		}
		msg.m_nseg = msg.m_arg = msg.m_arg1 = 0;
		msg_reply(msg.m_sender, &msg);
		break;
	case M_ISR:		/* Interrupt */
		ASSERT_DEBUG(f == 0, "el3: session from kernel");
		el3_isr();
		break;

	case FS_READ:		/* Read the disk */
		el3_read(&msg, f);
		break;

	case FS_WRITE:		/* Write the disk */
		el3_write(&msg, f);
		break;
	case FS_STAT:		/* Get stat of file */
		el3_stat(&msg, f);
		break;
	case FS_WSTAT:		/* Writes stats */
		el3_wstat(&msg, f);
		break;
	case FS_OPEN:		/* Move from dir down into drive */
		if (!valid_fname(msg.m_buf, x)) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		el3_open(&msg, f);
		break;
	default:		/* Unknown */
		msg_err(msg.m_sender, EINVAL);
		break;
	}
	goto loop;
}

static void
signalh (int sig)
{
	struct adapter *ap;
	int unit = 0;

	ap = &adapters[unit];

	syslog (LOG_INFO, "Closing 3c509 device...\n");
	el3_close_adapter (ap);

	exit (0);
}

/*
 * main()
 *	Startup of the 3c509 Ethernet server
 *
 * A 3Com509 instance expects to start with a command line:
 *	$ el3 <I/O base>,<IRQ> [<I/O base>,<IRQ>] ...
 */
int
main(int argc, char *argv[])
{
	struct adapter *ap;
	int unit, units;
	char *interfaces[] = { "10baseT", "AUI", "undefined", "10base2 (BNC)" };


	for (unit = 0; unix < NEL3; unit++) {
		ap = &adapters[unit];
		ap->a_base = 0;
	}
	for (unit = 0; unit < NEL3; unit++) {
		ap = &adapters[unit];
		if (!el3_probe (ap)) {
			break;
		}
	}

	/*
	 * Syslog
	 */
	openlog("el3", LOG_PID, LOG_DAEMON);

	/*
	 * Allocate handle->file hash table.  8 is just a guess
	 * as to what we'll have to handle.
	 */
        filehash = hash_alloc(8);
	if (filehash == 0) {
		perror("file hash");
		exit(1);
        }
	ll_init(&files);

	units = 0;
	for (unit = 0; unit < NEL3; unit++) {
		ap = &adapters[unit];

		if (ap->a_base == 0)
			continue;

		syslog(LOG_INFO, "3c509 %d: base=0x%x, IRQ=%d: interface=%s",
			unit, ap->a_base, ap->a_irq, interfaces[ap->a_if_port]);
		units += 1;

		/*
		 * We don't use ISA DMA, but we *do* want to be able to
		 * copyout directly into the user's buffer if they
		 * desire.  So we flag ourselves as DMA, but don't
		 * consume a channel.
		 */
		if (enable_dma(0) < 0) {
			perror("EL3 DMA");
			exit(1);
		}

		/*
		 * Enable I/O for the needed range
		 */
		if (enable_io(ap->a_base, ap->a_base+EL3_RANGE) < 0) {
			perror("EL3 I/O");
			exit(1);
		}

		/*
		 * Init our data structures.
		 */
		el3_init(ap);
		el3_configure(ap);

		/*
		 * Dump MAC address
		 */
		syslog(LOG_INFO, " MAC addr: %02x.%02x.%02x.%02x.%02x.%02x\n",
			ap->a_addr[0], ap->a_addr[1],
			ap->a_addr[2], ap->a_addr[3],
			ap->a_addr[4], ap->a_addr[5]);
	}

	/*
	 * If no units found, bail
	 */
	if (units == 0) {
		syslog(LOG_ERR, "No 3c509 units found, exiting.\n");
		exit(1);
	}

	signal (SIGINT, signalh);
	signal (SIGTERM, signalh);

	rw_init();

	/*
	 * Get a port for the 3c509 task
	 */
	el3port = msg_port((port_name)0, &el3name);

	/*
	 * Register as 3c509 Ethernet controller
	 */
	if (namer_register("net/el3", el3name) < 0) {
		syslog(LOG_ERR, "el3: can't register name\n");
		exit(1);
	}

	/*
	 * Tell system about our I/O vector
	 */
	if (enable_isr(el3port, ap->a_irq)) {
		perror("EL3 IRQ");
		exit(1);
	}

	/*
	 * Start serving requests for the filesystem
	 */
	el3_main();
	return(0);
}
