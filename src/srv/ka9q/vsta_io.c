/*
	FILE: vsta_io.c
	
	Routines:
		ioinit()
		asy_init()
		asy_stop()
		asy_speed()
		asy_output()
		asy_recv()
	Written or converted by Mikel Matthews, N9DVG
	SYS5 added by Jere Sandidge, K4FUM
	Directory pipe added by Ed DeHart, WA3YOA
	Numerous changes by Charles Hedrick and John Limpert, N3DMC
	Hell, *I* even hacked on it... :-)  Bdale, N3EUA
	VSTa port by Andy Valencia, WB6RRU
*/
#include <sys/fs.h>
#include <termios.h>
#include <stdio.h>
#include <std.h>
#include <signal.h>
#include <dirent.h>
#include <time.h>
#include <fcntl.h>
#include <memory.h>
#include <string.h>
#include "global.h"
#include "asy.h"
#include "mbuf.h"
#include "internet.h"
#include "iface.h"
#include "cmdparse.h"
#include "vsta.h"

/*
 * Globals
 */
unsigned int nasy = 0;
#define BUFLEN (4096)

/*
 * Per async port state
 */
static struct asy {
	char *a_tty;		/* Port accessed */
	port_t a_txport,	/* Open ports to this name */
		a_rxport;
	char volatile *
		a_txbuf;	/* When active, buffer being sent */
	int a_txcnt;		/*  ...size */
	char *a_rxbuf;		/* Receive buffer */
	volatile int
		a_hd, a_tl;	/* Head/tail of rx FIFO */
	pid_t a_txpid,		/* Thread serving this port (tx) */
		a_rxpid;	/*   ...rx */
} asy[ASY_MAX];

/*
 * ioinit()
 *	Called at startup time to set up console I/O
 */
int
ioinit(void)
{
	/*
	 * Console is set up in our code for the console thread
	 */
	return 0;
}

/*
 * clearout()
 *	Hack to flush buffered I/O on file descriptor op
 */
void
clearout(fd)
     int fd;
{
	if ((fd == 0) || (fd == 1))
		fflush(stdout);
}

/*
 * flowon()
 *	Ask for flow control on the given tty
 */
void
flowon(int fd)
{
	struct termios ttybuf;

	tcgetattr(fd, &ttybuf);
	ttybuf.c_iflag |= IXON;
	tcsetattr(fd, TCSANOW, &ttybuf);
}

/*
 * flowoff()
 *	Ask for no flow control on the given tty
 */
void
flowoff(int fd)
{
	struct termios ttybuf;

	tcgetattr(fd, &ttybuf);
	ttybuf.c_iflag &= ~IXON;
	tcsetattr(fd, TCSANOW, &ttybuf);
}

/*
 * flowdefault()
 *	Ask for default flow control
 *
 * Always off by default
 */
void
flowdefault(int fd, int tts)
{
	struct termios ttybuf;

	tcgetattr(fd, &ttybuf);
	ttybuf.c_iflag &= ~IXON;
	tcsetattr(fd, TCSANOW, &ttybuf);
}

/*
 * set_stdout()
 *	Set output for \n vs. \r\n
 */
void
set_stdout(int fd, int mode)
{
	struct termios ttybuf;

	tcgetattr(fd, &ttybuf);
	if (mode == 2) {  /* cooked */
		ttybuf.c_oflag |= ONLCR;
	} else {
		ttybuf.c_oflag &= ~ONLCR;
	}
	tcsetattr(fd, TCSANOW, &ttybuf);
}

/*
 * asy_rx_daemon()
 *	Thread to watch for incoming bytes, and queue them
 */
static void
asy_rx_daemon(struct asy *ap)
{
	struct msg m;
	int cnt, tl;

	for (;;) {
		/*
		 * See how much we can pull in this time
		 */
		tl = ap->a_tl;
		if (ap->a_hd >= tl) {
			cnt = BUFLEN - ap->a_hd;
		} else {
			cnt = (tl - ap->a_hd) - 1;
		}
		if (cnt <= 0) {
			__msleep(10);
			continue;
		}

		/*
		 * Do the receive
		 */
		m.m_op = FS_READ | M_READ;
		m.m_arg = m.m_buflen = cnt;
		m.m_arg1 = 0;
		m.m_buf = ap->a_rxbuf + ap->a_hd;
		m.m_nseg = 1;
		cnt = msg_send(ap->a_rxport, &m);

		/*
		 * Update state
		 */
		if (cnt <= 0) {
			__msleep(10);
			continue;
		}
		if ((ap->a_hd + cnt) >= BUFLEN) {
			ap->a_hd = 0;
		} else {
			ap->a_hd += cnt;
		}

		/*
		 * Tell the main loop to run
		 */
		recalc_timers();
	}
}

