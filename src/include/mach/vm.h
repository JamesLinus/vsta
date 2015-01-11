#ifndef _MACHVM_H
#define _MACHVM_H
/*
 * vm.h
 *	Machine-dependent VM stuff
 *
 * Some basic facts to keep in mind.  On i386, a two-level page table
 * organization is used.  Each top slot (known as a level 1 PTE) points
 * down to a page of level 2 PTEs.  Each top slot is responsible for
 * 4 Mb of virtual address space; each level 2 PTE handles 4K of that.
 *
 * Our boot loader has arranged for the kernel text to reside at 0 (the
 * first page is invalid, so references to vaddr 0 will trap).  It has
 * filled in a couple slots of the L1PTEs, pushed the page frame number
 * (PFN) of the next free page on the stack, and called us.  See locore.s
 * for details on initial boot.
 *
 * On boot, the root page table is layed out as:
 * 0000: 1:1/text
 * 0001: data remapping
 * 0002: CR3 (so at 8 Mb you will see the page table memory linearly)
 * 0003: empty, but L2PTEs are present
 *
 * We convert slot 3 to be our utility mapping area.  We then map 0004 and
 * up to be a 1:1 mapping of all physical memory.
 *
 * To avoid the cost of swapping page table roots when entering the
 * kernel, we share a root page table with our user-mode threads.  The
 * user's segments are tweaked to map vaddr 0 to paddr 2 Gb.  The user's
 * stack(s) start at the top of his 2 Gb.  So indices 512 and 1023 of
 * our L1PTEs will be filled in.  Perhaps 513 and 1022, etc. if the
 * process is big enough.
 */
#include <sys/param.h>
#include <mach/pte.h>

#define BYTES_L1PT (1024*NBPG)		/* Bytes mapped per L1PTE */

#define L1PT_TEXT 0			/* Indices for L1PTE slots */
#define L1PT_DATA 1
#define L1PT_CR3 2		/* For addressing L2PTEs */
#define L1PT_UTIL 3		/* For vmap */
#define L1PT_FREE 4		/* 1:1 mapping of memory will start here */
#define L1PT_UTEXT 512
#define L1PT_USTACK 1023

/*
 * Layout of Global Descriptor Table:
 *
 * We don't use the LDT; all gates and such are via the GDT or IDT.
 */
#define GDT_NULL (0 << 3)
#define GDT_KDATA (1 << 3)
#define GDT_KTEXT (2 << 3)
#define GDT_BOOT32 (3 << 3)
#define GDT_UDATA (5 << 3)
#define GDT_UTEXT (6 << 3)
#define NGDT 7			/* # entries in GDT */
#define GDTIDX(i) ((i) >> 3)

/*
 * Fixed locations in user's virtual address space.  Note that from
 * the kernel's linear perspective, the user resides in quandrants
 * 2 and 3; from the user's perspective, he resides in 0 and 1.
 * The following addresses are from the user perspective; this is
 * what is used for pview vaddrs and such.
 * UBASE--what user thinks is his lowest virtual address
 * UOFF--offset to convert from user to kernel perspective
 * USTACKADDR--base address at which stack starts.  esp will start
 *  at the top of this, and grow downwards.
 */
#define UBASE (0x0)
#define UOFF (0x80000000)
#define USTACKADDR (0x80000000-UMAXSTACK)

/*
 * Address of utility map used by hat_attach() on i386
 */
#define VMAP_BASE (0x40000000)	/* Map area starts at 1 Gb */
#define VMAP_SIZE (0x20000000)	/*  ...for 1/2 Gb */

/*
 * Address of attach point for shared libraries
 */
#define SHLIB_BASE (VMAP_BASE + VMAP_SIZE)

#endif /* _MACHVM_H */
