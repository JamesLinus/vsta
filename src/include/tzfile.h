#ifndef _TZFILE_H
#define _TZFILE_H

/*
 * tzfile.h
 *	Timezone file information
 *
 * Much of the code in this file is distributed under the BSD copyright.
 * Please see the accompanying library file time.c in the VSTa C library.
 *
 * This isn't exactly the same as the BSD code though, as some of the
 * constants are declared in <time.h> in VSTa - they fit there more neatly.
 * It's also modified to support 1990 as the EPOCH_YEAR and to use VSTa
 * default paths.
 */

/*
 * Information about zone files
 */
#define TZDIR "/vsta/lib/zoneinfo"
#define TZDEFAULT "localtime"
#define TZDEFRULES "posixrules"

/*
 * Each timezone file begins with the following header
 */
struct tzhead {
	char tzh_reserved[24];	/* reserved for future use */
	char tzh_ttisstdcnt[4];	/* coded number of trans. time flags */
	char tzh_leapcnt[4];	/* coded number of leap seconds */
	char tzh_timecnt[4];	/* coded number of transition times */
	char tzh_typecnt[4];	/* coded number of local time types */
	char tzh_charcnt[4];	/* coded number of abbr. chars */
};

#define TZ_MAX_TIMES 370	/* Large enough to handle just over 1 year */
#define TZ_MAX_TYPES 20		/* Max number of local time types */
#define TZ_MAX_CHARS 50		/* Max number of abbreviation characters */
#define TZ_MAX_LEAPS 50		/* Max number of leap second corrections */

#endif /* _TZFILE_H */
