#ifndef _UTIME_H
#define _UTIME_H
/*
 * utime.h
 *	Time stamp handling
 *
 * Gee, if there's something we need more of it's time-oriented header
 * files and APIs.  So here's another one.
 */
#include <time.h>

struct utimbuf {
	time_t actime;		/* Access time */
	time_t modtime;		/* Modification time */
};
#endif /* _UTIME_H */
