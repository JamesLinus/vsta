/*
 * param.h
 *	Global parameters
 */
#ifndef _MACHPARAM_H
#define _MACHPARAM_H

/*
 * Values and macros relating to hardware pages
 */
#define NBPG (4096)	/* # bytes in a page */
#define PGSHIFT (12)	/* LOG2(NBPG) */
#define ptob(x) ((ulong)(x) << PGSHIFT)
#define btop(x) ((ulong)(x) >> PGSHIFT)
#define btorp(x) (((ulong)(x) + (NBPG-1)) >> PGSHIFT)

/*
 * How often our clock "ticks"
 */
#define HZ (20)

/*
 * Which console device to use for printf() and such
 */
#undef SERIAL		/* Define for COM1, else use screen */

#endif /* _MACHPARAM_H */
