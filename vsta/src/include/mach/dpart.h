#ifndef _DPART_H
#define _DPART_H
/*
 * dpart.h
 *	IBM PC disk partition information handling
 *
 * The partition information detailed here is that expected by the PC's BIOS
 * and most (all?) PC operating systems.
 */
#include <sys/types.h>
#include <sys/perm.h>

/*
 * Values for f_nodes used in disk/partition name tables
 */
#define WHOLE_DISK (0x0)	/* Partition slot for the whole disk */
#define FIRST_PART (0x1)	/* First partition entry slot */
#define LAST_PART (0xf)		/* Last partition entry slot */
#define NPART (4)		/* Max num of part'ns in a partition table */
#define MAX_PARTS (16)		/* Max number pf part'ns per drive */

/*
 * Shape of a partition
 */
struct part {
	char p_name[16];	/* Symbolic name */
	ulong p_off;		/* Sector offset */
	ulong p_len;		/*  ...length */
	uint p_extoffs;		/*  ...from base ext partition */
	int p_val;		/* Valid slot? */
	struct prot p_prot;	/* Protection for partition */
	uint p_type;		/* Record of ps_type below */
};

/*
 * Structure of each slot
 */
struct part_slot {
	uchar ps_boot;		/* 0x80 == bootable, 0 == inactive */
	uchar ps_head;		/* Start head */
	uchar ps_sec;		/*  ...sector */
	uchar ps_cyl;		/*  ...cylinder */
	uchar ps_type;		/* Type of partition */
	uchar ps_ehead;		/* End head */
	uchar ps_esec;		/*  ...sector */
	uchar ps_ecyl;		/*  ...cylinder */
	ulong ps_start;		/* Sector # for start of partition */
	ulong ps_len;		/*  ...length in sectors */
};


/*
 * Values for ps_type
 */
#define PT_DOS12 0x01		/* DOS 12-bit FAT fs */
#define PT_DOS16 0x04		/* DOS 16-bit FAT fs */
#define PT_EXT 0x05		/* Extended partition */
#define PT_DOSBIG 0x06		/* DOS > 32 Mb 16-bit FAT fs */
#define PT_OS2HPFS 0x07		/* OS/2 HPFS fs*/
#define PT_386BSD 0xa5		/* 386BSD McKusick-type fs */
#define PT_MINIX 0x80		/* Old style Minix fs */
#define PT_LXMINIX 0x81		/* Linux/Minix fs */
#define PT_LXSWAP 0x82		/* Linux swap partition */
#define PT_LXNAT 0x83		/* Linux native fs, eg ext2 */
#define PT_VSBFS 0x9e		/* VSTa boot fs */
#define PT_VSSWAP 0x9f		/* VSTa swap partition */

/*
 * Macros to access partition info within a SECSZ block
 */
#define PART(buf, idx) ((struct part_slot *)((char *)(buf) + \
	0x1be + (sizeof(struct part_slot) * (idx))))
#define SIG(buf, idx) (((unsigned char *)(buf) + 0x1fe + (idx))[0])

/*
 * Library functions to access these data structures
 */
extern int dpart_init_whole(char *name, uint unit, uint sec_size,
			    struct prot *protection,
			    struct part *partition[]);
extern int dpart_init(char *name, uint unit, char *secbuf,
		      uint *sec_num, struct prot *protection,
		      struct part *partition[], int *next_part);
extern int dpart_get_offset(struct part *partition[], int part_slot, ulong off,
			    ulong *offp, uint *cntp);

#endif /* _DPART_H */
