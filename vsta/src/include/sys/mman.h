#ifndef _MMAN_H
#define _MMAN_H
/*
 * mman.h
 *	Definitions for mmap system call
 *
 * This name is inherited from the Mach-ish organization of mmap().
 * VSTa only supports a subset of their interface.
 */
#include <sys/types.h>
#include <mach/mman.h>

/*
 * Lots of code expects this type to be used
 */
typedef char *caddr_t;

/*
 * mmap()
 *	Map stuff
 */
extern void *mmap(caddr_t vaddr, ulong len, int prot, int flags,
	int fd, ulong offset);
extern int munmap(caddr_t vaddr, ulong len);
#ifdef KERNEL
extern void *add_map(struct vas *,
	struct portref *, caddr_t, ulong, ulong, int);
extern char *mach_page_wire(uint flags, struct pview *pv,
	struct perpage *pp, void *va, uint idx);
#endif /* KERNEL */

/*
 * Bits for prot
 */
#define PROT_EXEC (1)
#define PROT_READ (2)
#define PROT_WRITE (4)

/*
 * Bits for flags
 */
#define MAP_ANON (1)		/* fd not used--anonymous memory */
#define MAP_FILE (2)		/* map from fd */
#define MAP_FIXED (4)		/* must use vaddr or fail */
#define MAP_PRIVATE (8)		/* copy-on-write */
#define MAP_SHARED (16)		/* share same memory */
#define MAP_PHYS (32)		/* talk to physical memory */
#define MAP_NODEST (64)		/* leave memory segment across exec() */

/*
 * Physical DMA support stuff
 */
extern int page_wire(void *arg_va, void **arg_pa, uint flags);
extern int page_release(uint arg_handle);

/*
 * Flags for page_wire()
 */
#define WIRE_MACH1 0x01		/* Hooks for platform specific */
#define WIRE_MACH2 0x02		/*  handling. */
#define WIRE_MACH (WIRE_MACH1 | WIRE_MACH2)

#endif /* _MMAN_H */
