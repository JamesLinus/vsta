#ifndef _MALLOC_H
#define _MALLOC_H
/*
 * malloc.h
 *	Function defs for a malloc()/free() kernel interface
 */
#include <sys/types.h>
#include <sys/assert.h>

/*
 * Basic functions
 */
extern void *malloc(uint size);
extern void free(void *);

/*
 * Interface with provisions for future functionality.  If we have
 * to hunt memory leaks, this interface provides a little more
 * information.
 */
#define MALLOC(var, size, kind, flags) \
	ASSERT_DEBUG(flags != M_NOWAIT, "malloc: illegal flags"); \
	(var) = malloc(size);
#define FREE(var, kind) \
	free(var);

/*
 * Values for "kind"
 */
#define M_QIO 1		/* struct qio */
#define M_ATL 2		/* struct atl */
#define M_VAS 3		/* struct vas */
#define M_PVIEW 4	/* struct pview */
#define M_PSET 5	/* struct pset */
#define M_PPAGE 6	/* array of struct perpage under pset */

/*
 * Values for "flags"
 */
#define M_NOWAIT 1	/* Don't sleep--currently unsupported */
#define M_WAITOK 2	/* Sleeping is fine */

#endif /* _MALLOC_H */
