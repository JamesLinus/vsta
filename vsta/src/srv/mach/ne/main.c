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
#include "ne.h"

extern char *valid_fname(char *, int);

static struct hash *filehash;	/* Map session->context structure */

port_t neport;		/* Port we receive contacts through */
port_name nename;	/*  ...its name */

/*
 * Per-adapter state information
 */
struct adapter adapters[NNE];

/*
 * Top-level protection for NE hierarchy
 */
struct prot ne_prot = {
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
	f->f_perm = perm_calc(f->f_perms, f->f_nperm, &ne_prot);

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
	ne_close(f);
	free(f);
}

/*
 * ne_main()
 *	Endless loop to receive and serve requests
 */
static void
ne_main()
{
	struct msg msg;
	int x;
	struct file *f;
loop:
	/*
	 * Receive a message, log an error and then keep going
	 */
	x = msg_receive(neport, &msg);
	if (x < 0) {
		perror("ne: msg_receive");
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
		ASSERT_DEBUG(f == 0, "ne: session from kernel");
		ne_isr();
		break;

	case FS_READ:		/* Read the disk */
		ne_read(&msg, f);
		break;

	case FS_WRITE:		/* Write the disk */
		ne_write(&msg, f);
		break;
	case FS_STAT:		/* Get stat of file */
		ne_stat(&msg, f);
		break;
	case FS_WSTAT:		/* Writes stats */
		ne_wstat(&msg, f);
		break;
	case FS_OPEN:		/* Move from dir down into drive */
		if (!valid_fname(msg.m_buf, x)) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		ne_open(&msg, f);
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
	printf("Usage is: ne <I/O base>,<IRQ> [<I/O base>,<IRQ>] ...\n");
	exit(1);
}

/*
 * main()
 *	Startup of the NE2000 Ethernet server
 *
 * A NE instance expects to start with a command line:
 *	$ ne <I/O base>,<IRQ> [<I/O base>,<IRQ>] ...
 */
int
main(int argc, char *argv[])
{
	struct adapter *ap;
	int base, irq;
	int unit, units;
	char *cp;

	/*
	 * Check arguments
	 */
	if (argc < 2) {
		usage ();
	}
	argc--;
	for (unit = 0; unit < NNE; unit++) {
		ap = &adapters[unit];
		if (unit >= argc) {
			ap->a_base = 0;		/* not found */
			continue;
		}
		cp = argv[1+unit];
		if (strncmp(cp, "0x", 2) == 0)
			cp += 2;
		sscanf(cp, "%x,%d", &base, &irq);
		ap->a_base = base;
		ap->a_irq = irq;
	}

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
	for (unit = 0; unit < NNE; unit++) {
		ap = &adapters[unit];

		if (ap->a_base == 0)
			continue;

		printf("NE2000 %d: base=0x%x, IRQ=%d: ",
			unit, ap->a_base, ap->a_irq);
		units += 1;

		/*
		 * We don't use ISA DMA, but we *do* want to be able to
		 * copyout directly into the user's buffer if they
		 * desire.  So we flag ourselves as DMA, but don't
		 * consume a channel.
		 */
		if (enable_dma(0) < 0) {
			perror("NE DMA");
			exit(1);
		}

		/*
		 * Enable I/O for the needed range
		 */
		if (enable_io(ap->a_base, ap->a_base+NE_RANGE) < 0) {
			perror("NE I/O");
			exit(1);
		}

		/*
		 * Init our data structures.
		 */
		ne_init(ap);
		ne_configure(ap);

		/*
		 * Dump MAC address
		 */
		printf("%02x.%02x.%02x.%02x.%02x.%02x\n",
			ap->a_addr[0], ap->a_addr[1],
			ap->a_addr[2], ap->a_addr[3],
			ap->a_addr[4], ap->a_addr[5]);
	}

	/*
	 * If no units found, bail
	 */
	if (units == 0) {
		printf("No NE units found, exiting.\n");
		exit(1);
	}

	rw_init();

	/*
	 * Get a port for the NE2000 task
	 */
	neport = msg_port((port_name)0, &nename);

	/*
	 * Register as NE2000 Ethernet controller
	 */
	if (namer_register("net/ne", nename) < 0) {
		fprintf(stderr, "NE: can't register name\n");
		exit(1);
	}

	/*
	 * Tell system about our I/O vector
	 */
	if (enable_isr(neport, ap->a_irq)) {
		perror("NE IRQ");
		exit(1);
	}

	/*
	 * Start serving requests for the filesystem
	 */
	ne_main();
	return(0);
}
