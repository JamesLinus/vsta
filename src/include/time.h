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
#include <sys/types.h>

/*
 * The difference between VSTa time and UNIX time is 20 years.  VSTa starts
 * ticking from 1990 not 1970.  Additionally, MS-DOS started ticking in 1980
 * so we have a constant for the difference there as well.  Note that as
 * well as leap years there were also leap seconds as well :-)  If you're
 * interested in the leap seconds, RFC1305 (NTP) provides more detail (we
 * ignore them here though)
 */
#define VSTA_UNIX_TDIFF (631152000)
#define VSTA_DOS_TDIFF (315619200)
#define DOS_UNIX_TDIFF (VSTA_UNIX_TDIFF - VSTA_DOS_TDIFF)

/*
 * Miscellaneous useful timekeeping constants
 */
#define SECSPERMIN	60
#define MINSPERHOUR	60
#define HOURSPERDAY	24
#define DAYSPERWEEK	7
#define DAYSPERNYEAR	365
#define DAYSPERLYEAR	366
#define SECSPERHOUR	(SECSPERMIN * MINSPERHOUR)
#define SECSPERDAY	((long) SECSPERHOUR * HOURSPERDAY)
#define MONSPERYEAR	12

#define TM_SUNDAY	0
#define TM_MONDAY	1
#define TM_TUESDAY	2
#define TM_WEDNESDAY	3
#define TM_THURSDAY	4
#define TM_FRIDAY	5
#define TM_SATURDAY	6

#define TM_JANUARY	0
#define TM_FEBRUARY	1
#define TM_MARCH	2
#define TM_APRIL	3
#define TM_MAY		4
#define TM_JUNE		5
#define TM_JULY		6
#define TM_AUGUST	7
#define TM_SEPTEMBER	8
#define TM_OCTOBER	9
#define TM_NOVEMBER	10
#define TM_DECEMBER	11

#define TM_YEAR_BASE	1900

#define EPOCH_YEAR	1990
#define TIMEEND_YEAR	2057
#define EPOCH_WDAY	TM_MONDAY

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

struct timeval {
	time_t	tv_sec;		/* seconds */
	time_t	tv_usec;	/* microseconds */
};

struct timezone {
	int	tz_minuteswest;	/* minutes west of Greenwich */
	int	tz_dsttime;	/* type of daylight saving correction */
};

/*
 * Some prototypes
 */
extern char *ctime(time_t *);
extern char *asctime(const struct tm *);
extern struct tm *gmtime(time_t *);
extern struct tm *localtime(time_t *);
extern time_t mktime(struct tm *);
extern int __usleep(uint);
extern int __msleep(uint);
extern time_t time(time_t *);
extern int utimes(const char *, struct timeval *);
extern int gettimeofday(struct timeval *, const char *);
extern size_t strftime(char *, size_t, const char *, const struct tm *);
extern void tzset(void);
extern void tzsetwall(void);

/*
 * clock() API from ANSI
 */
#include <mach/param.h>
#define CLOCKS_PER_SEC (HZ)
typedef unsigned int clock_t;
extern clock_t clock(void);

/*
 * isleap()
 *	Returns 1 if the year is a leap year, 0 otherwise
 *
 * This will break in 2100, but I'm not about to lose any sleep over
 * that for a long time :-)
 */
#define isleap(year) ((year & 3) == 0 ? 1 : 0)

/*
 * tzname is a POSIX externally visible variable.  It "should" be an
 * array of two char pointers; instead, our shared library cobbles
 * up this data structure and returns a pointer to it.
 */
extern char **__get_tzname(void);
#define tzname (__get_tzname())

#endif /* _TIME_H */
