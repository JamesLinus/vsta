/*
 * main.c
 *	Main message handling
 */
#include <sys/perm.h>
#include <sys/types.h>
#include <sys/fs.h>
#include <sys/ports.h>
#include <lib/hash.h>
#include <cons/cons.h>
#include <stdio.h>

extern void write_string(), *malloc(), cons_stat(), cons_wstat();
extern char *strerror();

struct hash *filehash;	/* Map session->context structure */

port_t consport;	/* Port we receive contacts through */
uint accgen = 0;	/* Generation counter for access */

/*
 * Protection for console; starts out with access for all.  sys can
 * change the protection label.
 */
struct prot cons_prot = {
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
	uperms = perm_calc(perms, nperms, &cons_prot);
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
 *	See if the console is still accessible to them
 */
static
check_gen(struct msg *m, struct file *f)
{
	if (f->f_gen != accgen) {
		msg_err(m->m_sender, EIO);
		return(1);
	}
	return(0);
}

/*
 * screen_main()
 *	Endless loop to receive and serve requests
 */
static void
screen_main()
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
	x = msg_receive(consport, &msg);
	if (x < 0) {
		perror("bfs: msg_receive");
		goto loop;
	}

	/*
	 * If we've received more than a single buffer of data, pull it in
	 * to a dynamic buffer.
	 */
	if (msg.m_nseg > 1) {
		buf2 = malloc(x);
		if (buf2 == 0) {
			msg_err(msg.m_sender, E2BIG);
			goto loop;
		}
		(void)seg_copyin(msg.m_seg, msg.m_nseg, buf2, x);
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
	case FS_WRITE:		/* Write file */
		if (check_gen(&msg, f)) {
			break;
		}
		if (msg.m_buflen > 0) {
			write_string(msg.m_buf, msg.m_buflen);
		}
		msg.m_buflen = msg.m_arg1 = msg.m_nseg = 0;
		msg.m_arg = x;
		msg_reply(msg.m_sender, &msg);
		break;
	case FS_STAT:		/* Stat of file */
		if (check_gen(&msg, f)) {
			break;
		}
		cons_stat(&msg, f);
		break;
	case FS_WSTAT:		/* Write selected stat fields */
		if (check_gen(&msg, f)) {
			break;
		}
		cons_wstat(&msg, f);
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
 * main()
 *	Startup of the screen server
 */
main()
{
	extern void init_screen();

	/*
	 * Allocate handle->file hash table.  16 is just a guess
	 * as to what we'll have to handle.
	 */
        filehash = hash_alloc(16);
	if (filehash == 0) {
		perror("file hash");
		exit(1);
        }

	/*
	 * Turn on our I/O access
	 */
	if (enable_io(CONS_LOW, CONS_HIGH) < 0) {
		fprintf(stderr, "CONS: can't do I/O operations\n");
		exit(1);
	}

	/*
	 * Get a port for the console
	 */
	consport = msg_port(PORT_CONS, 0);

	/*
	 * Let screen mapping get initialized
	 */
	init_screen();

	/*
	 * Start serving requests for the filesystem
	 */
	screen_main();
}
