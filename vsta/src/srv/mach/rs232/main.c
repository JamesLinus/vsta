/*
 * main.c
 *	Main message handling
 */
#include <sys/perm.h>
#include <sys/fs.h>
#include <sys/assert.h>
#include <sys/param.h>
#include <sys/syscall.h>
#include <sys/namer.h>
#include <hash.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include "rs232.h"

static struct hash *filehash;	/* Map session->context structure */
char rs232_name[NAMESZ] = "";	/* Port namer name for this server */
port_t rs232port;		/* Port we receive contacts through */
port_name rs232port_name;	/* What our port is known as externally */
uint accgen = 0;		/* Generation counter for access */
int uart = UART_UNKNOWN;	/* What type of UART do we have? */
static int test_uart = 1;	/* Are we going to test the UART? */
int irq;			/* IRQ number for rs232 */
int iobase;			/* Corresponding I/O base */
int rx_fifo_threshold;		/* Point at which UART rx FIFO triggers */
int tx_fifo_threshold;		/* Point at which UART tx FIFO triggers */
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
int kdb;			/* ^Z enters kernel debugger? */

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
 * Names of the UARTs supported
 */
char *uart_names[] = {
	"8250", "16450", "16550", "16550A", NULL
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
	sc_init(&f->f_selfs);
	f->f_sentry = NULL;

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
	if (f->f_sentry) {
		ll_delete(f->f_sentry);
		nsel -= 1;
	}
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
		 * Hunt down any active operation for this
		 * handle, and abort it.  Then answer with
		 * an abort acknowledge.
		 */
		if (f->f_count) {
			abort_io(f);
		}
		msg_reply(msg.m_sender, &msg);
		break;

	case FS_READ:		/* Read file */
		if (check_gen(&msg, f)) {
			break;
		}
		f->f_selfs.sc_iocount += 1;
		f->f_selfs.sc_needsel = 0;
		rs232_read(&msg, f);
		break;

	case FS_WRITE:		/* Write */
		if (check_gen(&msg, f)) {
			break;
		}
		f->f_selfs.sc_iocount += 1;
		f->f_selfs.sc_needsel = 0;
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
	case RS232_HELPER:	/* Run helper code */
		msg.m_arg = msg.m_nseg = 0;
		msg_reply(msg.m_sender, &msg);
		run_helper();
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
	fprintf(stderr, "Usage is: rs232 <com[1-4] | userdef> [-k] "
		"[opts=args] ...\n\n");
	fprintf(stderr, "options: baseio=<I/O-base-address>\n"
			"	irq=<IRQ-number>\n"
			"	namer=<namer-entry>\n"
			"	uart=<uart-type>\n"
			"	nouarttest\n"
			"	-k (^Z enters kernel debugger)\n"
			);
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
	for (i = 2; i < argc; ++i) {
		char *p;

		p = argv[i];

		/*
		 * Command line switches
		 */
		if (p[0] == '-') {
			switch (p[1]) {
			case 'k':
				kdb = 1;
				break;
			default:
				usage();
			}
			continue;
		}

		/*
		 * Otherwise <var>=<value> constructs
		 */
		if (!strchr(p, '=')) {
			/*
			 * Compatibility: arg can be portname
			 */
			strcpy(rs232_name, p);
		} else if (!strncmp(p, "irq=", 4)) {
			/*
			 * Select a new IRQ line
			 */
			irq = (int)strtol(&p[4], &check, 0);
			if (check == &p[4] || *check != '\0') {
				fprintf(stderr, "rs232: invalid IRQ setting " \
					"'%s' - aborting\n", p);
				exit(1);
			}
		} else if (!strncmp(p, "baseio=", 7)) {
			/*
			 * Select a new base I/O port address
			 */
			iobase = (int)strtol(&p[7], &check, 0);
			if (check == &p[7] || *check != '\0') {
				fprintf(stderr, "rs232: invalid I/O address " \
					"'%s' - aborting\n", p);
				exit(1);
			}
		} else if (!strncmp(p, "namer=", 6)) {
			/*
			 * Select a new namer entry
			 */
			if ((strlen(&p[6]) == 0)
			    || (strlen(&p[6]) >= NAMESZ)) {
				fprintf(stderr, "rs232: invalid name '%s' " \
					"- aborting\n", &p[6]);
				exit(1);
			}
			strcpy(rs232_name, &p[6]);
		} else if (!strncmp(p, "notest=", 7)) {
			/*
			 * Don't attempt to test the UART
			 */
			test_uart = 0;
		} else if (!strncmp(p, "uart=", 5)) {
			/*
			 * Force the uart type
			 */
			for(uart = 0; uart_names[uart]; uart++) {
				if (!strcmp(&p[5], uart_names[uart])) {
					break;
				}
			}
			if (!uart_names[uart]) {
				fprintf(stderr, "rs232: invalid UART type " \
					"'%s' - aborting\n", p);
				exit(1);
			}
 		} else {
 			fprintf(stderr,
 				"rs232: unknown option '%s' - aborting\n",
 				p);
 			exit(1);
		}
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
}

/*
 * main()
 *	Startup of the rs232 server
 */
int
main(int argc, char **argv)
{
	/*
	 * Initialise syslog
	 */
	openlog("rs232", LOG_PID, LOG_DAEMON);	

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
		syslog(LOG_ERR, "file hash not allocated");
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
		syslog(LOG_ERR, "I/O permissions");
		exit(1);
	}

	/*
	 * Determine the type of UART we have, and test it's really there
	 */
	if (rs232_iduart(test_uart)) {
		syslog(LOG_ERR, "unable to find UART at 0x%x", iobase);
		exit(1);
	}

	/*
	 * Get a port for the rs232
	 */
	rs232port = msg_port((port_name)0, &rs232port_name);
	if (namer_register(rs232_name, rs232port_name) < 0) {
		syslog(LOG_ERR, "namer registry of '%s'", rs232_name);
		exit(1);
	}

	/*
	 * Default configuration and turn on interrupts
	 */
	rs232_baud(9600);
	rs232_setrxfifo(0);
	rs232_databits(8);
	rs232_stopbits(1);
	rs232_parity(PARITY_NONE);
	rs232_enable(irq);

	syslog(LOG_INFO, "%s on IRQ %d, I/O base 0x%x",
		uart_names[uart], irq, iobase);

	/*
	 * Start serving requests for the filesystem
	 */
	rs232_main();
	return(0);
}
