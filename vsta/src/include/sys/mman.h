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

/*
 * mmap()
 *	Map stuff
 */
extern void *mmap(void *vaddr, ulong len, int prot, int flags,
	int fd, ulong offset);
extern int munmap(void *vaddr, ulong len);
#ifdef KERNEL
extern void *add_map(struct vas *,
	struct portref *, void *, ulong, ulong, int);
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

#endif /* _MMAN_H */
