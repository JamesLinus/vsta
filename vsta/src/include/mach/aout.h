#ifndef _AOUT_H
#define _AOUT_H
/*
 * aout.h
 *	A very abbreviated notion of what an a.out header looks like
 */
#include <sys/types.h>

struct aout {
unsigned long
	a_info,		/* Random stuff, already checked */
	a_text,		/* length of text in bytes */
	a_data,		/* length of data in bytes */
	a_bss,		/* length of bss, in bytes */
	a_syms,		/* symbol stuff, ignore */
	a_entry,	/* entry point */
	a_trsize,	/* relocation stuff, ignore */
	a_drsize;	/*  ...ditto */
};

#endif /* _AOUT_H */
