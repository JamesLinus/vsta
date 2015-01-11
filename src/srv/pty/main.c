/*
 * main.c
 *	Main processing loop
 */
#include <hash.h>
#include <stdio.h>
#include <fcntl.h>
#include <std.h>
#include <sys/namer.h>
#include <sys/assert.h>
#include <syslog.h>
#include <server.h>
#include "pty.h"

#define NCACHE (16)	/* Roughly, # clients */

static struct hash	/* Map of all active users */
	*filehash;
static port_t rootport;	/* Port we receive contacts through */

/*
 * Our PTY's
 */
struct pty ptys[NPTY];
static const char pty_chars[] = "0123456789abcdefghijklmnopqrstuvwxyz";
char *ptydir;
int ptydirlen;

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
	sc_init(&f->f_selfs);

	/*
	 * Fill in fields
	 */
	f->f_nperm = nperms;
	bcopy(m->m_buf, &f->f_perms, nperms * sizeof(struct perm));
	f->f_perm = ACC_READ | ACC_WRITE;

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
	struct pty *pty;

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
	bcopy(fold, f, sizeof(*f));
	sc_init(&f->f_selfs);
	f->f_sentry = NULL;

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
	if ((pty = f->f_file)) {
		if (f->f_master) {
			pty->p_nmaster += 1;
		} else {
			pty->p_nslave += 1;
		}
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
	struct pty *pty = f->f_file;

	(void)hash_delete(filehash, m->m_sender);
	if (pty) {
		if (f->f_master) {
			pty->p_nmaster -= 1;
		} else {
			pty->p_nslave -= 1;
		}
	}
	if (f->f_sentry) {
		ll_delete(f->f_sentry);
	}
	pty_close(f);
	free(f);
}

/*
 * pty_main()
 *	Endless loop to receive and serve requests
 */
static void
pty_main()
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
		pty_abort(&msg, f);
		break;

	case FS_OPEN:		/* Look up file from directory */
		if ((msg.m_nseg != 1) || !valid_fname(msg.m_buf,
				msg.m_buflen)) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		pty_open(&msg, f);
		break;

	case FS_READ:		/* Read file */
		f->f_selfs.sc_iocount += 1;
		f->f_selfs.sc_needsel = 0;
		pty_read(&msg, f);
		break;

	case FS_WRITE:		/* Write file */
		f->f_selfs.sc_iocount += 1;
		f->f_selfs.sc_needsel = 0;
		pty_write(&msg, f);
		break;

	case FS_STAT:		/* Tell about file */
		pty_stat(&msg, f);
		break;

	case FS_WSTAT:		/* Set stuff on file */
		pty_wstat(&msg, f);
		break;
	default:		/* Unknown */
		msg_err(msg.m_sender, EINVAL);
		break;
	}
	goto loop;
}

int
main(void)
{
	port_name nm;
	int x;

	/*
	 * Initialize syslog
	 */
	openlog("pty", LOG_PID, LOG_DAEMON);

	/*
	 * Allocate data structures we'll need
	 */
        filehash = hash_alloc(NCACHE/4);
	if (filehash == 0) {
		syslog(LOG_ERR, "file hash not allocated");
		exit(1);
        }

	/*
	 * Pre-calculate the "directory" of PTY entries.
	 */
	ptydir = malloc(6 * NPTY * 2);	/* "tty, ptyX\n\0" for each entry */
	ptydir[0] = '\0';
	for (x = 0; x < NPTY; ++x) {
		char entry[8];

		sprintf(entry, "pty%c\n", pty_chars[x]);
		strcat(ptydir, entry);
		sprintf(entry, "tty%c\n", pty_chars[x]);
		strcat(ptydir, entry);
	}
	ptydirlen = strlen(ptydir);

	/*
	 * Set up port
	 */
	rootport = msg_port(0, &nm);
	if (rootport < 0) {
		syslog(LOG_ERR, "can't establish port");
		exit(1);
	}

	/*
	 * Register port name
	 */
	if (namer_register("fs/pty", nm) < 0) {
		syslog(LOG_ERR, "unable to register name");
		exit(1);
	}

	syslog(LOG_INFO, "pty filesystem started");

	/*
	 * Start serving requests for the filesystem
	 */
	pty_main();
	return(0);
}
