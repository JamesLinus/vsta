/*
 * ibmrs232.c
 *    Polling driver for the IBM serial mouse.
 *
 * Original code copyright (C) 1993 by G.T.Nicol, all rights reserved.
 * Modified by Dave Hudson to use the rs232 server to transfer data via
 * the serial port
 *
 * A lot of this code came from the Linux/MGR mouse driver code.
 */
#include <sys/fs.h>
#include <stdio.h>
#include <fcntl.h>
#include <syslog.h>
#include "machine.h"
#include "mouse.h"

/*
 * Our function table
 */
static mouse_function_table ibm_serial_functions = {
   ibm_serial_poller_entry_point,	/* mouse_poller_entry_point */
   NULL,				/* mouse_interrupt */
   ibm_serial_update_period,		/* mouse_update_period */
};

/*
 * Each type of serial mouse records its details in one of these
 */
struct ibm_serial_config {
	int mask0;
	int test0;
	int mask1;
	int test1;
	int dmin;
	int dmax;
	int baud;
	int databits;
	int stopbits;
	char parity[5];
};

/*
 * General data used by the driver
 */
static ushort ibm_serial_delay_period = 100;
static uint ibm_serial_update_allowed = TRUE;
static ibm_model_t ibm_serial_model = RS_MICROSOFT;
static port_t	ibm_serial_port;
static struct ibm_serial_config *m_data;
static port_t main_port;

static struct ibm_serial_config ibm_serial_data[5] = {
	{0x40, 0x40, 0x40, 0x00,		/* MicroSoft */
	3, 4, 1200, 7, 1, "none"},
	{0xf8, 0x80, 0x00, 0x00,		/* MouseSystems 3 */
	3, 3, 1200, 8, 2, "none"},
	{0xf8, 0x80, 0x00, 0x00,		/* MouseSystems 5 */
	5, 5, 1200, 8, 2, "none"},
	{0xe0, 0x80, 0x80, 0x00,		/* MMSeries */
	3, 3, 1200, 8, 1, "odd"},
	{0xe0, 0x80, 0x80, 0x00,		/* Logitech */
	3, 3, 1200, 8, 1, "odd"}
};

/*
 * rs232_putc()
 *    Send a character to the rs232 server
 *
 * We communicate direct with the server as this eliminates a little of the
 * overhead in the libc code, but mainly because the rs232 server is a "tty"
 * server and we don't want our I/O tty'd
 */
static void
rs232_putc(uchar c)
{
	struct msg m;

	m.m_op = FS_WRITE;
	m.m_buf = &c;
	m.m_buflen = 1;
	m.m_arg = 1;
	m.m_arg1 = 0;
	m.m_nseg = 1;

	msg_send(ibm_serial_port, &m);
}

/*
 * rs232_read()
 *    Read all of the characters currently in the rs232's buffer
 *
 * Returns the number of characters read.
 */
static int
rs232_read(uchar *buf, int len)
{
	struct msg m;

	m.m_op = M_READ | FS_READ;
	m.m_buf = buf;
	m.m_arg = m.m_buflen = len;
	m.m_nseg = 1;
	m.m_arg1 = 0;

	return(msg_send(ibm_serial_port, &m));
}

/*
 * mouse()
 *	Process mouse events
 *
 * A buffer containing a single assembled mouse packet is passed
 * in; this routine updates the position and button values.
 */
static int
mouse(uchar *buffer, int len, mouse_pointer_data_t *p)
{
	char x_off, y_off;
	static char saw_middle;

	static uchar btn_cvt[8] = {
		0,
		MOUSE_RIGHT_BUTTON,
		MOUSE_MIDDLE_BUTTON,
		MOUSE_RIGHT_BUTTON | MOUSE_MIDDLE_BUTTON,
		MOUSE_LEFT_BUTTON,
		MOUSE_LEFT_BUTTON | MOUSE_RIGHT_BUTTON,
		MOUSE_LEFT_BUTTON | MOUSE_MIDDLE_BUTTON,
		MOUSE_LEFT_BUTTON | MOUSE_RIGHT_BUTTON | MOUSE_MIDDLE_BUTTON
	};

	switch (ibm_serial_model) {
	case RS_MICROSOFT:	/* Microsoft */
	default:
		p->buttons = ((buffer[0] & 0x20) >> 5) |
			((buffer[0] & 0x10) >> 3) |
			((len > m_data->dmin) ?
				((buffer[3] & 0x20) >> 3) : 0);
		x_off = (char) (((buffer[0] & 0x03) << 6) |
			(buffer[1] & 0x3f));
		y_off = (char) (((buffer[0] & 0x0c) << 4) |
			(buffer[2] & 0x3f));
		break;

	case RS_MOUSE_SYS_3:	/* Mouse Systems 3 byte */
		p->buttons = btn_cvt[(~buffer[0]) & 0x07];
		x_off = (char) (buffer[1]);
		y_off = -(char) (buffer[2]);
		break;

	case RS_MOUSE_SYS_5:	/* Mouse Systems Corp 5 bytes */
		p->buttons = btn_cvt[(~buffer[0]) & 0x07];
		x_off = (char) (buffer[1]) + (char) (buffer[3]);
		y_off = -((char) (buffer[2]) + (char) (buffer[4]));
		break;

	case RS_MM:		/* MM Series */
	case RS_LOGITECH:		/* Logitech */
		p->buttons = btn_cvt[buffer[0] & 0x07];
		x_off = (buffer[0] & 0x10) ? buffer[1] : -buffer[1];
		y_off = (buffer[0] & 0x08) ? -buffer[2] : buffer[2];
		break;
	}

	/*
	 * Middle button insanity
	 */
	if (!saw_middle) {
		if (p->buttons & MOUSE_MIDDLE_BUTTON) {
			saw_middle = 1;
		} else if ((p->buttons &
				(MOUSE_RIGHT_BUTTON|MOUSE_LEFT_BUTTON)) ==
				(MOUSE_RIGHT_BUTTON|MOUSE_LEFT_BUTTON)) {
			p->buttons = MOUSE_MIDDLE_BUTTON;
		}
	}

	/*
	 * Update the current coordinates
	 */
	p->dx += x_off;
	p->dy -= y_off;
}

