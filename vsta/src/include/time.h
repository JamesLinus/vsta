#ifndef _TIME_H
#define _TIME_H
/*
 * time.h
 *	Interface for timing information
 *
 * Some of this based on technology:
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
 */
typedef long time_t;

struct tm {
	int	tm_sec;		/* seconds after the minute [0-60] */
	int	tm_min;		/* minutes after the hour [0-59] */
	int	tm_hour;	/* hours since midnight [0-23] */
	int	tm_mday;	/* day of the month [1-31] */
	int	tm_mon;		/* months since January [0-11] */
	int	tm_year;	/* years since 1900 */
	int	tm_wday;	/* days since Sunday [0-6] */
	int	tm_yday;	/* days since January 1 [0-365] */
	int	tm_isdst;	/* Daylight Savings Time flag */
	long	tm_gmtoff;	/* offset from CUT in seconds */
	char	*tm_zone;	/* timezone abbreviation */
};

/*
 * Some prototypes
 */
extern char *ctime(time_t *);
extern struct tm *gmtime(time_t *), *localtime(time_t *);
extern int __usleep(uint);
extern int __msleep(uint);

#endif /* _TIME_H */
