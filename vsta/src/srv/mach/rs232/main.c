/*
 * main.c
 *	Main message handling
 */
#include <sys/perm.h>
#include <sys/fs.h>
#include <sys/assert.h>
#include <sys/param.h>
#include <hash.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include "rs232.h"

static struct hash *filehash;	/* Map session->context structure */
char rs232_name[NAMESZ] = "";	/* Port namer name for this server */
port_t rs232port;		/* Port we receive contacts through */
uint accgen = 0;		/* Generation counter for access */
int irq;			/* IRQ number for rs232 */
int iobase;			/* Corresponding I/O base */
int baud;			/* Baud rate */
int databits;			/* Number of data bits in use */
int stopbits;			/* Number of stop bits in use */
int parity;			/* Current parity setting */
uchar dsr;			/* Status of DSR control line */
uchar dtr;			/* Status of DTR control line */
uchar cts;			/* Status of CTS control line */
uchar rts;			/* Status of RTS control line */
uchar dcd;			/* Status of DCD (RLSD) control line */
uchar ri;			/* Status of RI control line */
char rs232_sysmsg[9 + NAMESZ];	/* Syslog message prefix */

/*
 * Protection for port; starts out with access for all.  sys can
 * change the protection label.
 */
struct prot rs232_prot = {
	1,
	ACC_READ|ACC_WRITE,
	{1},
	{ACC_CHMOD}
};

/*
 * Default base port and IRQ settings for the standard com1-4 available on
 * a PC - any others require explicit details passing from the command line
 */
