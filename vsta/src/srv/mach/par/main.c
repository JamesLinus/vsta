/*
 * main.c
 *	Main message handling
 */
#include <sys/perm.h>
#include <sys/fs.h>
#include <hash.h>
#include <stdio.h>
#include <sys/assert.h>
#include <stdlib.h>
#include <sys/ports.h>
#include <sys/msg.h>
#include <sys/namer.h>
#include "par.h"
#include "par_port.h"
extern volatile void exit(int error);

static struct hash *filehash;	/* Map session->context structure */

static port_t lp_port;		/* Port we receive contacts through */
uint accgen = 0;		/* Generation counter for access */
struct par_port printer;	/* the printer "object" */
int timeout = 30;		/* number of seconds before a write is
				   aborted */

/*
 * Protection for port; starts out with access for all.  sys can
 * change the protection label.
 */
struct prot par_prot = {
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
	uperms = perm_calc(perms, nperms, &par_prot);
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
	f->f_gen = accgen;
	f->f_flags = uperms;

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
	(void)hash_delete(filehash, m->m_sender);
	free(f);
}

/*
 * check_gen()
 *	Check access generation
 */
static int
check_gen(struct msg *m, struct file *f)
{
	if (f->f_gen != accgen) {
		msg_err(m->m_sender, EIO);
		return(1);
	}
	return(0);
}

/*
 * par_main()
 *	Endless loop to receive and serve requests
 */
static volatile void
par_main()
{
	struct msg msg;
	int x;
	struct file *f;
loop:
	/*
	 * Receive a message, log an error and then keep going
	 */
	x = msg_receive(lp_port, &msg);
	if (x < 0) {
		perror("par: msg_receive");
		goto loop;
	}

	/*
	 * All incoming data should fit in one buffer
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
		 * We are synchronous. At the time we handle this
		 * message, any I/O is completed. Just answer
		 * with an abort message.
		 */
		msg_reply(msg.m_sender, &msg);
		break;
	case FS_WRITE:		/* Write */
		if (check_gen(&msg, f)) {
			break;
		}
		par_write(&msg, f);
		break;
	case FS_STAT:		/* Get stat of file */
		if (check_gen(&msg, f)) {
			break;
		}
		par_stat(&msg, f);
		break;
	case FS_WSTAT:		/* Writes stats */
		if (check_gen(&msg, f)) {
			break;
		}
		par_wstat(&msg, f);
		break;
	default:		/* Unknown */
		msg_err(msg.m_sender, EINVAL);
		break;
	}
	goto loop;
}

/*
 * usage()
 *	Tell how to use
 */
static volatile void
usage(void)
{
	puts("usage: par <par0|par1|par2> <name>");
	exit(1);
}

int main(int argc, char *argv[])
{
	port_name n;
	int status;

	if (argc != 3) {
		usage();
	}

	if (!strcmp(argv[1], "par0") || (argv[1][0] == '0')) {
		status = par_init(&printer, 0);
	} else if (!strcmp(argv[1], "par1") || (argv[1][0] == '1')) {
		status = par_init(&printer, 1);
	} else if (!strcmp(argv[1], "par2") || (argv[1][0] == '2')) {
		status = par_init(&printer, 2);
	} else {
		usage();
	}
	if (status) {
		perror("can't init printer object");
		exit(1);
	}
	par_reset(&printer);

	/*
	 * Allocate handle->file hash table.  16 is just a guess
	 * as to what we'll have to handle.
	 */
	filehash = hash_alloc(16);
	if (filehash == 0) {
		perror("file hash");
		exit(1);
	}

	lp_port = msg_port((port_name)0, &n);
	if (namer_register(argv[2], n) < 0) {
		perror("Name registry");
		exit(1);
	}

	par_main();
}
