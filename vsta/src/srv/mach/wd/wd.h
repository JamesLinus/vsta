#ifndef _WD_H
#define _WD_H
/*
 * wd.h
 *	Western Digital ST-506 type hard disk interface definitions
 */
#include <sys/types.h>
#include <sys/perm.h>
#include <mach/dpart.h>

#define NWD (2)			/* Max # WD units supported */

/*
 * Undefine this if your WD controller can't switch heads by itself.
 * Most can, and it's much more efficient, so we leave it on by default.
 */
#define AUTO_HEAD

#define SECSZ (512)		/* Only 512 byte sectors handled */
#define MAXIO (128*1024)	/* Max I/O--128K */

#define WD1_PORT 0x1f0		/* I/O ports here */
#define WD2_PORT 0x170		/* Secondary I/O ports start here */
#define WD1_IRQ	14		/* IRQ number for hard disk ctrl */
#define WD2_IRQ	15		/* Usual IRQ number for second HDD ctrl */

/*
 * Registers on WD controller
 */
#define WD_DATA		0	/* Data */
#define WD_ERROR	1	/* Error */
#define WD_SCNT		2	/* Sector count */
#define WD_SNUM		3	/* Sector number */
#define WD_CYL0		4	/* Cylinder, low 8 bits */
#define WD_CYL1		5	/*  ...high 8 bits */
#define WD_SDH		6	/* Sector size, drive and head */
#define WD_STATUS	7	/* Command or immediate status */
#define WD_CMD		7
#define WD_CTLR		0x206	/* Controller port */

/*
 * Status bits
 */
#define WDS_ERROR	0x1	/* Error */
#define WDS_ECC		0x4	/* Soft ECC error */
#define WDS_DRQ		0x8	/* Data request */
#define WDS_BUSY	0x80	/* Busy */

/*
 * Parameters for WD_SDH
 */
#define WDSDH_512	0x20
#define WDSDH_EXT	0x80

/*
 * Bits for controller port
 */
#define CTLR_IDIS 0x2		/* Disable controller interrupts */
#define CTLR_RESET 0x4		/* Reset controller */
#define CTLR_4BIT 0x8		/* Use four head bits */

/*
 * Commands
 */
#define WDC_READ	0x20	/* Command: read */
#define WDC_WRITE	0x30	/*  ...write */
#define WDC_READP	0xEC	/*  ...read parameters */
#define WDC_DIAG	0x90	/* Run controller diags */
#define WDC_SPECIFY	0x91	/* Initialise controller parameters */

/*
 * Read parameters command (WDC_READP) returns this.  I think this
 * struct comes from CMU Mach originally.
 */
struct wdparameters {

	/* drive info */
	ushort	w_config;		/* general configuration */
	ushort	w_fixedcyl;		/* number of non-removable cylinders */
	ushort	w_removcyl;		/* number of removable cylinders */
	ushort	w_heads;		/* number of heads */
	ushort	w_unfbytespertrk;	/* number of unformatted bytes/track */
	ushort	w_unfbytes;		/* number of unformatted bytes/sector */
	ushort	w_sectors;		/* number of sectors */
	ushort	w_minisg;		/* minimum bytes in inter-sector gap*/
	ushort	w_minplo;		/* minimum bytes in postamble */
	ushort	w_vendstat;		/* number of words of vendor status */

	/* controller info */
	char	w_cnsn[20];		/* controller serial number */
	ushort	w_cntype;		/* controller type */
#define	WDTYPE_SINGLEPORTSECTOR	1	 /* single port, single sector buffer */
#define	WDTYPE_DUALPORTMULTI	2	 /* dual port, multiple sector buffer */
#define	WDTYPE_DUALPORTMULTICACHE 3	 /* above plus track cache */
	ushort	w_cnsbsz;		/* sector buffer size, in sectors */
	ushort	w_necc;			/* ecc bytes appended */
	char	w_rev[8];		/* firmware revision */
	char	w_model[40];		/* model name */
	ushort	w_nsecperint;		/* sectors per interrupt */
	ushort	w_usedmovsd;		/* can use double word read/write? */
};

/*
 * This is our post-massage version of wdparms.  It contains things
 * precalculated into the shape we'll need.
 */
struct wdparms {
	uint w_cyls;		/* = w_fixedcyl+w->w_removcyl */
	uint w_tracks;		/* = w_heads */
	uint w_secpertrk;	/* = w_sectors */
	uint w_secpercyl;	/* = w_heads * w_sectors */
	uint w_size;		/* = w_secpercyl * w_cyls */
};

/*
 * Values for f_nodes
 */
#define ROOTDIR (-1)		/* Root */
#define UNITSTEP (0x10)		/* Node number step between units */ 
#define NODE_UNIT(n) ((n >> 4) & 0xFF)
				/* Unit number */
#define NODE_SLOT(n) (n & 0xF)	/* Partition in unit */
#define MKNODE(unit, slot) ((((unit) & 0xFF) << 4) | (slot))
				/* Merge unit and slot to make node number */
/*
 * State of an open file
 */
struct file {
	long f_sender;		/* Sender of current operation */
	uint f_flags;		/* User access bits */
	struct llist		/* When operation pending on this file */
		*f_list;
	int f_node;		/* Current "directory" */
	ushort f_unit;		/* Unit we want */
	ushort f_abort;		/* Abort requested */
	uint f_blkno;		/* Block # for operation */
	uint f_count;		/* # bytes wanted for current op */
	ushort f_op;		/* FS_READ, FS_WRITE */
	void *f_buf;		/* Base of buffer for operation */
	off_t f_pos;		/* Offset into device */
	int f_local;		/* f_buf is a local buffer */
};

/*
 * State of a disk
 */
struct disk {
	struct part		/* Partition details */
		*d_parts[MAX_PARTS];
	struct wdparms d_parm;	/* Disk "physical" parameters */
	int d_configed;		/* Is the disk configured */
};

/*
 * Function prototypes for wd.c
 */
extern void wd_init(int argc, char **argv);
extern int wd_io(int op, void *handle, uint unit,
		 ulong secnum, void *va, uint secs);
extern void wd_isr(void);

/*
 * Function prototypes for rw.c
 */
extern void wd_rw(struct msg *m, struct file *f);
extern void iodone(void *tran, int result);
extern void rw_init(void);
extern void rw_readpartitions(int unit);

/*
 * Function prototypes for stat.c
 */
extern void wd_stat(struct msg *m, struct file *f);
extern void wd_wstat(struct msg *m, struct file *f);

/*
 * Function prototypes for dir.c
 */
extern void wd_readdir(struct msg *m, struct file *f);
extern void wd_open(struct msg *m, struct file *f);

/*
 * Global data
 */
extern uint first_unit,		/* Lowest unit # configured */
	partundef;		/* All partitioning read yet? */
extern struct disk disks[];
extern struct prot wd_prot;
extern int wd_irq, wd_baseio;
extern port_name wdname;
extern char wd_namer_name[];

#endif /* _WD_H */