/*
 * ibm_serial_check_status()
 *    Read in the mouse coordinates.
 */
static void
ibm_serial_check_status(uchar *buff, int len)
{
	static uchar pos = 0, msg[8];
	mouse_pointer_data_t md;
	int nevent = 0;

	bzero(&md, sizeof(md));
	while (len-- > 0) {
		uchar c = *buff++;

		/*
		 * For body, assemble while not end of packet
		 * and it hasn't run overlong
		 */
		if (pos > 0) {
			/*
			 * Not end.  Add to buffer if there's room.
			 */
			if (((c & m_data->mask1) == m_data->test1) &&
					(c != 0x80)) {
				if (pos < m_data->dmax) {
					msg[pos++] = c;
				}

				/*
				 * If we've seen the end of a burst,
				 * and there appears to be enough to
				 * use as a packet, drop through to
				 * process this packet.  Otherwise
				 * keep assembling.
				 */
				if ((pos < m_data->dmin) || (len > 0)) {
					continue;
				}
			}

			/*
			 * End.  This is actually the first byte of the
			 * next packet.  Feed in this packet, then fall
			 * into start-of-packet handling.
			 */
			if (pos >= m_data->dmin) {
				mouse(msg, pos, &md);
				nevent += 1;
			}
			pos = 0;
		}

		/*
		 * For first position, must see a packet header
		 */
		if ((c & m_data->mask0) == m_data->test0) {
			msg[pos++] = c;
		}
	}

	/*
	 * Notify of the change
	 */
	if (nevent > 0) {
		struct msg m;

		m.m_op = MOUSE_UPD;
		m.m_buf = &md;
		m.m_arg = m.m_buflen = sizeof(md);
		m.m_nseg = 1;
		m.m_arg1 = 0;
		(void)msg_send(main_port, &m);
	}
}

/*
 * ibm_serial_poller_entry_point()
 *    Main loop of the polling thread.
 */
void
ibm_serial_poller_entry_point(void)
{
	uchar buf[64];
	int len;

	main_port = msg_connect(mouse_name, MOUSE_MAGIC);
	for (;;) {
		len = rs232_read(buf, sizeof(buf));
		ibm_serial_check_status(buf, len);
	}
}

/*
 * ibm_serial_update_period()
 *	Read the mouse button status
 */
void
ibm_serial_update_period(ushort period)
{
	if (period == 0)
		period = 5;
	set_semaphore(&ibm_serial_update_allowed, FALSE);
	ibm_serial_delay_period = 1000 / period;
	set_semaphore(&ibm_serial_update_allowed, TRUE);

	if (ibm_serial_model == RS_LOGITECH) {
		rs232_putc('S');
	}
	if (period <= 0)
		rs232_putc('O');
	else if (period <= 15)
		rs232_putc('J');
	else if (period <= 27)
		rs232_putc('K');
	else if (period <= 42)
		rs232_putc('L');
	else if (period <= 60)
		rs232_putc('R');
	else if (period <= 85)
		rs232_putc('M');
	else if (period <= 125)
		rs232_putc('Q');
	else
		rs232_putc('N');
}

/*
 * ibm_serial_usage()
 *	Inform the user how to use the serial mouse driver
 */
static void
ibm_serial_usage(void)
{
	fprintf(stderr,
		"usage: mouse -type serial <-p port | rs232-dev-file> "
		"[-delay=<delay-in-ms>]\n"
		"       <-microsoft | -mouse_sys_3 | -mouse_sys_5 |"
		" -mm | -logitech>\n");
	exit(1);
}

/*
 * ibm_serial_parse_args()
 *	Parse the command line.
 *
 * We also establish a connection to the relevant rs232 server port.
 */
