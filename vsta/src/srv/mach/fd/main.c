/*
 * main.c
 *	Main message handling
 */
#include <sys/msg.h>
#include <sys/perm.h>
#include <sys/fs.h>
#include <sys/namer.h>
#include <alloc.h>
#include <hash.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/assert.h>
#include <sys/syscall.h>
#include <mach/dma.h>
#include "fd.h"


static struct hash *filehash;	/* Map session->context structure */
int fdc_type = FDC_HAVE_UNKNOWN;
				/* Type of FDC we have */
int fd_baseio = FD_BASEIO;	/* Base I/O address */
int fd_irq = FD_IRQ;		/* Interrupt request line */
int fd_dma = FD_DRQ;		/* DMA channel */
port_t fdport;			/* Port we receive contacts through */
port_name fdport_name;		/* ... its name */
char fd_name[NAMESZ] = "disk/fd";
				/* Port namer name for this server */
char fdc_names[][FDC_NAMEMAX];	/* Names of the FDC types */


/*
 * Default protection for floppy drives:  anybody can read/write, sys
 * can chmod them.
 */
struct prot fd_prot = {
	1,
	ACC_READ | ACC_WRITE,
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
	bzero(f, sizeof(*f));
	f->f_sender = m->m_sender;
	f->f_flags = uperms;
	f->f_slot = ROOTDIR;

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
	ASSERT(fold->f_list == 0, "fd dup_client: busy");
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
		fd_time();
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
 * parse_options()
 *	Parse the initial command line options and establish the base I/O and
 *	IRQ settings.  Aborts if there are any missing or invalid combinations
 */
static void
parse_options(int argc, char **argv)
{
	int i;
	char *check;

	/*
	 * Start processing the option parameters
	 */
	i = 1;
	while (i < argc) {
		if (!strncmp(argv[i], "baseio=", 7)) {
			/*
			 * Select a new base I/O port address
			 */
			fd_baseio = (int)strtol(&argv[i][7], &check, 0);
			if (check == &argv[i][7] || *check != '\0') {
				fprintf(stderr, "fd: invalid I/O address " \
					"'%s' - aborting\n", argv[i]);
				exit(1);
			}
		} else if (!strncmp(argv[i], "dma=", 4)) {
			/*
			 * Select a new DMA channel
			 */
			fd_dma = (int)strtol(&argv[i][4], &check, 0);
			if (check == &argv[i][4] || *check != '\0') {
				fprintf(stderr, "fd: invalid DMA setting " \
					"'%s' - aborting\n", argv[i]);
				exit(1);
			}
		} else if (!strncmp(argv[i], "fdc=", 4)) {
			/*
			 * Force the FDC type
			 */
			for(fdc_type = 0; fdc_names[fdc_type][0];
			    fdc_type++) {
				if (!strcmp(&argv[i][4],
					    fdc_names[fdc_type])) {
					break;
				}
			}
			if (!fdc_names[fdc_type][0]) {
				fprintf(stderr, "fd: invalid FDC type " \
					"'%s' - aborting\n", argv[i]);
				exit(1);
			}
		} else if (!strncmp(argv[i], "irq=", 4)) {
			/*
			 * Select a new IRQ line
			 */
			fd_irq = (int)strtol(&argv[i][4], &check, 0);
			if (check == &argv[i][4] || *check != '\0') {
				fprintf(stderr, "fd: invalid IRQ setting " \
					"'%s' - aborting\n", argv[i]);
				exit(1);
			}
		} else if (!strncmp(argv[i], "namer=", 6)) {
			/*
			 * Select a new namer entry
			 */
			if ((strlen(&argv[i][6]) == 0)
			    || (strlen(&argv[i][6]) >= NAMESZ)) {
				fprintf(stderr, "fd: invalid name '%s' " \
					"- aborting\n", &argv[i][6]);
				exit(1);
			}
			strcpy(fd_name, &argv[i][6]);
		} else {
			fprintf(stderr,
				"fd: unknown option '%s' - aborting\n",
				argv[i]);
			exit(1);
		}
		i++;
	}
}


/*
 * main()
 *	Startup of the floppy server
 */
void
main(int argc, char **argv)
{
	/*
	 * Initialise syslog
	 */
	openlog("fd", LOG_PID, LOG_DAEMON);

	/*
	 * Work out which I/O addresses to use, etc
	 */
	parse_options(argc, argv);

	/*
	 * Allocate handle->file hash table.  8 is just a guess
	 * as to what we'll have to handle.
	 */
        filehash = hash_alloc(8);
	if (filehash == 0) {
		syslog(LOG_ERR, "file hash not allocated");
		exit(1);
        }

	/*
	 * We still have to program our own DMA.  This gives the
	 * system just enough information to shut down the channel
	 * on process abort.  It can also catch accidentally launching
	 * two floppy tasks.
	 */
	if (enable_dma(fd_dma) < 0) {
		syslog(LOG_ERR, "DMA %d not enabled", fd_dma);
		exit(1);
	}

	/*
	 * Enable I/O for the required range
	 */
	if (enable_io(MIN(fd_baseio + FD_LOW, DMA_LOW),
		      MAX(fd_baseio + FD_HIGH, DMA_HIGH)) < 0) {
		syslog(LOG_ERR, "I/O permissions not granted");
		exit(1);
	}

	/*
	 * Get a port for the floppy task
	 */
	fdport = msg_port((port_name)0, &fdport_name);

	/*
	 * Register as floppy drives
	 */
	if (namer_register(fd_name, fdport_name) < 0) {
		syslog(LOG_ERR, "can't register name '%s'", fd_name);
		exit(1);
	}

	/*
	 * Tell system about our I/O vector
	 */
	if (enable_isr(fdport, fd_irq)) {
		syslog(LOG_ERR, "couldn't get IRQ %d", fd_irq);
		exit(1);
	}
	
	/*
	 * Init our data structures.  We must enable_dma() first, because
	 * init wires a bounce buffer, and you have to be a DMA-type
	 * server to wire memory.  We also need the IRQ hooked as some FDCs
	 * can't handle versioning commands and respond with an invalid
	 * command interrupt.
	 */
	fd_init();

	/*
	 * Start serving requests for the filesystem
	 */
	fd_main();
}
