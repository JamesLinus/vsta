#include <pipe/pipe.h>
#ifdef DEBUG
#include <sys/ports.h>
#endif
#include <lib/hash.h>
#include <lib/llist.h>
#include <stdio.h>
#include <fcntl.h>
#include <std.h>
#include <namer/namer.h>

#define NCACHE (16)	/* Roughly, # clients */

extern void pipe_open(), pipe_read(), pipe_write(), pipe_abort(),
	pipe_stat(), pipe_close(), pipe_wstat();

static struct hash	/* Map of all active users */
	*filehash;
port_t rootport;	/* Port we receive contacts through */
struct llist		/* All files in filesystem */
	files;

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
	bzero(f, sizeof(struct file));

	/*
	 * Fill in fields
	 */
	f->f_nperm = nperms;
	bcopy(m->m_buf, &f->f_perms, nperms * sizeof(struct perm));

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
		f->f_file->p_refs += 1;
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
	pipe_close(f);
	free(f);
}

/*
 * pipe_main()
 *	Endless loop to receive and serve requests
 */
static void
pipe_main()
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
		perror("pipe: msg_receive");
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
		pipe_abort(&msg, f);
		break;
	case FS_OPEN:		/* Look up file from directory */
		if ((msg.m_nseg != 1) || !valid_fname(msg.m_buf,
				msg.m_buflen)) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		pipe_open(&msg, f);
		break;
	case FS_READ:		/* Read file */
		pipe_read(&msg, f);
		break;
	case FS_WRITE:		/* Write file */
		pipe_write(&msg, f, x);
		break;
	case FS_STAT:		/* Tell about file */
		pipe_stat(&msg, f);
		break;
	case FS_WSTAT:		/* Set stuff on file */
		pipe_wstat(&msg, f);
		break;
	default:		/* Unknown */
		msg_err(msg.m_sender, EINVAL);
		break;
	}
	goto loop;
}

main(int argc, char *argv[])
{
	port_name nm;

#ifdef DEBUG
	{
		port_t kbd, cons;

		kbd = msg_connect(PORT_KBD, ACC_READ);
		cons = msg_connect(PORT_CONS, ACC_WRITE);
		(void)__fd_alloc(kbd);
		(void)__fd_alloc(cons);
		(void)__fd_alloc(cons);
	}
#endif
	/*
	 * Set name for boot task
	 */
	if (argc == 0) {
		set_cmd("pipe");
	}

	/*
	 * Allocate data structures we'll need
	 */
        filehash = hash_alloc(NCACHE/4);
	if (filehash == 0) {
		perror("file hash");
		exit(1);
        }
	ll_init(&files);

	/*
	 * Set up port
	 */
	rootport = msg_port(0, &nm);
	if (rootport < 0) {
		perror("pipe: port");
		exit(1);
	}

	/*
	 * Register port name
	 */
	if (namer_register("fs/pipe", nm) < 0) {
		perror("pipe: name");
		exit(1);
	}

	/*
	 * Start serving requests for the filesystem
	 */
	pipe_main();
}
