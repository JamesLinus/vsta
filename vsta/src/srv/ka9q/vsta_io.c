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
#include <sys/types.h>
#include <stdio.h>
#include <std.h>
#include <termios.h>
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
#include "unix.h"
#include "cmdparse.h"

struct asy asy[ASY_MAX];
struct interface *ifaces;
struct termios mysavetty, savecon;
int savettyfl;
int IORser[ASY_MAX];
unsigned int nasy = ASY_MAX;

/* Called at startup time to set up console I/O, memory heap */
ioinit(void)
{
	struct termios ttybuf;
	extern void ioexit();

	tcgetattr(0, &ttybuf);
	savecon = ttybuf;

	ttybuf.c_iflag &= ~(ICRNL);
	ttybuf.c_lflag &= ~(ICANON|ISIG|ECHO);
	ttybuf.c_cc[VTIME] = 1;
	ttybuf.c_cc[VMIN] = 0;

	tcsetattr(0, TCSANOW, &ttybuf);
	return 0;
}

void
ioexit()
{
	iostop();
	exit(0);
}

void
clearout(fd)
     int fd;
{
	if (fd == 0 || fd == 1)
	  fflush(stdout);
}

void
flowon(int fd)
{
	struct termios ttybuf;

	tcgetattr(fd, &ttybuf);
	ttybuf.c_iflag |= IXON;
	tcsetattr(fd, TCSANOW, &ttybuf);
}

void
flowoff(int fd)
{
	struct termios ttybuf;

	tcgetattr(fd, &ttybuf);
	ttybuf.c_iflag &= ~IXON;
	tcsetattr(fd, TCSANOW, &ttybuf);
}

void
flowdefault(int fd, int tts)
{
	struct termios ttybuf;
	int iflag;

	if (tts) {
		iflag = remote_tty_iflag(tts);
	} else {
		iflag = savecon.c_iflag;
	}

	tcgetattr(fd, &ttybuf);
	if (iflag & IXON) {
		ttybuf.c_iflag |= IXON;
	} else {
		ttybuf.c_iflag &= ~IXON;
	}
	tcsetattr(fd, TCSANOW, &ttybuf);
}

void
set_stdout(fd, mode)
	int fd;
	int mode;
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

/* Initialize asynch port "dev" */
int
asy_init(int16 dev, char *arg1, char *arg2, unsigned int bufsize)
{
	struct asy *ap;
	struct termios	sgttyb;

	if (dev >= nasy)
		return -1;

	ap = &asy[dev];
	ap->tty = malloc((unsigned)(strlen(arg2)+1));
	strcpy(ap->tty, arg2);
	printf("asy_init: tty name = %s\n", ap->tty);

	if ((IORser[dev] = open(ap->tty, O_RDWR, 0)) < 0) {
		perror("Could not open device IORser");
		return -1;
	}
	 /* 
	  * get the stty structure and save it 
	  */
	tcgetattr(IORser[dev], &mysavetty);

	 /* 
	  * copy over the structure 
	  */
	sgttyb = mysavetty;
	sgttyb.c_iflag = (IGNBRK | IGNPAR);
	sgttyb.c_oflag = 0;
	sgttyb.c_lflag = 0;
	sgttyb.c_cflag = (B9600 | CS8 | CREAD);
	sgttyb.c_cc[VEOL] = 0300;
	sgttyb.c_cc[VTIME] = 0;
	sgttyb.c_cc[VMIN] = 1;

	tcsetattr(IORser[dev], TCSANOW, &sgttyb);

	return 0;
}

int
asy_stop(struct interface *iface)
{
	return(0);
}


/* Set asynch line speed */
int
asy_speed(dev, speed)
int16 dev;
int speed;
{
	struct termios sgttyb;

	if (speed == 0 || dev >= nasy)
		return -1;

#ifdef	SYS5_DEBUG
	printf("asy_speed: Setting speed for device %d to %d\n",dev, speed);
#endif

	asy[dev].speed = speed;
	tcgetattr(IORser[dev], &sgttyb);
	cfsetispeed(&sgttyb, speed);
	cfsetospeed(&sgttyb, speed);
	tcsetattr(IORser[dev], TCSANOW, &sgttyb);

	return(0);
}


/* Send a buffer to serial transmitter */
asy_output(int dev, char *buf, int cnt)
{
	if (dev >= nasy)
		return -1;

	if (write(IORser[dev], buf, cnt) < cnt) {
		perror("asy_output");
		printf("asy_output: error in writing to device %d\n", dev);
		return -1;
	}

	return 0;
}

/* Receive characters from asynch line
 * Returns count of characters read
 */
int16
asy_recv(int dev, char *buf, int cnt)
{
#define	IOBUFLEN	256
	unsigned tot;
	int r;
	static struct	{
		char	buf[IOBUFLEN];
		char	*data;
		int	cnt;
	}	IOBUF[ASY_MAX];

	if(IORser[dev] < 0) {
		printf("asy_recv: bad file descriptor passed for device %d\n",
			dev);
		return(0);
	}
	tot = 0;
	/* fill the read ahead buffer */
	if (IOBUF[dev].cnt == 0) {
		IOBUF[dev].data = IOBUF[dev].buf;
		r = read(IORser[dev], IOBUF[dev].data, IOBUFLEN);
		/* check the read */
		if (r == -1) {
			IOBUF[dev].cnt = 0;	/* bad read */
			return(0);
		} else {
			IOBUF[dev].cnt = r;
		}
		
	} 
	r = 0;	/* return count */
	/* fetch what you need with no system call overhead */
	if (IOBUF[dev].cnt > 0) {
		if(cnt == 1) { /* single byte copy, do it here */
			*buf = *IOBUF[dev].data++;
			IOBUF[dev].cnt--;
			r = 1;
		} else { /* multi-byte copy, left memcpy do the work */
			unsigned n = min(cnt, IOBUF[dev].cnt);
			memcpy(buf, IOBUF[dev].data, n);
			IOBUF[dev].cnt -= n;
			IOBUF[dev].data += n;
			r = n;
		}
	}
	tot = (unsigned int) r;
	return (tot);
}

asy_ioctl(struct interface *i, int argc, char **argv)
{
	if (argc < 1) {
		printf("%d\r\n", asy[i->dev].speed);
		return 0;
	}
	return asy_speed(i->dev, atoi(argv[0]));
}
