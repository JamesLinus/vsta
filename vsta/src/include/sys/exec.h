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

#ifndef KERNEL
/*
 * The exec() system call
 */
extern int exec(port_t, struct mapfile *, void *);
#endif

/*
 * The third argument to exec() is defined by the C library.  It is
 * documented here even though the kernel knows nothing about it.
 * Sue me.
 *
 * Three things need to be passed from the old instance of the
 * process to the new.  First, a command line, which is mapped into
 * the argc/argv format on the receiving end.  Second, the state of
 * the file descriptor layer.  Finally, the state of the mount table.
 * All data is passed in an mmap()'ed sharable region; all data is
 * packed together end-to-end.
 *
 * The command lines is passed as an unsigned long count, followed by
 * that many null-terminated strings.
 *
 * The file descriptor layer is passed as an unsigned long count of
 * the number file descriptors, followed by that many pairs of
 * <int, port_t>'s, indicating the association of a file descriptor
 * to a port.  Port state is then initialized as if this was the first
 * open on the file descriptor.  XXX it would be very ugly to pass
 * full state; would it be worth it?
 *
 * The mount table is passed as an unsigned long count of the number
 * of mount table slots.  Each slot is represented by an unsigned
 * long count of the number of entries under the slot.  Each entry
 * is a port_t, corresponding to an open connection to a filesystem
 * server.
 */

#endif /* _EXEC_H */
