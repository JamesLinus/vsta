/*
 * cdfstime.c - time conversion functions.
 */
#include <time.h>
#include "cdfs.h"

#define	IS_LEAP(year)	((year % 4 == 0) && (!(year % 100 == 0) || \
			 (year % 400 == 0)))

int	cdfs_cvt_date(union iso_date *date, int fmt, int hs, int base_year,
	              time_t *time)
{
	int	year, month, day, hour, minute, second, tz, direction;
	static	int mtod[] = { 0, 31, 59, 90, 120, 151, 181, 212,
		               243, 273, 304, 334 };

	if(fmt == 7) {
		year = date->date7.year + 1900;
		month = date->date7.month;
/*
 * Convert day from one based to zero based.
 */
		day = date->date7.day - 1;
		hour = date->date7.hour;
		minute = date->date7.minute;
		second = date->date7.second;
		if(hs)
			tz = 0;
		else
			tz = (int)date->date7.tz;
	} else
		return(0);

	direction = (year < base_year ? 1 : -1);
	day += mtod[month - 1] + ((IS_LEAP(year) && (month > 2)) ? 1 : 0);
	while(year != base_year) {
		year += direction;
		day += 365 + (IS_LEAP(year) ? 1 : 0);
	}
	*time = (day * 24 * 60 * 60) + (hour * 60 * 60) +
	         ((minute - (tz * 15)) * 60) + second;
/*
 * For years less than the base year, negate the time value.
 */
	if(direction == 1)
		*time *= -1;

	return(1);
}
