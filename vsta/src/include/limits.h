#ifndef _LIMITS_H
#define _LIMITS_H
/*
 * limits.h
 *	Machine independent OS limits
 */

/*
 * Max path length
 */
#define _POSIX_PATH_MAX (256)

/*
 * Nest in machine specific ones now
 */
#include <mach/limits.h>

#endif /* _LIMITS_H */
