/*
 * Filename:	main.c
 * Author:	Dave Hudson <dave@humbug.demon.co.uk>
 * Started:	12th March 1994
 * Last Update: 10th May 1994
 * Implemented:	GNU GCC 1.42 (VSTa v1.3.1)
 *
 * Description: Main message handling for the NVRAM device.  Also handle
 *		the initialisation of the device server and namer registration
 */
#include <fdl.h>
#include <hash.h>
#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <sys/assert.h>
#include <sys/fs.h>
#include <sys/namer.h>
#include <sys/perm.h>
#include <sys/ports.h>
#include <sys/syscall.h>
#include "nvram.h"

static struct hash *filehash;	/* Map session->context structure */

port_t nvram_port;		/* Port we receive contacts through */
port_name nvram_name;		/* And it's name */
uint nvram_accgen = 0;		/* Generation counter for access */

struct prot nvram_prot = {	/* Protection for the nvram starts */
	1,			/* as access for all.  Sys can change */
	ACC_READ|ACC_WRITE,	/* this however */
	{1},
	{ACC_CHMOD}
};

extern int valid_fname();

/*
 * nvram_new_client()
 *	Create new per-connect structure
 */
static void
nvram_new_client(struct msg *m)
{
	struct file *f;
	struct perm *perms;
	int uperms, nperms, desired;

	/*
	 * See if they're OK to access
	 */
	perms = (struct perm *)m->m_buf;
	nperms = (m->m_buflen) / sizeof(struct perm);
	uperms = perm_calc(perms, nperms, &nvram_prot);
	desired = m->m_arg & (ACC_WRITE | ACC_READ | ACC_CHMOD);
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
	f->f_gen = nvram_accgen;
	f->f_flags = uperms;
	f->f_pos = 0;
	f->f_node = ROOT_INO;

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
 * nvram_dup_client()
 *	Duplicate current file access onto new session
 */
static void
nvram_dup_client(struct msg *m, struct file *fold)
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
 * nvram_dead_client()
 *	Someone has gone away.  Free their info.
 */
static void
nvram_dead_client(struct msg *m, struct file *f)
{
	(void)hash_delete(filehash, m->m_sender);
	free(f);
}

/*
 * nvram_check_gen()
 *	Check access generation
 */
static int
nvram_check_gen(struct msg *m, struct file *f)
{
	if (f->f_gen != nvram_accgen) {
		msg_err(m->m_sender, EIO);
		return 1;
	}
	return(0);
}

/*
 * nvram_main()
 *	Endless loop to receive and serve requests
 */
static void
nvram_main()
{
	struct msg msg;
	int x;
	struct file *f;

loop:
	/*
	 * Receive a message, log an error and then keep going
	 */
	x = msg_receive(nvram_port, &msg);
	if (x < 0) {
		syslog(LOG_ERR, "msg_receive");
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
		nvram_new_client(&msg);
		break;

	case M_DISCONNECT:		/* Client done */
		nvram_dead_client(&msg, f);
		break;

	case M_DUP:			/* File handle dup during exec() */
		nvram_dup_client(&msg, f);
		break;

	case M_ABORT:			/* Aborted operation */
		/*
		 * We're synchronous, so presumably the operation is
		 * done and this abort is old news
		 */
		msg_reply(msg.m_sender, &msg);
		break;

	case FS_SEEK:			/* Set position */
		if (!f || (msg.m_arg < 0)) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		f->f_pos = msg.m_arg;
		msg.m_arg = msg.m_arg1 = msg.m_nseg = 0;
		msg_reply(msg.m_sender, &msg);
		break;

	case FS_ABSREAD:		/* Set position, then read */
	case FS_ABSWRITE:		/* Set position, then write */
		if (!f || (msg.m_arg1 < 0)) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		f->f_pos = msg.m_arg1;
		msg.m_op = (msg.m_op == FS_ABSREAD) ? FS_READ : FS_WRITE;

		/*
		 * Now fall into FS_READ/FS_WRITE 
		 */

	case FS_READ:			/* Read file */
	case FS_WRITE:		/* Write file */
		if (nvram_check_gen(&msg, f)) {
			break;
		}
		nvram_readwrite(&msg, f);
		break;

	case FS_STAT:			/* Get stat of file */
		if (nvram_check_gen(&msg, f)) {
			break;
		}
		nvram_stat(&msg, f);
		break;

	case FS_WSTAT:		/* Writes stats */
		if (nvram_check_gen(&msg, f)) {
			break;
		}
		nvram_wstat(&msg, f);
		break;

	case FS_OPEN:			/* Move from dir down into drive */
		if (!valid_fname(msg.m_buf, x)) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		nvram_open(&msg, f);
		break;

	default:			/* Unknown */
		msg_err(msg.m_sender, EINVAL);
		break;
	}
	goto loop;
}

/*
 * main()
 *	Startup of the "nvram" server
 */
int
main(void)
{
	/*
	 * Initialize syslog
	 */
	openlog("nvram", LOG_PID, LOG_DAEMON);

	/*
	 * Allocate handle->file hash table.  16 is just a guess
	 * as to what we'll have to handle.
	 */
	filehash = hash_alloc(16);
	if (filehash == 0) {
		syslog(LOG_ERR, "unable to allocate file hash");
		exit(1);
	}

	/*
	 * Enable I/O for the NVRAM index and data ports
	 */
	if (enable_io(RTCSEL, RTCDATA) < 0) {
		syslog(LOG_ERR, "unable to get I/O permissions");
		exit(1);
	}

	/*
	 * Initialise the server parameters for the NVRAM - basically find out
	 * how many bytes of data we can support
	 */
	nvram_init();

	/*
	 * Get a port for the NVRAM server
	 */
	nvram_port = msg_port((port_name)0, &nvram_name);

	/*
	 * Register the device name with the namer
	 */
	if (namer_register("srv/nvram", nvram_name) < 0) {
		syslog(LOG_ERR, "can't register name");
		exit(1);
	}

	/*
	 * Start serving requests for the filesystem
	 */
	nvram_main();
	return(0);
}
