#ifndef _RMAP_H
#define _RMAP_H
/*
 * rmap.h
 *	A resource map
 *
 * A resource map is represented as an open array.  You get an array
 * of X of them, and then you pass it and the count to rmap_init().
 */
#include <sys/types.h>

struct rmap {
	uint r_off;
	uint r_size;
};

/*
 * Routines you can call
 */
extern void rmap_init(struct rmap *, uint);
extern uint rmap_alloc(struct rmap *, uint);
extern void rmap_free(struct rmap *, uint, uint);
extern int rmap_grab(struct rmap *, uint, uint);

#endif /* _RMAP_H */
