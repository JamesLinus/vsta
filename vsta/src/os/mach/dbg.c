/*
 * dbg.c
 *	A simple/simplistic debug interface
 *
 * Hard-wired to talk out the COM1 serial port at 9600 baud.  Does
 * not use interrupts.
 */
static char buf[80];		/* Typing buffer */
static int dbg_init = 0;
static int col = 0;		/* When to wrap */

/*
 * 1 for COM1, 0 for COM2 (bleh)
 */
#define COM (1)

/*
 * I/O address of registers
 */
#define IOBASE (0x2F0 + COM*0x100)	/* Base of registers */
#define LINEREG (IOBASE+0xB)	/* Format of RS-232 data */
#define LOWBAUD (IOBASE+0x8)	/* low/high parts of baud rate */
#define HIBAUD (IOBASE+0x9)
#define LINESTAT (IOBASE+0xD)	/* Status of line */
#define  RXRDY (1)		/* Character assembled */
#define  TXRDY (0x20)		/* Transmitter ready for next char */
#define DATA (IOBASE+0x8)	/* Read/write data here */
#define INTREG (IOBASE+0x9)	/* Interrupt control */
#define INTID (IOBASE+0xA)	/* Why "interrupted" */
#define MODEM (IOBASE+0xC)	/* Modem lines */

/*
 * rs232_init()
 *	Initialize to 9600 baud on com port
 */
static void
rs232_init(void)
{
	outportb(LINEREG, 0x80);	/* 9600 baud */
	outportb(HIBAUD, 0);
	outportb(LOWBAUD, 0x0C);
	outportb(LINEREG, 3);		/* 8 bits, one stop */
}

/*
 * rs232_putc()
 *	Busy-wait and then send a character
 */
static void
rs232_putc(int c)
{
	while ((inportb(LINESTAT) & 0x20) == 0)
		;
	outportb(DATA, c & 0x7F);
}

/*
 * rs232_getc()
 *	Busy-wait and return next character
 */
static
rs232_getc(void)
{
	while ((inportb(LINESTAT) & 1) == 0)
		;
	return(inportb(DATA) & 0x7F);
}

/*
 * putchar()
 *	Write a character to the debugger port
 *
 * Brain damage as my serial terminal doesn't wrap columns.
 */
void
putchar(int c)
{
	if (c == '\n') {
		col = 0;
		rs232_putc('\r');
	} else {
		if (++col >= 78) {
			rs232_putc('\r'); rs232_putc('\n');
			col = 1;
		}
	}
	rs232_putc(c);
}

/*
 * getchar()
 *	Get a character from the debugger port
 */
getchar(void)
{
	char c;

	c = rs232_getc() & 0x7F;
	if (c == '\r')
		c = '\n';
	return(c);
}

/*
 * gets()
 *	A spin-oriented "get line" routine
 */
void
gets(char *p)
{
	char c;
	char *start = p;

	putchar('>');
	for (;;) {
		c = getchar();
		if (c == '\b') {
			if (p > start) {
				printf("\b \b");
				p -= 1;
			}
		} else if (c == '') {
			p = start;
			printf("\\\n");
		} else {
			putchar(c);
			if (c == '\n')
				break;
			*p++ = c;
		}
	}
	*p = '\0';
}

/*
 * dbg_enter()
 *	Basic interface for debugging
 */
void
dbg_enter(void)
{
	extern void dbg_main();

	if (!dbg_init) {
		rs232_init();
		dbg_init = 1;
	}
	printf("[Kernel debugger]\n");
	dbg_main();
}
