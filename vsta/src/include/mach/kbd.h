#ifndef _KEYBD_H
#define _KEYBD_H
/*
 * kbd.h
 *	Declarations for PC/AT keyboard controller
 */

#define KEYBD_MAXBUF (1024)	/* # bytes buffered from keyboard */

/*
 * I/O ports used
 */
#define KEYBD_LOW KEYBD_DATA

#define KEYBD_DATA 0x60
#define KEYBD_CTL 0x61
#define KEYBD_STATUS 0x64

#define KEYBD_HIGH KEYBD_STATUS

/*
 * Bit in KEYBD_CTL for strobing enable
 */
#define KEYBD_ENABLE 0x80

/*
 * Bits in KEYBD_STATUS
 */
#define KEYBD_BUSY 0x2
#define KEYBD_WRITE 0xD1

/*
 * Command for KEYBD_DATA to turn on high addresses
 */
#define KEYBD_ENAB20 0xDF

/*
 * Interrupt vector
 */
#define KEYBD_IRQ 1	/* Hardware IRQ1==interrupt vector 9 */

/*
 * Function key scan codes
 */
#define F1 (59)
#define F10 (68)

#ifdef KBD
#include <sys/types.h>

/*
 * Structure for per-connection operations
 */
struct file {
	int f_sender;	/* Sender of current operation */
	uint f_gen;	/* Generation of access */
	uint f_flags;	/* User access bits */
	uint f_count;	/* # bytes wanted for current op */
};
#endif

#endif /* _KEYBD_H */
