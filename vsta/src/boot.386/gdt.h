#ifndef _GDT_H
#define _GDT_H
/*
 * gdt.h
 *	Definitions for Global Descriptor Table and friends
 */

/*
 * Memory and System segment descriptors
 */
struct seg {
	uint seg_limit0 : 16;	/* Size 0 */
	uint seg_base0 : 16;	/* Base 0 */
	uint seg_base1 : 8;	/* Base 1 */
	uint seg_type : 5;	/* Type */
	uint seg_dpl : 2;	/* Priv level */
	uint seg_p : 1;		/* Present */
	uint seg_limit1 : 4;	/* Size 1 */
	uint seg_pad0 : 2;	/* Pad */
	uint seg_32 : 1;	/* 32-bit size? */
	uint seg_gran : 1;	/* Granularity (pages if set) */
	uint seg_base2 : 8;	/* Base 2 */
};

/*
 * Gate descriptors.
 */
struct gate {
	uint gd_off0 : 16;	/* Offset 0 */
	uint gd_sel : 16;	/* Selector */
	uint gd_stkwds : 5;	/* Stack words to copy (always 0) */
	uint gd_pad0 : 3;	/* Pad */
	uint gd_type : 5;	/* Type */
	uint gd_dpl : 2;	/* Priv level */
	uint gd_p : 1;		/* Present */
	uint gd_off1 : 16;	/* Offset 1 */
};

/* Segment types used in VSTa */
#define	T_INVAL		 0	/* Invalid */
#define	T_LDT		 2	/* LDT */
#define	T_TASK		 5	/* Task gate (struct gate) */
#define	T_TSS		 9	/* TSS */
#define	T_CALL		12	/* Call gate (struct gate) */
#define	T_INTR		14	/* Interrupt gate (struct gate) */
#define	T_TRAP		15	/* Trap gate (struct gate) */
#define	T_MEMRO		16	/* Read only */
#define	T_MEMRW		18	/* Read+write */
#define	T_MEMX		24	/* Execute only */
#define	T_MEMXR		26	/* Execute+read */

/*
 * Linear memory description for lgdt and lidt instructions
 */
struct linmem {
	ushort l_len;	/* Length */
	ulong l_addr;	/* Address  */
};

/*
 * Layout of Global Descriptor Table
 */
#define GDT_NULL (0 << 3)
#define GDT_KDATA (1 << 3)
#define GDT_KTEXT (2 << 3)
#define GDT_BOOT32 (3 << 3)
#define GDT_TMPTSS (4 << 3)
#define NGDT 5			/* # entries in GDT */

/*
 * Convert descriptor value into GDT index
 */
#define GDTIDX(sel) ((unsigned)(sel) >> 3)

#endif /* _GDT_H */