static void
ibm_serial_parse_args(int argc, char **argv)
{
	int microsoft_option = 0, mouse_sys_3_option = 0,
		mouse_sys_5_option = 0,
		mm_option = 0, logitech_option = 0;
	int arg, arg_st;
	char st_msg[32];

	/*
	 * Check we have enough parameters
	 */
	if (argc < 5) {
		ibm_serial_usage();
	}

	/*
	 * Look for the rs232 server details
	 */
	if (strcmp(argv[3], "-p") == 0) {
		/*
		 * Looks like we're attaching straight to the server
		 */
		if (!argv[4]) {
			fprintf(stderr, "mouse: missing port parameter\n");
			exit(1);
		}
		ibm_serial_port =
			path_open(argv[4], ACC_READ|ACC_WRITE|ACC_CHMOD);
		if (ibm_serial_port < 0) {
			syslog(LOG_ERR,
				"unable to get connection to '%s'", argv[4]);
			exit(1);
		}
		arg_st = 5;
	} else {
		/*
		 * Try to attach to the server via the specified mounted file
		 */
		int fd;

		fd = open(argv[3], O_RDWR);
		if (fd < 0) {
			syslog(LOG_ERR,
				"unable to open path to '%s'", argv[3]);
			exit(1);
		}
		ibm_serial_port = __fd_port(fd);
		arg_st = 4;
	}

	/*
	 * Parse the command line args - things such as establishing the mouse
	 * type and the baud rate
	 */
	for (arg = arg_st; arg < argc; arg++) {
		if (argv[arg][0] != '-')
			continue;
		if (strcmp(&argv[arg][1], "delay=") == 0) {
			if (argv[arg][7] == '\0')
				ibm_serial_usage();
			ibm_serial_delay_period = atoi(&argv[arg][7]);
		} else if (strcmp(&argv[arg][1], "microsoft") == 0) {
			microsoft_option = 1;
			ibm_serial_model = RS_MICROSOFT;
		} else if (strcmp(&argv[arg][1], "mouse_sys_3") == 0) {
			mouse_sys_3_option = 1;
			ibm_serial_model = RS_MOUSE_SYS_3;
		} else if (strcmp(&argv[arg][1], "mouse_sys_5") == 0) {
			mouse_sys_5_option = 1;
			ibm_serial_model = RS_MOUSE_SYS_5;
		} else if (strcmp(&argv[arg][1], "mm") == 0) {
			mm_option = 1;
			ibm_serial_model = RS_MM;
		} else if (strcmp(&argv[arg][1], "logitech") == 0) {
			logitech_option = 1;
			ibm_serial_model = RS_LOGITECH;
		} else {
			fprintf(stderr,
				"mouse: unknown option '%s' - aborting\n",
				argv[arg]);
			exit(1);
		}
	}

	/*
	 * Record for all to enjoy from here on
	 */
	m_data = &ibm_serial_data[ibm_serial_model];

	/*
	 * Set DTR and RTS toggle to power up the mouse and reset it.  Also
	 * establish the baud rate and the data/stop bit/parity settings.
	 */
	sprintf(st_msg, "baud=%d\n", m_data->baud);
	wstat(ibm_serial_port, st_msg);
	sprintf(st_msg, "databits=%d\n", m_data->databits);
	wstat(ibm_serial_port, st_msg);
	sprintf(st_msg, "stopbits=%d\n", m_data->stopbits);
	wstat(ibm_serial_port, st_msg);
	sprintf(st_msg, "parity=%s\n", m_data->parity);
	wstat(ibm_serial_port, st_msg);
	wstat(ibm_serial_port, "dtr=0\n");
	wstat(ibm_serial_port, "rts=0\n");
	__msleep(400);
	wstat(ibm_serial_port, "rts=1\n");
	wstat(ibm_serial_port, "dtr=1\n");

	if (microsoft_option + mouse_sys_3_option + mouse_sys_5_option +
			mm_option + logitech_option > 1) {
		ibm_serial_usage();
	}
	switch(ibm_serial_model) {
	case RS_MOUSE_SYS_5:
		rs232_putc('*');
		rs232_putc('n');
		rs232_putc('*');
		rs232_putc('U');
	}
}

/*
 * ibm_serial_initialise()
 *	Initialise the mouse system.
 */
int
ibm_serial_initialise(int argc, char **argv)
{
	mouse_data_t *m = &mouse_data;

	/*
	 *  Initialise the system data.
	 */
	m->functions = ibm_serial_functions;
	m->pointer_data.dx =
	m->pointer_data.dy = 0;
	m->pointer_data.buttons = 0;
	m->irq_number = 0;
	m->update_frequency = ibm_serial_delay_period;

	/*
	 * Parse mouse specific args
	 */
	ibm_serial_parse_args(argc, argv);

	/*
	 *  Make doubly sure that we have every ready for polling.
	 */
	m->functions.mouse_interrupt = NULL;
	m->enable_interrupts = FALSE;
	ibm_serial_update_period(m->update_frequency);

	syslog(LOG_INFO, "serial mouse installed");

	return (0);
}
