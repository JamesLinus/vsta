#ifndef _TERMIOS_H
#define _TERMIOS_H
/* 
 * termios.h
 *	POSIX-defined interface for controlling TTY
 */
#include <sys/types.h>

/*
 * Central TTY state
 */
struct termios {
	ulong c_iflag;		/* Input */
	ulong c_oflag;		/* Output */
	ulong c_cflag;		/* Control */
	ulong c_lflag;		/* Local */
	uchar c_cc[11];		/* Characters */
	ulong c_ispeed;		/* Input baud */
	ulong c_ospeed;		/* Output baud */
};

/*
 * Slots in c_cc[]
 */
#define VEOF 0		/* EOF */
#define VEOL 1		/* EOL */
#define VERASE 2	/* Erase one char */
#define VWERASE 3	/*  ...one word */
#define VKILL 4		/* Kill current line of input */
#define VINTR 5		/* Signal process group */
#define VQUIT 6		/* Abort process group */
#define VSTART 7	/* Start output */
#define VSTOP 8		/* Stop output */
#define VMIN 9		/* Min chars when ~ICANON */
#define VTIME 10	/*  ...time */

/*
 * Bits in c_iflag
 */
#define IGNBRK 0x1	/* Ignore breaks */
#define BRKINT 0x2	/* Map breaks to VINTR */
#define IGNPAR 0x4	/* Ignore parity errors */
#define PARMRK 0x8	/* Mark parity errors */
#define INPCK 0x10	/* Enable parity checking */
#define ISTRIP 0x20	/* Strip high bit */
#define INLCR 0x40	/* NL -> CR */
#define IGNCR 0x80	/* Ignore CR */
#define ICRNL 0x100	/* CR -> NL */
#define IXON 0x200	/* Outgoing flow control */
#define IXOFF 0x400	/* Incoming flow control */

/*
 * Bits in c_oflag
 */
#define OPOST 0x1	/* Enable output processing */
#define ONLCR 0x2	/* NL -> CR/NL */

/*
 * Bits in c_cflag
 */
#define CSIZE 0x300	/* Character size */
#define  CS7 0x100	/*  ... 7 bits */
#define  CS8 0x200	/*  ...8 */
#define CSTOPB 0x1	/* Two stop bits */
#define CREAD 0x2	/* Enable reads */
#define PARENB 0x4	/* Parity */
#define PARODD 0x8	/* Odd parity */
#define HUPCL 0x10	/* Drop DTR on last close */
#define CLOCAL 0x20	/* Ignore DSR/CD/etc */

/* 
 * Bits in c_lflag
 */
#define ECHOE 0x1	/* Echo BS-space-BS on erase */
#define ECHOK 0x2	/* Echo NL for line kill */
#define ECHO 0x4	/* Echo typing */
#define ECHONL 0x8	/* Echo NL always */
#define ISIG 0x10	/* Enable VINTR etc. */
#define ICANON 0x20	/* Enable line-oriented input processing */
#define NOFLSH 0x200	/* don't flush after interrupt */

/* 
 * Operations for tcsetattr()
 */
#define TCSANOW 0	/* Make change now */
#define TCSADRAIN 1	/*  ...drain first */
#define TCSAFLUSH 2	/*  ...drain output, chuck input */

/*
 * Values for get/set in/out speed
 */
#define B0 0
#define B50 50
#define B75 75
#define B110 110
#define B134 134
#define B150 150
#define B200 200
#define B300 300
#define B600 600
#define B1200 1200
#define B1800 1800
#define B2400 2400
#define B4800 4800
#define B9600 9600
#define B19200 19200
#define B38400 38400

/*
 * Routines for changing TTY state
 */
extern int cfsetispeed(struct termios *, ulong),
	cfsetospeed(struct termios *, ulong);
extern ulong cfgetispeed (struct termios *),
	cfgetospeed (struct termios *);
extern int tcdrain(int), tcflow(int, int), tcflush(int, int),
	tcsendbreak(int, int),
	tcgetattr(int, struct termios *),
	tcsetattr(int, int, struct termios *);
extern int tcgetsize(int fd, int *rows, int *cols);

#endif /* _TERMIOS_H */
