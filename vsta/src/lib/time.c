/*
 * time.c
 *	Time-oriented services
 */
#include <sys/types.h>
#include <time.h>

#define HRSECS (60*60)
#define DAYSECS (24*HRSECS)
static char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static char *months[] =
	{"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static int month_len[] =
	{31, 28, 31, 30,
	 31, 30, 31, 31,
	 30, 31, 30, 31};

/*
 * sleep()
 *	Suspend execution the given amount of time
 */
uint
sleep(uint secs)
{
	struct time t;

	time_get(&t);
	t.t_sec += secs;
	time_sleep(&t);
	return(0);
}

/*
 * usleep()
 *	Like sleep, but in microseconds
 */
__usleep(int usecs)
{
	struct time t;

	time_get(&t);
	t.t_usec += usecs;
	while (t.t_usec > 1000000) {
		t.t_sec += 1;
		t.t_usec -= 1000000;
	}
	time_sleep(&t);
	return(0);
}

/*
 * msleep()
 *	Like sleep, but milliseconds
 */
__msleep(int msecs)
{
	return(__usleep(msecs * 1000));
}

/*
 * time()
 *	Get time in seconds since 1990
 *
 * Yeah, I could've done it from 1970, but this gains me 20 years.
 * It also lets me skip some weirdness in the 70's, and even I think
 * in the 80's.  It should also piss off all the people who like
 * to write ~1500 lines of C just to tell the time.
 */
long
time(long *lp)
{
	struct time t;

	time_get(&t);
	if (lp) {
		*lp = t.t_sec;
	}
	return(t.t_sec);
}

/*
 * leap()
 *	Tell if year is the leap year
 */
static
leap(int year)
{
	return(((year - 1990) & 3) == 2);
}

/*
 * gmtime()
 *	Convert time to Greenwich
 */
struct tm *
gmtime(time_t *lp)
{
	static struct tm tm;
	time_t l = *lp;
	ulong len;

	/*
	 * Take off years until we reach the desired one
	 */
	tm.tm_year = 1990;
	for (;;) {
		if (leap(tm.tm_year)) {
			len = 366 * DAYSECS;
		} else {
			len = 365 * DAYSECS;
		}
		if (l < len) {
			break;
		}
		l -= len;
		tm.tm_year += 1;
	}

	/*
	 * Absolute count of days into year
	 */
	tm.tm_yday = l/DAYSECS;

	/*
	 * Take off months until we reach the desired month
	 */
	if (leap(tm.tm_year)) {
		month_len[1] = 29;	/* Feb. on leap year */
	}
	tm.tm_mon = 0;
	for (;;) {
		len = month_len[tm.tm_mon] * DAYSECS;
		if (l < len) {
			break;
		}
		l -= len;
		tm.tm_mon += 1;
	}

	/*
	 * Figure the day
	 */
	tm.tm_mday = l/DAYSECS;
	l = l % DAYSECS;

	/*
	 * Hour/minute/second
	 */
	tm.tm_hour = l / HRSECS;
	l = l % HRSECS;
	tm.tm_min = l / 60;
	l = l % 60;
	tm.tm_sec = l;

	/*
	 * Day of week is easier, at least until we get leap weeks
	 * where you get, say, two tuesdays in a row.  Hmmm... or
	 * two saturdays. :-)
	 *
	 * 1990 started on a monday.
	 */
	tm.tm_wday = *lp / DAYSECS;
	tm.tm_wday += 1;
	tm.tm_wday %= 7;

	/*
	 * Fluff
	 */
	tm.tm_isdst = 0;
	tm.tm_gmtoff = 0;
	tm.tm_zone = "GMT";

	/*
	 * Convert values to their defined basis
	 */
	tm.tm_mday += 1;
	tm.tm_year -= 1900;

	return(&tm);
}

/*
 * localtime()
 *	Just gmtime
 *
 * Yes, always in GMT.  Get a clue, it's all one big world.
 */
struct tm *
localtime(time_t *lp)
{
	return(gmtime(lp));
}

/*
 * ctime()
 *	Give printed string version of time
 */
char *
ctime(time_t *lp)
{
	register struct tm *tm;
	static char timebuf[32];

	/*
	 * Get basic time information
	 */
	tm = localtime(lp);

	/*
	 * Print it all into a buffer
	 */
	sprintf(timebuf, "%s %s %d, %d  %02d:%02d:%02d %s\n",
		days[tm->tm_wday], months[tm->tm_mon],
		tm->tm_mday, 1900 + tm->tm_year,
		tm->tm_hour, tm->tm_min, tm->tm_sec,
		tm->tm_zone);
	return(timebuf);
}

