/*
 * main.c
 *	Main interface to port name server
 *
 * The name space is a tree, with each node possessing a name and
 * protection label.  If the node is internal, it can contain zero
 * or more entries.  If it is a leaf, it will hold a global port
 * name.
 */
#define _NAMER_H_INTERNAL
#include <sys/perm.h>
#include <sys/param.h>
#include <namer/namer.h>
#include <lib/hash.h>
#include <sys/fs.h>
#include <sys/ports.h>
#include <sys/types.h>
#include <stdio.h>

extern void *malloc(), namer_open(), namer_read(), namer_write(),
	namer_remove(), namer_stat(), namer_wstat();
extern char *strerror();

#define BUFSIZE (NAMESZ*3)	/* That should be enough */

port_t namerport;	/* Port we receive contacts through */

static struct hash *filehash;
static struct node rootnode;

/*
 * Default protection for system-defined names; anybody can read,
 * only sys can write.
 */
static struct prot namer_prot = {
	1,
	ACC_READ,
	{1},
	{ACC_WRITE|ACC_CHMOD}
};

/*
 * namer_seek()
 *	Set file position
 */
static void
namer_seek(struct msg *m, struct file *f)
{
	struct node *n = f->f_node;

	if (m->m_arg < 0) {
		msg_err(m->m_sender, EINVAL);
		return;
	}
	f->f_pos = m->m_arg;
	m->m_buflen = m->m_arg = m->m_arg1 = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}

/*
 * access()
 *	See if user is compatible with desired access for object
 */
access(struct file *f, int mode, struct prot *prot)
{
	int uperms, desired;

	uperms = perm_calc(f->f_perms, f->f_nperm, prot);
	desired = mode & (ACC_WRITE|ACC_READ|ACC_CHMOD);
	if ((uperms & desired) != desired)
		return(1);
	f->f_mode = uperms;
	return(0);
}

/*
 * new_client()
 *	Create new per-connect structure
 */
static void
new_client(struct msg *m)
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
	 *
	 * As soon as possible, check access modes and bomb
	 * if they're bad.
	 */
	f->f_nperm = m->m_buflen/sizeof(struct perm);
	bcopy(m->m_buf, &f->f_perms, f->f_nperm*sizeof(struct perm));
	if (access(f, m->m_arg, &namer_prot)) {
		msg_err(m->m_sender, EPERM);
		free(f);
		return;
	}
	/* f->f_mode set by access() */
	f->f_pos = 0L;

	/*
	 * Hash under the sender's handle
	 */
        if (hash_insert(filehash, m->m_sender, f)) {
		free(f);
		msg_err(m->m_sender, ENOMEM);
		return;
	}

	/*
	 * Start out at root node
	 */
	f->f_node = &rootnode;
	rootnode.n_refs += 1;

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
	 * Bump ref now that the has_insert worked
	 */
	f->f_node->n_refs += 1;

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
	f->f_node->n_refs -= 1;
	free(f);
}

/*
 * namer_main()
 *	Endless loop to receive and serve requests
 */
static void
namer_main()
{
	struct msg msg;
	int x;
	struct file *f;

loop:
	/*
	 * Receive a message, log an error and then keep going
	 */
	x = msg_receive(namerport, &msg);
	if (x < 0) {
		perror("namer: msg_receive");
		goto loop;
	}

	/*
	 * All requests should fit in one buffer
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
		 * We're synchronous, so presumably the operation
		 * is all done and this abort is old news.
		 */
		msg_reply(msg.m_sender, &msg);
		break;
	case FS_OPEN:		/* Look up file from directory */
		if (!valid_fname(msg.m_buf, x)) {
			msg_err(msg.m_sender, ESRCH);
			break;
		}
		namer_open(&msg, f);
		break;
	case FS_READ:		/* Read file */
		namer_read(&msg, f);
		break;
	case FS_WRITE:		/* Write file */
		namer_write(&msg, f);
		break;
	case FS_SEEK:		/* Set new file position */
		namer_seek(&msg, f);
		break;
	case FS_REMOVE:		/* Get rid of a file */
		namer_remove(&msg, f);
		break;
	case FS_STAT:		/* Stat node */
		namer_stat(&msg, f);
		break;
	case FS_WSTAT:		/* Write stat info */
		namer_wstat(&msg, f);
		break;
	default:		/* Unknown */
		msg_err(msg.m_sender, EINVAL);
		break;
	}
	goto loop;
}

/*
 * main()
 *	Startup of system namer
 */
main()
{
	port_t port;
	port_name blkname;
	struct msg msg;
	int chan, fd, x;

	/*
	 * Allocate data structures we'll need
	 */
        filehash = hash_alloc(16);
	if (filehash == 0) {
		perror("file hash");
		exit(1);
        }

	/*
	 * Set up root node
	 */
	rootnode.n_prot = namer_prot;
	strcpy(rootnode.n_name, "/");
	rootnode.n_internal = 1;
	ll_init(&rootnode.n_elems);
	rootnode.n_refs = 1;	/* Always at least 1 */
	rootnode.n_owner = 0;

	/*
	 * Block device looks good.  Last check is that we can register
	 * with the given name.
	 */
	namerport = msg_port(PORT_NAMER, 0);
	if (x < 0) {
		fprintf(stderr, "NAMER: can't register name\n");
		exit(1);
	}

#ifdef DEBUG
	/*
	 * XXX hack so printf will show up on console
	 */
	port = msg_connect(PORT_KBD, ACC_READ);
	__fd_alloc(port);
	port = msg_connect(PORT_CONS, ACC_WRITE);
	__fd_alloc(port);
	__fd_alloc(port);
#endif

	/*
	 * Start serving requests for the filesystem
	 */
	namer_main();
}
