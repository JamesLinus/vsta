#ifndef _BOOT_H
#define _BOOT_H
/*
 * boot.h
 *	Some defines for the boot loader
 */
#include <stdio.h>

typedef unsigned long ulong;
typedef unsigned int uint;
typedef unsigned short ushort;
typedef unsigned char uchar;
#define K (1024)
#define M ((long)K*(long)K)
#define NBPG (4*K)

/*
 * This is what lives at the front of a djgpp-generated i386 binary
 * which we'll load as the VSTa kernel.
 */
struct aouthdr {
	ushort a_magic;		/* Magic # */
	uchar a_mach;		/* Machine type */
	uchar a_flags;		/* Misc. flags */
	ulong a_text;		/* Length of text, in bytes */
	ulong a_data;		/* Length of data, in bytes */
	ulong a_bss;		/* Length of BSS, in bytes */
	ulong a_syms;		/* Length of symbol info--not used */
	ulong a_entry;		/* Start address */
	ulong a_trsize;		/* Reloc info--not used */
	ulong a_drsize;
};

/*
 * VSTa must boot 413 formats--keeping everything page aligned and
 * data and text distinct is very helpful.
 */
#define VMAGIC 0413

extern void *lptr(ulong), setup_ptes(ulong), setup_gdt(void),
	load_image(FILE *);
extern void move_jump(void);
extern void load_kernel(struct aouthdr *, FILE *);
extern void lread(FILE *, ulong, ulong);
extern void lbzero(ulong, ulong), lbcopy(ulong, ulong, ulong);
extern ulong linptr(void *);
extern void set_args32(void);

#endif /* _BOOT_H */
