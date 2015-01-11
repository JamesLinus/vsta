#ifndef MAP_H
#define MAP_H
/*
 * map.h
 *	Data structures for defining ranges of a file
 *
 * Various virtual addresses map to different parts of a file (or
 * memory, for that matter).  A map structure describes a virtual
 * address, length, and corresponding offset.
 */
#include <sys/types.h>

#define NMAP (4)		/* # mapping ranges handled */

/*
 * An entry within a map
 */
struct mslot {
	void *m_addr;		/* Starting addr */
	ulong m_len,		/* Length (bytes) */
		m_off;		/* Offset (bytes) */
};

/*
 * A map
 */
struct map {
	uint m_nmap;
	struct mslot m_map[NMAP];
};

/*
 * Routines
 */
extern struct map *alloc_map(void);
extern int add_map(struct map *, void *, ulong, ulong),
	do_map(const struct map *, const void *, ulong *);
extern void map_aout(struct map *);

#endif MAP_H
