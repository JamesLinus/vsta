#ifndef _BOOT_H
#define _BOOT_H
/*
 * boot.h
 *	Cruft needed only during boot
 */
#include <sys/types.h>

/*
 * This describes a boot task which was loaded along with the kernel
 * during IPL.  The machine-dependent code figures out where they
 * are, and fills in an array of these to describe them in a portable
 * way.
 */
struct boot_task {
	uint b_pc;		/* Starting program counter */
	void *b_textaddr;	/* Address of text */
	uint b_text;		/* # pages of text */
	void *b_dataaddr;	/* Address of data */
	uint b_data;		/* # pages data */
	uint b_pfn;		/* Physical address of task */
};
#ifdef KERNEL
extern struct boot_task *boot_tasks;
extern uint nboot_task;
#endif

#endif /* _BOOT_H */
