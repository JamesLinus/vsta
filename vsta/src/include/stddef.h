#ifndef _STDDEF_H
#define _STDDEF_H
/*
 * stddef.h
 *	More POSIX definition goop
 */

/*
 * NULL pointer
 */
#ifndef NULL
#define NULL (0)
#endif

/*
 * size_t, also defined in sys/types.h
 */
#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned long size_t;
#endif

/*
 * Difference between pointers--an int.  Doesn't C guarantee this?
 */
typedef int ptrdiff_t;

#endif /* _STDDEF_H */
