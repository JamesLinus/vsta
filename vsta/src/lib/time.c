/*
 * time.c
 *	Time-oriented services
 */
#include <sys/types.h>

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
__usleep(uint usecs)
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
__msleep(uint msecs)
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
 * ctime()
 *	Give printed string version of time
 *
 * Always in GMT.  Get a clue, it's all one big world.
 */
char *
ctime(long *lp)
{
	int day, month, year, dow, hr, min, sec;
	long l = *lp, len;
	static char timebuf[32];

	/*
	 * Take off years until we reach the desired one
	 */
	year = 1990;
	for (;;) {
		if (leap(year)) {
			len = 366 * DAYSECS;
		} else {
			len = 365 * DAYSECS;
		}
		if (l < len) {
			break;
		}
		l -= len;
		year += 1;
	}

	/*
	 * Take off months until we reach the desired month
	 */
	if (leap(year)) {
		month_len[1] = 29;	/* Feb. on leap year */
	}
	month = 0;
	for (;;) {
		len = month_len[month] * DAYSECS;
		if (l < len) {
			break;
		}
		l -= len;
		month += 1;
	}

	/*
	 * Figure the day
	 */
	day = l/DAYSECS;
	l = l % DAYSECS;

	/*
	 * Hour/minute/second
	 */
	hr = l / HRSECS;
	l = l % HRSECS;
	min = l / 60;
	l = l % 60;
	sec = l;

	/*
	 * Day of week is easier, at least until we get leap weeks
	 * where you get, say, two tuesdays in a row.  Hmmm... or
	 * two saturdays. :-)
	 *
	 * 1990 started on a monday.
	 */
	dow = *lp / DAYSECS;
	dow += 1;
	dow %= 7;

	/*
	 * Print it all into a buffer
	 */
	sprintf(timebuf, "%s %s %d, %d  %02d:%02d:%02d\n",
		days[dow], months[month], day+1, year,
		hr, min, sec);
	return(timebuf);
}
