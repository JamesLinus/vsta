#ifndef _GDT_H
#define _GDT_H
/*
 * gdt.h
 *	Definitions for Global Descriptor Table and friends
 */
#include <sys/types.h>

/*
 * Selectors XXX move to mach/vm.h?
 */
#define	PRIV_MASK ((s) & 3)	/* Privilege bits in a selector */
#define	PRIV_KERN 0		/*  ...kernel */
#define	PRIV_USER 3		/*  ...user */

/*
 * Memory-type segment entries
 */
struct segment {
	uint seg_limit0 : 16;	/* Size 0 */
	uint seg_base0 : 24;	/* Base 0 */
	uint seg_type : 5;	/* Type */
	uint seg_dpl : 2;	/* Priv level */
	uint seg_p : 1;		/* Present */
	uint seg_limit1 : 4;	/* Size 1 */
	uint seg_pad0 : 2;	/* Pad */
	uint seg_32 : 1;	/* 32-bit size? */
	uint seg_gran : 1;	/* Granularity (pages if set) */
	uint seg_base1 : 8;	/* Base 1 */
};

/*
 * Gateway-type segment entries
 */
struct gate {
	uint g_off0 : 16;	/* Offset 0 */
	uint g_sel : 16;	/* Selector */
	uint g_stkwds : 5;	/* Stack words to copy (always 0) */
	uint g_pad0 : 3;	/* Pad */
	uint g_type : 5;	/* Type */
	uint g_dpl : 2;		/* Priv level */
	uint g_p : 1;		/* Present */
	uint g_off1 : 16;	/* Offset 1 */
};

/* Segment types used in VSTa */
#define	T_INVAL		 0	/* Invalid */
#define	T_LDT		 2	/* LDT */
#define	T_TASK		 5	/* task gate */
#define	T_TSS		 9	/* TSS */
#define	T_CALL		12	/* call gate */
#define	T_INTR		14	/* interrupt gate */
#define	T_TRAP		15	/* trap gate */
#define	T_MEMRO		16	/* read only */
#define	T_MEMRW		18	/* read+write */
#define	T_MEMX		24	/* execute only */
#define	T_MEMXR		26	/* execute+read */

/*
 * Linear memory description for lgdt and lidt instructions.  The
 * compiler tries to put l_addr on a long boundary, so you must
 * use &l.l_len as the argument to lgdt() and friends.
 */
struct linmem {
	ushort l_pad;	/* Pad XXX */
	ushort l_len;	/* Length */
	ulong l_addr;	/* Address  */
};

#define	NIDT	256	/* Total entries in IDT table */
#define	CPUIDT	32	/* This many for CPU (come first) */
#define IDTISA	16	/* This many at CPUIDT used for ISA intrs */

#endif /* _GDT_H */
