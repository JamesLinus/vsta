#ifndef _EXEC_H
#define _EXEC_H
/*
 * exec.h
 *	Definitions for exec() kernel service
 *
 * Most of this stuff is just a portable way for the user to describe
 * the file he now wishes to run as a memory-mapped file.
 */
#include <sys/types.h>

/*
 * Description of a single mapping
 */
struct mapseg {
	void *m_vaddr;		/* Vaddr to map at */
	uint m_off;		/* Offset into file, in pages */
	uint m_len;		/*  ...length */
	uint m_flags;		/* See below */
};

/*
 * Bits in m_flags
 */
#define M_RO 1		/* Mapping should be read-only */
#define M_ZFOD 2	/*  ...not a real mapping; BSS */

/*
 * Second argument to exec()
 */
#define NMAP 4
struct mapfile {
	void *m_entry;			/* Starting PC value */
	struct mapseg m_map[NMAP];	/* The mappings requested */
};

#endif /* _EXEC_H */
