#ifndef _SYS_TIMEB_H
#define _SYS_TIMEB_H

/*
 * timeb.h
 *	Time implementation for ftime()
 */
#include <time.h>
 
struct timeb {
	time_t time;
	unsigned short millitm;
	short timezone;
	short dstflag;
};

extern int ftime(struct timeb *__tp);

#endif /* _SYS_TIMEB_H */