/*
 * asy_tx_daemon()
 *	Daemon to wait for data to transmit, then transmit it
 */
static void
asy_tx_daemon(struct asy *ap)
{
	struct msg m;

	for (;;) {
		/*
		 * Wait for work
		 */
		mutex_thread(0);

		/*
		 * Transmit it
		 */
		m.m_op = FS_WRITE;
		m.m_arg = m.m_buflen = ap->a_txcnt;
		m.m_arg1 = 0;
		m.m_buf = (char *)ap->a_txbuf;
		m.m_nseg = 1;
		(void)msg_send(ap->a_txport, &m);

		/*
		 * Flag completion
		 */
		ap->a_txbuf = 0;
		recalc_timers();
	}
}

/*
 * asy_init()
 *	Initialize async state for given tty
 */
int
asy_init(int16 dev, char *port, unsigned int bufsize)
{
	struct asy *ap;
	port_t p;

	if (dev >= nasy)
		return -1;

	/*
	 * Access port
	 */
	ap = &asy[dev];
	p = ap->a_rxport = path_open(port, ACC_READ | ACC_WRITE);
	if (p < 0) {
		perror(port);
		return(-1);
	}

	/*
	 * Record name
	 */
	ap->a_tty = strdup(port);
	printf("asy_init: tty name = %s\n", ap->a_tty);

	/* 
	 * Set to default parameters
	 */
	(void)wstat(p, "baud=9600\n");
	(void)wstat(p, "databits=8\n");
	(void)wstat(p, "stopbits=1\n");
	(void)wstat(p, "parity=none\n");
	(void)wstat(p, "onlcr=0\n");

	/*
	 * Initialize tty, launch threads
	 */
	ap->a_txport = clone(p);
	ap->a_rxbuf = malloc(BUFLEN);
	ap->a_rxpid = tfork(asy_rx_daemon, (ulong)ap);
	ap->a_txpid = tfork(asy_tx_daemon, (ulong)ap);

	return(0);
}

/*
 * asy_stop()
 *	Stop using a line
 */
int
asy_stop(struct interface *i)
{
	struct asy *ap = &asy[i->dev];

	/*
	 * Terminate threads, close async port
	 */
	(void)notify(0, ap->a_txpid, "kill");
	(void)notify(0, ap->a_rxpid, "kill");
	(void)msg_disconnect(ap->a_rxport);
	(void)msg_disconnect(ap->a_txport);

	return(0);
}


/*
 * asy_speed()
 *	Set asynch line speed
 */
int
asy_speed(int16 dev, int speed)
{
	char buf[64];
	struct asy *ap;

	ap = &asy[dev];
	sprintf(buf, "baud=%d\n", speed);
	return(wstat(ap->a_txport, buf));
}

/*
 * asy_output()
 *	Send a buffer to serial transmitter thread
 */
asy_output(int16 dev, char *buf, int cnt)
{
	struct asy *ap = &asy[dev];

	ap->a_txbuf = buf;
	ap->a_txcnt = cnt;
	mutex_thread(ap->a_txpid);

	return(0);
}

/*
 * asy_recv()
 *	Receive characters from asynch line
 *
 * Returns count of characters read
 */
int16
asy_recv(int16 dev, char *buf, int cnt)
{
	struct asy *ap = &asy[dev];
	int x, avail;

	/*
	 * Take the lesser of how much is there and how much
	 * they want.
	 */
	x = ap->a_hd;
	if (x < ap->a_tl) {
		avail = BUFLEN - ap->a_tl;
	} else {
		avail = x - ap->a_tl;
	}
	if (avail > cnt) {
		avail = cnt;
	}
	if (avail <= 0) {
		return(0);
	}

	/*
	 * Copy it out
	 */
	bcopy(ap->a_rxbuf + ap->a_tl, buf, avail);

	/*
	 * Update state
	 */
	x = ap->a_tl + avail;
	if (x >= BUFLEN) {
		ap->a_tl = 0;
	} else {
		ap->a_tl = x;
	}

	return(avail);
}

/*
 * asy_ioctl()
 *	General interface to wstat functions of async driver
 */
asy_ioctl(struct interface *i, int argc, char **argv)
{
	struct asy *ap = &asy[i->dev];

	if (argc < 1) {
		printf("%s\r\n", rstat(ap->a_txport, "baud"));
		return(0);
	}
	return(wstat(ap->a_txport, argv[0]));
}

/*
 * stxrdy()
 *	Tell if transmitter is ready for more
 */
stxrdy(int16 dev)
{
	struct asy *ap = &asy[dev];

	return(ap->a_txbuf == 0);
}
