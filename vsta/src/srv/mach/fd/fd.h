#ifndef _FD_H
#define _FD_H
/*
 * fd.h
 *	Declarations for PC/AT floppy disks
 */
#include <sys/types.h>
#include <sys/msg.h>
#include <lib/llist.h>
#include <mach/nvram.h>

/*
 * # floppy units supported
 */
#define NFD 2

/*
 * # times to retry an I/O before giving up
 */
#define FD_MAXERR 5

/*
 * Special value for f_unit to indicate we're in the root dir
 * of the devices.
 */
#define ROOTDIR (999)

/*
 * Per-floppy state
 */
struct floppy {
	struct llist f_q;	/* List of pending work */
	uchar f_state;		/* State (see below) */
	uchar f_density;	/* What format floppy */
	uchar f_unit;		/* Our unit # */
	uchar f_spinning;	/* Our motor is on */
	uint f_cyl;		/* Cylinder heads are currently on */
	uint f_opencnt;		/* # open references */
};
extern struct floppy floppies[NFD];

/*
 * Description of a type of floppy
 */
struct fdparms {
	int f_sectors;		/* sectors/track */
	int f_gap;		/* gap between sectors */
	int f_tracks;		/* total tracks */
	int f_size;		/* size of disk, sectors */
};
extern struct fdparms fdparms[];

/*
 * f_state values
 */
#define F_OFF 1		/* Motor off, but device open */
#define F_CLOSED 2	/* No user */
#define F_SPINUP1 3	/* Motor starting for recal */
#define F_SPINUP2 4	/* Motor starting for same floppy */
#define F_RECAL 5	/* Recalibrating */
#define F_READY 6	/* Format known, ready for work */
#define F_SEEK 7	/* Head in motion */
#define F_IO 8		/* Head at track, I/O in progress */
#define F_NXIO 9	/* Unit does not exist */
#define F_RESET 10	/* Reset in progress */

/*
 * event values for state machine
 */
#define FEV_TIME 1	/* Timer has popped */
#define FEV_INTR 2	/* Received interrupt */
#define FEV_WORK 3	/* More work has arrived */
#define FEV_CLOSED 4	/* All done--an event so we can allow floppy switch */

/*
 * State transition table
 */
struct state {
	int s_state;		/* Current state */
	int s_event;		/* Event */
	voidfun s_fun;		/* Action */
	int s_next;		/* New state */
};

/*
 * I/O ports used.  Sadly, we straddle two ranges--one for the FDC,
 * and another for the DMAC.
 */
#include <mach/dma.h>
#define FD_LOW DMA_LOW

#define FD_MOTOR 0x3F2
#define FD_STATUS 0x3F4
#define FD_DATA 0x3F5

#define FD_HIGH FD_DATA

/*
 * FDC commands
 */
#define FDC_RECAL 0x7
#define FDC_SEEK 0xF
#define FDC_READ 0xE6
#define FDC_WRITE 0xC5

/*
 * Bits for FD_MOTOR
 */
#define FD_MOTMASK 0x10		/* Starting here, bits for motors */
#define FD_INTR 0x0C		/* Enable interrupts */
#define FD_UNITMASK 0x3		/* Mask for unit #'s */

/*
 * Bits for FD_STATUS
 */
#define F_MASTER 0x80		/* Whether he's listening */
#define F_DIR 0x40		/* Direction of data */

/*
 * How long to wait for motor spinup
 */
#define MOT_TIME 3

/*
 * Interrupt, DMA vectors
 */
#define FD_IRQ 6	/* Hardware IRQ6==interrupt vector 14 */
#define FD_DRQ 2	/*  ...DMA channel 2 */

/*
 * Basic unit of transfer
 */
#define SECSZ (512)

/*
 * Structure for per-connection operations
 */
struct file {
	int f_sender;	/* Sender of current operation */
	uint f_flags;	/* User access bits */
	struct llist	/* When operation pending on this file */
		*f_list;
	ushort f_unit;	/* Unit we want */
	uint f_blkno;	/* Block # for operation */
	int f_cyl;	/*  ...cylinder it's on */
	int f_head;	/*  ...head */
	uint f_count;	/* # bytes wanted for current op */
	uint f_off;	/* Offset within current buffer */
	ushort f_dir;	/* FS_READ, FS_WRITE */
	ushort f_nseg;	/* # segments for I/O */
	seg_t		/*  ...the segments */
		f_seg[MSGSEGS];
	off_t f_pos;		/* Offset into device */
};

/*
 * Constants for manipulating configuration information
 * on floppy drives.
 */
#define NV_FDPORT 0x10		/* Slot # for FD info */
#define TYMASK 0xF0		/* Mask for density */
#define FDNONE 0x00		/* Density: not present, 1.2, 1.44 Mb */
#define FD12 0x20
#define FD144 0x40

#endif /* _FD_H */
