#ifndef _RS232_H
#define _RS232_H
/*
 * rs232.h
 *	Defines for PC RS232 adaptor
 *
 * Derived from COM driver in FreeBSD, originally from the UC Regents
 * and the Berkeley Software Distribution.
 */
#include <sys/types.h>
#include <sys/msg.h>

#define RS232_MAXBUF (16*1024)	/* # bytes buffered from/to serial port */
#define RS232_STDSUPPORT (4)	/* Support 4 standard PC ports, com1-com4 */
#define RS232_UARTNAMEMAX (8)	/* Max length of name of UART */

/*
 * Special messages
 */
#define RS232_HELPER (301)	/* Ask for helper to run */

/*
 * Maximum range of I/O ports used
 */
#define RS232_HIGH(base) (base+7)

/*
 * Structure for per-connection operations
 */
struct file {
	int f_sender;		/* Sender of current operation */
	uint f_gen;		/* Generation of access */
	uint f_flags;		/* User access bits */
	uint f_count;		/* Number of bytes wanted for current op */
	void *f_buf;		/* Buffer for current write */
};

/*
 * 16 bit baud rate divisor (lower byte in dca_data, upper in dca_ier)
 */
#define	COMBRD(x)	(1843200 / (16*(x)))

/*
 * Parity constants
 */
#define PARITY_NONE	0x0
#define PARITY_EVEN	0x1
#define PARITY_ODD	0x2
#define PARITY_ZERO	0x3	
#define PARITY_ONE	0x4

/*
 * Receive/transmit data here, also baud low
 */
#define DATA (0)
#define BAUDLO (0)

/*
 * interrupt enable register, also baud high
 */
#define IER (1)
#define BAUDHI (1)
#define	IER_ERXRDY	0x1
#define	IER_ETXRDY	0x2
#define	IER_ERLS	0x4
#define	IER_EMSC	0x8

/*
 * interrupt identification register
 */
#define IIR (2)
#define	IIR_IMASK	0xf
#define	IIR_RXTOUT	0xc
#define	IIR_RLS		0x6
#define	IIR_RXRDY	0x4
#define	IIR_TXRDY	0x2
#define	IIR_NOPEND	0x1
#define	IIR_MLSC	0x0
#define	IIR_FIFO_MASK	0xc0	/* set if FIFOs are enabled */

/*
 * fifo control register
 */
#define FIFO (2)
#define	FIFO_ENABLE	0x01
#define	FIFO_RCV_RST	0x02
#define	FIFO_XMT_RST	0x04
#define	FIFO_DMA_MODE	0x08
#define	FIFO_TRIGGER_1	0x00
#define	FIFO_TRIGGER_4	0x40
#define	FIFO_TRIGGER_8	0x80
#define	FIFO_TRIGGER_14	0xc0

/*
 * character format control register
 */
#define CFCR (3)
#define	CFCR_DLAB	0x80
#define	CFCR_SBREAK	0x40
#define	CFCR_PZERO	0x30
#define	CFCR_PONE	0x20
#define	CFCR_PEVEN	0x10
#define	CFCR_PODD	0x00
#define	CFCR_PENAB	0x08
#define	CFCR_STOPB	0x04
#define	CFCR_8BITS	0x03
#define	CFCR_7BITS	0x02
#define	CFCR_6BITS	0x01
#define	CFCR_5BITS	0x00

/*
 * modem control register
 */
#define MCR (4)
#define	MCR_LOOPBACK	0x10
#define	MCR_IENABLE	0x08
#define	MCR_DRS		0x04
#define	MCR_RTS		0x02
#define	MCR_DTR		0x01

/*
 * line status register
 */
#define LSR (5)
#define	LSR_RCV_FIFO	0x80
#define	LSR_TSRE	0x40
#define	LSR_TXRDY	0x20
#define	LSR_BI		0x10
#define	LSR_FE		0x08
#define	LSR_PE		0x04
#define	LSR_OE		0x02
#define	LSR_RXRDY	0x01
#define	LSR_RCV_MASK	0x1f

/*
 * modem status register
 */
#define MSR (6)
#define	MSR_DCD		0x80
#define	MSR_RI		0x40
#define	MSR_DSR		0x20
#define	MSR_CTS		0x10
#define	MSR_DDCD	0x08
#define	MSR_TERI	0x04
#define	MSR_DDSR	0x02
#define	MSR_DCTS	0x01

/*
 * Scrathpad register
 */
#define SCRATCH (7)

/*
 * UART types
 */
#define UART_UNKNOWN	-1
#define UART_8250	0
#define UART_16450	1
#define UART_16550	2
#define UART_16550A	3

/*
 * Function prototypes for rw.c
 */
extern void rs232_write(struct msg *m, struct file *fl);
extern void dequeue_tx(void);
extern void rs232_read(struct msg *m, struct file *fl);
extern void dequeue_rx(void);
void rs232_init(void);
void abort_io(struct file *f);

/*
 * Prototypes for isr.c
 */
extern void start_tx(void);
extern void rs232_enable(int);
extern void run_helper(void);
extern ulong overruns;
extern volatile uint txbusy;

/*
 * Prototypes for control.c
 */
extern void rs232_baud(int baud);
extern void rs232_databits(int dbits);
extern void rs232_stopbits(int sbits);
extern void rs232_parity(int ptype);
extern void rs232_setdtr(int newdtr);
extern void rs232_setrts(int newrts);
extern void rs232_getinsigs(void);
extern int rs232_setrxfifo(int threshold);
extern int rs232_iduart(int test_uart);

/*
 * Prototypes for stat.c
 */
extern void rs232_stat(struct msg *m, struct file *f);
extern void rs232_wstat(struct msg *m, struct file *f);
extern uchar onlcr;

/*
 * Prototypes from main.c
 */
extern port_name rs232port_name;
extern int kdb;

#endif /* _RS232_H */