struct rs232_defaults {
	int r_baseio;		/* Base I/O address */
	int r_irqnum;		/* Standard IRQ line */
} rs232_iodefs[RS232_STDSUPPORT] = {
	{0x3f8, 4},		/* COM1 */
	{0x2f8, 3},		/* COM2 */
	{0x3e8, 5},		/* COM3 */
	{0x2e8, 5}		/* COM4 */
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
	uperms = perm_calc(perms, nperms, &rs232_prot);
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
 * rs232_main()
 *	Endless loop to receive and serve requests
 */
static void
rs232_main()
{
	struct msg msg;
	int x;
	struct file *f;
loop:
	/*
	 * Receive a message, log an error and then keep going
	 */
	x = msg_receive(rs232port, &msg);
	if (x < 0) {
		syslog(LOG_ERR, "%s msg_receive", rs232_sysmsg);
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
		 * Hunt down any active operation for this
		 * handle, and abort it.  Then answer with
		 * an abort acknowledge.
		 */
		if (f->f_count) {
			abort_io(f);
		}
		msg_reply(msg.m_sender, &msg);
		break;
	case M_ISR:		/* Interrupt */
		ASSERT_DEBUG(f == 0, "rs232: session from kernel");
		rs232_isr(&msg);
		break;
	case FS_READ:		/* Read file */
		if (check_gen(&msg, f)) {
			break;
		}
		rs232_read(&msg, f);
		break;
	case FS_WRITE:		/* Write */
		if (check_gen(&msg, f)) {
			break;
		}
		rs232_write(&msg, f);
		break;
	case FS_STAT:		/* Get stat of file */
		if (check_gen(&msg, f)) {
			break;
		}
		rs232_stat(&msg, f);
		break;
	case FS_WSTAT:		/* Writes stats */
		if (check_gen(&msg, f)) {
			break;
		}
		rs232_wstat(&msg, f);
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
static void
usage(void)
{
	fprintf(stderr, "Usage is: rs232 <com[1-4] | userdef> " \
		"[opts=args] ...\n\n");
	fprintf(stderr, "          options: baseio=<I/O-base-address>\n" \
			"                   irq=<IRQ-number>\n" \
			"                   namer=<namer-entry>\n\n");
	exit(1);
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
	 * Com port should be single argument.  We accept either com[1-4], eg
	 * com3, or userdef meaning that explicit parameters will be specified
	 */
	if (argc < 2) {
		usage();
	}
	if (!strncmp(argv[1], "com", 3)) {
		int p;

		/*
		 * We're using a standard com definition - find the lookup
		 * table reference details
		 */		
		p = argv[1][3] - '1';
		if (p < 0 || p >= RS232_STDSUPPORT) {
			usage();
		}
		iobase = rs232_iodefs[p].r_baseio;
		irq = rs232_iodefs[p].r_irqnum;
		sprintf(rs232_name, "tty/tty0%d", p + 1);
	} else if (!strcmp(argv[1], "userdef")) {
		iobase = 0;
		irq = 0;
	} else {
		usage();
	}

	/*
	 * Start processing the option parameters
	 */
	i = 2;
	while (i < argc) {
		/*
		 * Select a new IRQ line
		 */
		if (!strncmp(argv[i], "irq=", 4)) {
			/*
			 * Select a new IRQ line
			 */
			irq = (int)strtol(&argv[i][4], &check, 0);
			if (check == &argv[i][4] || *check != '\0') {
				fprintf(stderr, "rs232: invalid IRQ setting " \
					"'%s' - aborting\n", argv[i]);
				exit(1);
			}
		} else if (!strncmp(argv[i], "baseio=", 7)) {
			/*
			 * Select a new base I/O port address
			 */
			iobase = (int)strtol(&argv[i][7], &check, 0);
			if (check == &argv[i][7] || *check != '\0') {
				fprintf(stderr, "rs232: invalid I/O address " \
					"'%s' - aborting\n", argv[i]);
				exit(1);
			}
		} else if (!strncmp(argv[i], "namer=", 6)) {
			/*
			 * Select a new namer entry
			 */
			if ((strlen(&argv[i][6]) == 0)
			    || (strlen(&argv[i][6]) >= NAMESZ)) {
				fprintf(stderr, "rs232: invalid name '%s' " \
					"- aborting\n", &argv[i][6]);
				exit(1);
			}
			strcpy(rs232_name, &argv[i][6]);
		} else {
			fprintf(stderr,
				"rs232: unknown option '%s' - aborting\n",
				argv[i]);
			exit(1);
		}
		i++;
	}

	/*
	 * Check that after all of the messing about we have a valid set
	 * of parameters - report failures if we don't
	 */
	if (iobase == 0) {
		fprintf(stderr,
			"rs232: no I/O base address specified - aborting\n");
		exit(1);
	}
	if (irq == 0) {
		fprintf(stderr,
			"rs232: no IRQ line specified - aborting\n");
		exit(1);
	}
	if (rs232_name[0] == '\0') {
		fprintf(stderr,
			"rs232: no namer entry specified - aborting\n");
		exit(1);
	}

	sprintf(rs232_sysmsg, "rs232 (%s):", rs232_name);
}

/*
 * main()
 *	Startup of the keyboard server
 */
int
main(int argc, char **argv)
{
	port_name n;

	/*
	 * Work out which I/O addresses to use, etc
	 */
	parse_options(argc, argv);

	/*
	 * Allocate handle->file hash table.  16 is just a guess
	 * as to what we'll have to handle.
	 */
        filehash = hash_alloc(16);
	if (filehash == 0) {
		syslog(LOG_ERR, "%s file hash not allocated", rs232_sysmsg);
		exit(1);
        }

	/*
	 * Init our data structures
	 */
	rs232_init();

	/*
	 * Enable I/O for the needed range
	 */
	if (enable_io(iobase, RS232_HIGH(iobase)) < 0) {
		syslog(LOG_ERR, "%s I/O permissions", rs232_sysmsg);
		exit(1);
	}

	/*
	 * Get a port for the keyboard
	 */
	rs232port = msg_port((port_name)0, &n);
	if (namer_register(rs232_name, n) < 0) {
		syslog(LOG_ERR, "%s namer registry of '%s'",
			rs232_sysmsg, rs232_name);
		exit(1);
	}

	/*
	 * Tell system about our I/O vector
	 */
	if (enable_isr(rs232port, irq)) {
		syslog(LOG_ERR, "%s IRQ %d allocation", rs232_sysmsg, irq);
		exit(1);
	}

	/*
	 * Default configuration and turn on interrupts
	 */
	rs232_baud(9600);
	rs232_databits(8);
	rs232_stopbits(1);
	rs232_parity(PARITY_NONE);
	rs232_enable();

	syslog(LOG_INFO, "%s established (IRQ %d, I/O base 0x%x)",
		rs232_sysmsg, irq, iobase);

	/*
	 * Start serving requests for the filesystem
	 */
	rs232_main();
}
