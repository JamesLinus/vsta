#ifndef _TSS_H
#define _TSS_H
/*
 * tss.h
 *	Definition of i386 Task State Segment
 *
 * VSTa doesn't use these much.  We need a junk one to switch into
 * 32-bit mode.  We then just need one so we can switch between priv
 * level 0 and 3 and back.
 *
 * It's faster to switch with pushal and diddling of CR3 than to
 * use the hardware task switch.
 */
#include <sys/types.h>

/* Task State Segment */
struct tss {
	ulong	link;
	ulong	esp0, ss0;
	ulong	esp1, ss1;
	ulong	esp2, ss2;
	ulong	cr3;
	ulong	eip;
	ulong	eflags;
	ulong	eax, ecx, edx, ebx, esp, ebp;
	ulong	esi, edi;
	ulong	es, cs, ss, ds, fs, gs;
	ulong	ldt;
	ulong	iomap;
};

#endif /* _TSS_H */
