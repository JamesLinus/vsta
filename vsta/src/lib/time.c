/*
 * time.c
 *	Time-oriented services
 *
 * Much of the code in this file is distributed under the BSD copyright
 * below (most of the timezone handling).  The remainder was written
 * specifically for VSTa.  All of the BSD code has been reformatted and
 * modified to work with VSTa.
 */

/*
 * Copyright (c) 1987, 1989 Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Arthur David Olson of the National Cancer Institute.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Leap second handling from Bradley White (bww@k.gp.cs.cmu.edu).
 * POSIX-style TZ environment variable handling from Guy Harris
 * (guy@auspex.com).
 */

#include <sys/types.h>
#include <sys/timeb.h>
#include <sys/param.h>
#include <time.h>
#include <tzfile.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

/*
 * Timezone handling constants
 */
#define ACCESS_MODE O_RDONLY
#define OPEN_MODE O_RDONLY

#define	JULIAN_DAY 0		/* Jn - Julian day */
#define	DAY_OF_YEAR 1		/* n - day of year */
#define	MONTH_NTH_DAY_OF_WEEK 2	/* Mm.n.d - month, week, day of week */

/*
 * Someone might make incorrect use of a time zone abbreviation:
 *	1.	They might reference tzname[0] before calling tzset (explicitly
 *	 	or implicitly).
 *	2.	They might reference tzname[1] before calling tzset (explicitly
 *	 	or implicitly).
 *	3.	They might reference tzname[1] after setting to a time zone
 *		in which Daylight Saving Time is never observed.
 *	4.	They might reference tzname[0] after setting to a time zone
 *		in which Standard Time is never observed.
 *	5.	They might reference tm.tm_zone after calling offtime.
 * What's best to do in the above cases is open to debate;
 * for now, we just set things up so that in any of the five cases
 * WILDABBR is used.  Another possibility:  initialize tzname[0] to the
 * string "tzname[0] used before set", and similarly for the other cases.
 * And another:  initialize tzname[0] to "ERA", with an explanation in the
 * manual page of what this "time zone abbreviation" means (doing this so
 * that tzname[0] has the "normal" length of three characters).
 */
#define WILDABBR "   "

static char GMT[] = "GMT";

struct ttinfo {			/* time type information */
	long tt_gmtoff;		/* GMT offset in seconds */
	int tt_isdst;		/* used to set tm_isdst */
	int tt_abbrind;		/* abbreviation list index */
	int tt_ttisstd;		/* TRUE if transition is std time */
};

struct lsinfo {			/* leap second information */
	time_t ls_trans;	/* transition time */
	long ls_corr;		/* correction to apply */
};

struct state {
	int leapcnt;
	int timecnt;
	int typecnt;
	int charcnt;
	time_t ats[TZ_MAX_TIMES];
	unsigned char types[TZ_MAX_TIMES];
	struct ttinfo ttis[TZ_MAX_TYPES];
	char chars[(TZ_MAX_CHARS + 1 > sizeof GMT) ?
			TZ_MAX_CHARS + 1 : sizeof GMT];
	struct lsinfo lsis[TZ_MAX_LEAPS];
};

struct rule {
	int r_type;		/* type of rule--see below */
	int r_day;		/* day number of rule */
	int r_week;		/* week number of rule */
	int r_mon;		/* month number of rule */
	long r_time;		/* transition time of rule */
};

/*
 * Names of the days, months, times, etc
 */
char *__days_short[] =
	{"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
char *__days_long[] =
	{"Sunday", "Monday", "Tuesday", "Wednesday",
	 "Thursday", "Friday", "Saturday"};
char *__months_short[] =
	{"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
char *__months_long[] =
	{"January", "February", "March", "April", "May", "June",
	 "July", "August", "September", "October", "November", "December"};
char *__ampm[] =
	{"AM", "PM"};

/*
 * Number of days in each month and in each year
 */
int __months_len[2][12] =
	{{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
	 {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}};
int __years_len[2] = {DAYSPERNYEAR, DAYSPERLYEAR};

/*
 * Keep track of local and GMT time information
 */
static struct state *lclptr;
static struct state *gmtptr;
static int lcl_is_set = FALSE;
static int gmt_is_set = FALSE;

/*
 * Externally visible variables
 */
char *__tzname[2] = {WILDABBR, WILDABBR};
time_t timezone = 0;
int daylight = 0;

/*
 * Prototypes for static functions.
 */
static long detzcode(const char *codep);
static const char *getzname(const char *strp);
static const char *getnum(const char *strp, int *nump, int min, int max);
static const char *getsecs(const char *strp, long *secsp);
static const char *getoffset(const char *strp, long *offsetp);
static const char *getrule(const char *strp, struct rule *rulep);
static void gmtload(struct state *sp);
static void gmtsub(const time_t *timep, long offset, struct tm *tmp);
static void localsub(const time_t *timep, long offset, struct tm *tmp);
static void settzname(void);
static void timesub(const time_t *timep, long offset,
		    const struct state *sp, struct tm *tmp);
static time_t transtime(time_t janfirst, int year,
			const struct rule *rulep, long offset);
static int tzload(const char *name, struct state *sp);
static int tzparse(const char *name, struct state *sp, int lastditch);
static void normalise(int *tensptr, int *unitsptr, int base);
static int tmcomp(const struct tm *atmp, const struct tm *btmp);
static time_t time2(struct tm *tmp, void (*funcp)(), long offset, int *okayp);
static time_t time1(struct tm *tmp, void (*funcp)(), long offset);

/*
 * DAYSINYEAR
 *	Returns the number of days in the specified year
 */
#define DAYSINYEAR(year) (__years_len[isleap(year)])

/*
 * __get_tzname()
 *	Return pointer to the timezone name
 */
char **
__get_tzname(void)
{
	return(__tzname);
}

#ifndef SRV
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
int
__usleep(uint usecs)
{
	struct time t;

	time_get(&t);
	t.t_usec += usecs;
	while (t.t_usec > 1000000) {
		t.t_sec += 1;
		t.t_usec -= 1000000;
	}
	if (time_sleep(&t) < 0) {
		return(-1);
	}
	return(0);
}
int
usleep(uint usecs)
{
	return(__usleep(usecs));
}

/*
 * __msleep()
 *	Like sleep, but milliseconds
 */
__msleep(uint msecs)
{
	return(__usleep(msecs * 1000));
}
#endif /* !SRV */

/*
 * time()
 *	Get time in seconds since 1990
 *
 * Yeah, I could've done it from 1970, but this gains me 20 years.
 * It also lets me skip some weirdness in the 70's, and even I think
 * in the 80's.  It should also piss off all the people who like
 * to write ~1500 lines of C just to tell the time.  Besides, we can
 * still have 1970 as a time, it's just negative :-)
 */
time_t
time(long *lp)
{
	struct time t;

	time_get(&t);
	if (lp) {
		*lp = t.t_sec;
	}
	return(t.t_sec);
}

#ifndef SRV
/*
 * asctime()
 *	Another way to get a printed string version of time
 */
char *
asctime(const struct tm *tm)
{
	static char timebuf[32];

	/*
	 * Print it all into a buffer
	 */
	sprintf(timebuf, "%.3s %.3s%3d %02.2d:%02.2d:%02.2d %d %s\n",
		__days_short[tm->tm_wday], __months_short[tm->tm_mon],
		tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec,
		TM_YEAR_BASE + tm->tm_year, tm->tm_zone);
	return(timebuf);
}

/*
 * ctime()
 *	Give printed string version of time
 */
char *
ctime(time_t *lp)
{
	register struct tm *tm;

	/*
	 * Get basic time information
	 */
	tm = localtime(lp);
	return(asctime(tm));
}

/*
 * ftime()
 *	Get the current time in timeb format
 */
int
ftime(struct timeb *__tp)
{
	struct time t;
	
	time_get(&t);
	__tp->time = t.t_sec;
	__tp->millitm = t.t_usec / 1000;
	__tp->timezone = 0;
	__tp->dstflag = 0;

	return(0);
}
#endif /* !SRV */

/*
 * detzcode()
 */
static long
detzcode(const char *codep)
{
	long result = 0;
	int i;

	for (i = 0; i < 4; ++i)
		result = (result << 8) | (codep[i] & 0xff);
	return(result);
}

/*
 * settzname()
 */
static void
settzname(void)
{
	const struct state *sp = lclptr;
	int i;

	__tzname[0] = WILDABBR;
	__tzname[1] = WILDABBR;
	daylight = 0;
	timezone = 0;
	if (sp == NULL) {
		__tzname[0] = __tzname[1] = GMT;
		return;
	}
	for (i = 0; i < sp->typecnt; ++i) {
		const struct ttinfo *ttisp = &sp->ttis[i];

		__tzname[ttisp->tt_isdst] =
			(char *) &sp->chars[ttisp->tt_abbrind];
		if (ttisp->tt_isdst) {
			daylight = 1;
		}
		if (i == 0 || !ttisp->tt_isdst) {
			timezone = -(ttisp->tt_gmtoff);
		}
	}
	/*
	 * And to get the latest zone names into tzname...
	 */
	for (i = 0; i < sp->timecnt; ++i) {
		const struct ttinfo *ttisp = &sp->ttis[sp->types[i]];

		__tzname[ttisp->tt_isdst] =
			(char *)&sp->chars[ttisp->tt_abbrind];
	}
}

/*
 * tzload()
 */
static int
tzload(const char *name, struct state *sp)
{
	const char *p;
	int i;
	int fid;
	char fullname[MAXPATH + 1];
	const struct tzhead *tzhp;
	char buf[sizeof *sp + sizeof *tzhp];
	int ttisstdcnt;

	if (name == NULL && (name = TZDEFAULT) == NULL) {
		return(-1);
	}

	if (name[0] == ':') {
		++name;
	}
	if (name[0] != '/') {
		if ((p = TZDIR) == NULL) {
			return(-1);
		}
		if ((strlen(p) + strlen(name) + 1) >= sizeof fullname) {
			return(-1);
		}
		(void)strcpy(fullname, p);
		(void)strcat(fullname, "/");
		(void)strcat(fullname, name);
		name = fullname;
	}
	if ((fid = open(name, OPEN_MODE)) == -1) {
		return(-1);
	}
		
	i = read(fid, buf, sizeof buf);
	if (close(fid) != 0 || i < sizeof *tzhp) {
		return(-1);
	}
	tzhp = (struct tzhead *) buf;
	ttisstdcnt = (int) detzcode(tzhp->tzh_ttisstdcnt);
	sp->leapcnt = (int) detzcode(tzhp->tzh_leapcnt);
	sp->timecnt = (int) detzcode(tzhp->tzh_timecnt);
	sp->typecnt = (int) detzcode(tzhp->tzh_typecnt);
	sp->charcnt = (int) detzcode(tzhp->tzh_charcnt);
	if (sp->leapcnt < 0 || sp->leapcnt > TZ_MAX_LEAPS ||
			sp->typecnt <= 0 || sp->typecnt > TZ_MAX_TYPES ||
			sp->timecnt < 0 || sp->timecnt > TZ_MAX_TIMES ||
			sp->charcnt < 0 || sp->charcnt > TZ_MAX_CHARS ||
			(ttisstdcnt != sp->typecnt && ttisstdcnt != 0)) {
		return(-1);
	}
	if (i < sizeof *tzhp +
			sp->timecnt * (4 + sizeof (char)) +
			sp->typecnt * (4 + 2 * sizeof (char)) +
			sp->charcnt * sizeof (char) +
			sp->leapcnt * 2 * 4 +
			ttisstdcnt * sizeof (char)) {
		return(-1);
	}
	p = buf + sizeof *tzhp;
	for (i = 0; i < sp->timecnt; ++i) {
		sp->ats[i] = detzcode(p);
		p += 4;
	}
	for (i = 0; i < sp->timecnt; ++i) {
		sp->types[i] = (unsigned char) *p++;
		if (sp->types[i] >= sp->typecnt)
			return(-1);
	}
	for (i = 0; i < sp->typecnt; ++i) {
		struct ttinfo *ttisp = &sp->ttis[i];

		ttisp->tt_gmtoff = detzcode(p);
		p += 4;
		ttisp->tt_isdst = (unsigned char) *p++;
		if (ttisp->tt_isdst != 0 && ttisp->tt_isdst != 1) {
			return(-1);
		}
		ttisp->tt_abbrind = (unsigned char) *p++;
		if (ttisp->tt_abbrind < 0 ||
				ttisp->tt_abbrind > sp->charcnt) {
			return(-1);
		}
	}
	for (i = 0; i < sp->charcnt; ++i) {
		sp->chars[i] = *p++;
	}
	sp->chars[i] = '\0';	/* ensure '\0' at end */
	for (i = 0; i < sp->leapcnt; ++i) {
		struct lsinfo *lsisp = &sp->lsis[i];

		lsisp->ls_trans = detzcode(p);
		p += 4;
		lsisp->ls_corr = detzcode(p);
		p += 4;
	}
	for (i = 0; i < sp->typecnt; ++i) {
		struct ttinfo *ttisp = &sp->ttis[i];

		if (ttisstdcnt == 0) {
			ttisp->tt_ttisstd = FALSE;
		} else {
			ttisp->tt_ttisstd = *p++;
			if (ttisp->tt_ttisstd != TRUE &&
					ttisp->tt_ttisstd != FALSE)
				return(-1);
		}
	}

	return(0);
}

/*
 * getzname()
 *
 * Given a pointer into a time zone string, scan until a character that is not
 * a valid character in a zone name is found.  Return a pointer to that
 * character.
 */
static const char *
getzname(const char *strp)
{
	char c;

	while ((c = *strp) != '\0' && !isdigit(c) && c != ',' && c != '-' &&
		c != '+') {
			++strp;
	}
	return(strp);
}

/*
 * getnum()
 *
 * Given a pointer into a time zone string, extract a number from that string.
 * Check that the number is within a specified range; if it is not, return
 * NULL.
 * Otherwise, return a pointer to the first character not part of the number.
 */
static const char *
getnum(const char *strp, int *nump, const int min, const int max)
{
	char c;
	int num;

	if (strp == NULL || !isdigit(*strp)) {
		return(NULL);
	}
	num = 0;
	while ((c = *strp) != '\0' && isdigit(c)) {
		num = num * 10 + (c - '0');
		if (num > max) {
			return(NULL);	/* illegal value */
		}
		++strp;
	}
	if (num < min) {
		return(NULL);		/* illegal value */
	}
	*nump = num;
	return(strp);
}

/*
 * getsecs()
 *
 * Given a pointer into a time zone string, extract a number of seconds,
 * in hh[:mm[:ss]] form, from the string.
 * If any error occurs, return NULL.
 * Otherwise, return a pointer to the first character not part of the number
 * of seconds.
 */
static const char *
getsecs(const char *strp, long *secsp)
{
	int num;

	strp = getnum(strp, &num, 0, HOURSPERDAY);
	if (strp == NULL) {
		return(NULL);
	}
	*secsp = num * SECSPERHOUR;
	if (*strp == ':') {
		++strp;
		strp = getnum(strp, &num, 0, MINSPERHOUR - 1);
		if (strp == NULL) {
			return(NULL);
		}
		*secsp += num * SECSPERMIN;
		if (*strp == ':') {
			++strp;
			strp = getnum(strp, &num, 0, SECSPERMIN - 1);
			if (strp == NULL) {
				return(NULL);
			}
			*secsp += num;
		}
	}
	return(strp);
}

/*
 * getoffset()
 *
 * Given a pointer into a time zone string, extract an offset, in
 * [+-]hh[:mm[:ss]] form, from the string.
 * If any error occurs, return NULL.
 * Otherwise, return a pointer to the first character not part of the time.
 */
static const char *
getoffset(const char *strp, long *offsetp)
{
	int neg;

	if (*strp == '-') {
		neg = 1;
		++strp;
	} else if (isdigit(*strp) || *strp++ == '+') {
		neg = 0;
	} else {
		return(NULL);		/* illegal offset */
	}
	strp = getsecs(strp, offsetp);
	if (strp == NULL) {
		return(NULL);		/* illegal time */
	}
	if (neg) {
		*offsetp = -*offsetp;
	}
	return(strp);
}

/*
 * getrule()
 *
 * Given a pointer into a time zone string, extract a rule in the form
 * date[/time].  See POSIX section 8 for the format of "date" and "time".
 * If a valid rule is not found, return NULL.
 * Otherwise, return a pointer to the first character not part of the rule.
 */
static const char *
getrule(const char *strp, struct rule *rulep)
{
	if (*strp == 'J') {
		/*
		 * Julian day.
		 */
		rulep->r_type = JULIAN_DAY;
		++strp;
		strp = getnum(strp, &rulep->r_day, 1, DAYSPERNYEAR);
	} else if (*strp == 'M') {
		/*
		 * Month, week, day.
		 */
		rulep->r_type = MONTH_NTH_DAY_OF_WEEK;
		++strp;
		strp = getnum(strp, &rulep->r_mon, 1, MONSPERYEAR);
		if (strp == NULL) {
			return(NULL);
		}
		if (*strp++ != '.') {
			return(NULL);
		}
		strp = getnum(strp, &rulep->r_week, 1, 5);
		if (strp == NULL) {
			return(NULL);
		}
		if (*strp++ != '.') {
			return(NULL);
		}
		strp = getnum(strp, &rulep->r_day, 0, DAYSPERWEEK - 1);
	} else if (isdigit(*strp)) {
		/*
		 * Day of year.
		 */
		rulep->r_type = DAY_OF_YEAR;
		strp = getnum(strp, &rulep->r_day, 0, DAYSPERLYEAR - 1);
	} else {
		return(NULL);		/* invalid format */
	}
	if (strp == NULL) {
		return(NULL);
	}
	if (*strp == '/') {
		/*
		 * Time specified.
		 */
		++strp;
		strp = getsecs(strp, &rulep->r_time);
	} else {
		rulep->r_time = 2 * SECSPERHOUR;	/* default = 2:00:00 */
	}
	return(strp);
}

/*
 * transtime()
 
 * Given the Epoch-relative time of January 1, 00:00:00 GMT, in a year, the
 * year, a rule, and the offset from GMT at the time that rule takes effect,
 * calculate the Epoch-relative time that rule takes effect.
 */
static time_t
transtime(time_t janfirst, int year, const struct rule *rulep, long offset)
{
	int leapyear;
	time_t value;
	int i;
	int d, m1, yy0, yy1, yy2, dow;

	leapyear = isleap(year);
	switch (rulep->r_type) {

	case JULIAN_DAY:
		/*
		 * Jn - Julian day, 1 == January 1, 60 == March 1 even in leap
		 * years.
		 * In non-leap years, or if the day number is 59 or less, just
		 * add SECSPERDAY times the day number-1 to the time of
		 * January 1, midnight, to get the day.
		 */
		value = janfirst + (rulep->r_day - 1) * SECSPERDAY;
		if (leapyear && rulep->r_day >= 60) {
			value += SECSPERDAY;
		}
		break;

	case DAY_OF_YEAR:
		/*
		 * n - day of year.
		 * Just add SECSPERDAY times the day number to the time of
		 * January 1, midnight, to get the day.
		 */
		value = janfirst + rulep->r_day * SECSPERDAY;
		break;

	case MONTH_NTH_DAY_OF_WEEK:
		/*
		 * Mm.n.d - nth "dth day" of month m.
		 */
		value = janfirst;
		for (i = 0; i < rulep->r_mon - 1; ++i) {
			value += __months_len[leapyear][i] * SECSPERDAY;
		}

		/*
		 * Use Zeller's Congruence to get day-of-week of first day of
		 * month.
		 */
		m1 = (rulep->r_mon + 9) % 12 + 1;
		yy0 = (rulep->r_mon <= 2) ? (year - 1) : year;
		yy1 = yy0 / 100;
		yy2 = yy0 % 100;
		dow = ((26 * m1 - 2) / 10
			+ 1 + yy2 + yy2 / 4 + yy1 / 4 - 2 * yy1) % 7;
		if (dow < 0) {
			dow += DAYSPERWEEK;
		}

		/*
		 * "dow" is the day-of-week of the first day of the month.  Get
		 * the day-of-month (zero-origin) of the first "dow" day of the
		 * month.
		 */
		d = rulep->r_day - dow;
		if (d < 0) {
			d += DAYSPERWEEK;
		}
		for (i = 1; i < rulep->r_week; ++i) {
			if (d + DAYSPERWEEK >=
				__months_len[leapyear][rulep->r_mon - 1]) {
					break;
			}
			d += DAYSPERWEEK;
		}

		/*
		 * "d" is the day-of-month (zero-origin) of the day we want.
		 */
		value += d * SECSPERDAY;
		break;
	}

	/*
	 * "value" is the Epoch-relative time of 00:00:00 GMT on the day in
	 * question.  To get the Epoch-relative time of the specified local
	 * time on that day, add the transition time and the current offset
	 * from GMT.
	 */
	return(value + rulep->r_time + offset);
}

/*
 * tzparse()
 *
 * Given a POSIX section 8-style TZ string, fill in the rule tables as
 * appropriate.
 */
static int
tzparse(const char *name, struct state *sp, const int lastditch)
{
	const char *stdname;
	const char *dstname;
	int stdlen;
	int dstlen;
	long stdoffset;
	long dstoffset;
	register time_t *atp;
	register unsigned char *typep;
	register char *cp;
	register int load_result;

	stdname = name;
	if (lastditch) {
		stdlen = strlen(name);	/* length of standard zone name */
		name += stdlen;
		if (stdlen >= sizeof sp->chars) {
			stdlen = (sizeof sp->chars) - 1;
		}
	} else {
		name = getzname(name);
		stdlen = name - stdname;
		if (stdlen < 3) {
			return(-1);
		}
	}
	if (*name == '\0') {
		return(-1);
	} else {
		name = getoffset(name, &stdoffset);
		if (name == NULL) {
			return(-1);
		}
	}
	load_result = tzload(TZDEFRULES, sp);
	if (load_result != 0) {
		sp->leapcnt = 0;		/* so, we're off a little */
	}
	if (*name != '\0') {
		dstname = name;
		name = getzname(name);
		dstlen = name - dstname;	/* length of DST zone name */
		if (dstlen < 3) {
			return(-1);
		}
		if (*name != '\0' && *name != ',' && *name != ';') {
			name = getoffset(name, &dstoffset);
			if (name == NULL) {
				return(-1);
			}
		} else {
			dstoffset = stdoffset - SECSPERHOUR;
		}
		if (*name == ',' || *name == ';') {
			struct rule start;
			struct rule end;
			int year;
			time_t janfirst;
			time_t starttime;
			time_t endtime;

			++name;
			if ((name = getrule(name, &start)) == NULL) {
				return(-1);
			}
			if (*name++ != ',') {
				return(-1);
			}
			if ((name = getrule(name, &end)) == NULL) {
				return(-1);
			}
			if (*name != '\0') {
				return(-1);
			}
			sp->typecnt = 2;	/* standard time and DST */
			/*
			 * Two transitions per year, from EPOCH_YEAR to
			 * TIMEEND_YEAR.
			 */
			sp->timecnt = 2 * (TIMEEND_YEAR - EPOCH_YEAR + 1);
			if (sp->timecnt > TZ_MAX_TIMES) {
				return(-1);
			}
			sp->ttis[0].tt_gmtoff = -dstoffset;
			sp->ttis[0].tt_isdst = 1;
			sp->ttis[0].tt_abbrind = stdlen + 1;
			sp->ttis[1].tt_gmtoff = -stdoffset;
			sp->ttis[1].tt_isdst = 0;
			sp->ttis[1].tt_abbrind = 0;
			atp = sp->ats;
			typep = sp->types;
			janfirst = 0;
			for (year = EPOCH_YEAR; year <= TIMEEND_YEAR; ++year) {
				starttime = transtime(janfirst, year, &start,
						      stdoffset);
				endtime = transtime(janfirst, year, &end,
						    dstoffset);
				if (starttime > endtime) {
					*atp++ = endtime;
					*typep++ = 1;	/* DST ends */
					*atp++ = starttime;
					*typep++ = 0;	/* DST begins */
				} else {
					*atp++ = starttime;
					*typep++ = 0;	/* DST begins */
					*atp++ = endtime;
					*typep++ = 1;	/* DST ends */
				}
				janfirst +=
					__years_len[isleap(year)] * SECSPERDAY;
			}
		} else {
			int sawstd;
			int sawdst;
			long stdfix;
			long dstfix;
			long oldfix;
			int isdst;
			int i;

			if (*name != '\0') {
				return(-1);
			}
			if (load_result != 0) {
				return(-1);
			}
			/*
			 * Compute the difference between the real and
			 * prototype standard and summer time offsets
			 * from GMT, and put the real standard and summer
			 * time offsets into the rules in place of the
			 * prototype offsets.
			 */
			sawstd = FALSE;
			sawdst = FALSE;
			stdfix = 0;
			dstfix = 0;
			for (i = 0; i < sp->typecnt; ++i) {
				if (sp->ttis[i].tt_isdst) {
					oldfix = dstfix;
					dstfix =
					    sp->ttis[i].tt_gmtoff + dstoffset;
					if (sawdst && (oldfix != dstfix)) {
						return(-1);
					}
					sp->ttis[i].tt_gmtoff = -dstoffset;
					sp->ttis[i].tt_abbrind = stdlen + 1;
					sawdst = TRUE;
				} else {
					oldfix = stdfix;
					stdfix =
					    sp->ttis[i].tt_gmtoff + stdoffset;
					if (sawstd && (oldfix != stdfix)) {
						return(-1);
					}
					sp->ttis[i].tt_gmtoff = -stdoffset;
					sp->ttis[i].tt_abbrind = 0;
					sawstd = TRUE;
				}
			}
			/*
			 * Make sure we have both standard and summer time.
			 */
			if (!sawdst || !sawstd) {
				return(-1);
			}
			/*
			 * Now correct the transition times by shifting
			 * them by the difference between the real and
			 * prototype offsets.  Note that this difference
			 * can be different in standard and summer time;
			 * the prototype probably has a 1-hour difference
			 * between standard and summer time, but a different
			 * difference can be specified in TZ.
			 */
			isdst = FALSE;	/* we start in standard time */
			for (i = 0; i < sp->timecnt; ++i) {
				const struct ttinfo *ttisp;

				/*
				 * If summer time is in effect, and the
				 * transition time was not specified as
				 * standard time, add the summer time
				 * offset to the transition time;
				 * otherwise, add the standard time offset
				 * to the transition time.
				 */
				ttisp = &sp->ttis[sp->types[i]];
				sp->ats[i] += (isdst && !ttisp->tt_ttisstd) ?
					      dstfix : stdfix;
				isdst = ttisp->tt_isdst;
			}
		}
	} else {
		dstlen = 0;
		sp->typecnt = 1;		/* only standard time */
		sp->timecnt = 0;
		sp->ttis[0].tt_gmtoff = -stdoffset;
		sp->ttis[0].tt_isdst = 0;
		sp->ttis[0].tt_abbrind = 0;
	}
	sp->charcnt = stdlen + 1;
	if (dstlen != 0) {
		sp->charcnt += dstlen + 1;
	}
	if (sp->charcnt > sizeof sp->chars) {
		return(-1);
	}
	cp = sp->chars;
	(void)strncpy(cp, stdname, stdlen);
	cp += stdlen;
	*cp++ = '\0';
	if (dstlen != 0) {
		(void) strncpy(cp, dstname, dstlen);
		*(cp + dstlen) = '\0';
	}
	return(0);
}

/*
 * gmtload()
 *	Pseudo load of the GMT timezone
 *
 * For the sake of not including all of the time library if we only want
 * GMT, and to stop us from dying if the GMT timezone file is missing we
 * hardcode this one.
 */
static void
gmtload(struct state *sp)
{
	sp->leapcnt = 0;
	sp->timecnt = 0;
	sp->typecnt = 1;
	sp->charcnt = 4;
	sp->ttis[0].tt_gmtoff = 0;
	sp->ttis[0].tt_isdst = 0;
	sp->ttis[0].tt_abbrind = 0;
	strcpy(sp->chars, GMT);
	sp->ttis[0].tt_ttisstd = 0;
	(void) tzparse(GMT, sp, TRUE);
}

/*
 * tzsetwall()
 */
void
tzsetwall(void)
{
	lcl_is_set = TRUE;
	if (lclptr == NULL) {
		lclptr = (struct state *) malloc(sizeof *lclptr);
		if (lclptr == NULL) {
			settzname();	/* all we can do */
			return;
		}
	}
	if (tzload((char *) NULL, lclptr) != 0) {
		gmtload(lclptr);
	}
	settzname();
}

/*
 * tzset()
 */
void
tzset(void)
{
#ifndef SRV
	const char *name;

	name = getenv("TZ");
	if (name == NULL) {
#endif /* !SRV */
		tzsetwall();
#ifndef SRV
		return;
	}
	lcl_is_set = TRUE;
	if (lclptr == NULL) {
		lclptr = (struct state *) malloc(sizeof *lclptr);
		if (lclptr == NULL) {
			settzname();	/* all we can do */
			return;
		}
	}
	if (*name == '\0') {
		/*
		 * User wants it fast rather than right.
		 */
		lclptr->leapcnt = 0;		/* so, we're off a little */
		lclptr->timecnt = 0;
		lclptr->ttis[0].tt_gmtoff = 0;
		lclptr->ttis[0].tt_abbrind = 0;
		(void) strcpy(lclptr->chars, GMT);
	} else if (tzload(name, lclptr) != 0) {
		if (name[0] == ':' || tzparse(name, lclptr, FALSE) != 0) {
			(void) gmtload(lclptr);
		}
	}
	settzname();
#endif /* !SRV */
}

/*
 * localsub()
 *
 * The easy way to behave "as if no library function calls" localtime
 * is to not call it--so we drop its guts into "localsub", which can be
 * freely called.  (And no, the PANS doesn't require the above behavior--
 * but it *is* desirable.)
 *
 * The unused offset argument is for the benefit of mktime variants.
 */
/*ARGSUSED*/
static void
localsub(const time_t *timep, const long offset, struct tm *tmp)
{
	struct state *sp;
	const struct ttinfo *ttisp;
	int i;
	const time_t t = *timep;

	if (!lcl_is_set) {
		tzset();
	}
	sp = lclptr;
	if (sp == NULL) {
		gmtsub(timep, offset, tmp);
		return;
	}
	if (sp->timecnt == 0 || t < sp->ats[0]) {
		i = 0;
		while (sp->ttis[i].tt_isdst) {
			if (++i >= sp->typecnt) {
				i = 0;
				break;
			}
		}
	} else {
		for (i = 1; i < sp->timecnt; ++i) {
			if (t < sp->ats[i]) {
				break;
			}
		}
		i = sp->types[i - 1];
	}
	ttisp = &sp->ttis[i];
	/*
	 * To get (wrong) behavior that's compatible with System V Release 2.0
	 * you'd replace the statement below with
	 *	t += ttisp->tt_gmtoff;
	 *	timesub(&t, 0L, sp, tmp);
	 *
	 * Why you'd want to do this under VSTa is beyond me, but someone
	 * will doubtless find this useful
	 */
	timesub(&t, ttisp->tt_gmtoff, sp, tmp);
	tmp->tm_isdst = ttisp->tt_isdst;
	__tzname[tmp->tm_isdst] = (char *)&sp->chars[ttisp->tt_abbrind];
	tmp->tm_zone = &sp->chars[ttisp->tt_abbrind];
}

/*
 * localtime()
 */
struct tm *
localtime(time_t *timep)
{
	static struct tm tm;

	localsub(timep, 0L, &tm);
	return(&tm);
}

/*
 * gmtsub()
 *
 * gmtsub is to gmtime as localsub is to localtime.
 */
static void
gmtsub(const time_t *timep, const long offset, struct tm *tmp)
{
	if (!gmt_is_set) {
		gmt_is_set = TRUE;
		gmtptr = (struct state *) malloc(sizeof *gmtptr);
		if (gmtptr != NULL) {
			gmtload(gmtptr);
		}
	}
	timesub(timep, offset, gmtptr, tmp);
	/*
	 * Could get fancy here and deliver something such as
	 * "GMT+xxxx" or "GMT-xxxx" if offset is non-zero,
	 * but this is no time for a treasure hunt.
	 */
	if (offset != 0) {
		tmp->tm_zone = WILDABBR;
	} else {
		if (gmtptr == NULL) {
			tmp->tm_zone = GMT;
		} else {
			tmp->tm_zone = gmtptr->chars;
		}
	}
}

/*
 * gmtime()
 */
struct tm *
gmtime(time_t *timep)
{
	static struct tm tm;

	gmtsub(timep, 0L, &tm);
	return(&tm);
}

/*
 * timesub()
 */
static void
timesub(const time_t *timep, long offset,
	const struct state *sp, struct tm *tmp)
{
	const struct lsinfo *lp;
	long days;
	long rem;
	int y;
	int yleap;
	const int *ip;
	long corr = 0;
	int hit = FALSE;
	int i;

	i = (sp == NULL) ? 0 : sp->leapcnt;
	while (--i >= 0) {
		lp = &sp->lsis[i];
		if (*timep >= lp->ls_trans) {
			if (*timep == lp->ls_trans) {
				hit = ((i == 0 && lp->ls_corr > 0) ||
					lp->ls_corr > sp->lsis[i - 1].ls_corr);
			}
			corr = lp->ls_corr;
			break;
		}
	}
	days = *timep / SECSPERDAY;
	rem = *timep % SECSPERDAY;
	rem += (offset - corr);
	while (rem < 0) {
		rem += SECSPERDAY;
		--days;
	}
	while (rem >= SECSPERDAY) {
		rem -= SECSPERDAY;
		++days;
	}
	tmp->tm_hour = (int)(rem / SECSPERHOUR);
	rem = rem % SECSPERHOUR;
	tmp->tm_min = (int)(rem / SECSPERMIN);
	tmp->tm_sec = (int)(rem % SECSPERMIN);
	if (hit) {
		/*
		 * A positive leap second requires a special
		 * representation.  This uses "... ??:59:60".
		 */
		++(tmp->tm_sec);
	}

	/*
	 * Day of week is easier, at least until we get leap weeks
	 * where you get, say, two Tuesdays in a row.  Hmmm... or
	 * two Saturdays. :-)
	 */
	tmp->tm_wday = (int)((EPOCH_WDAY + days) % DAYSPERWEEK);
	if (tmp->tm_wday < 0) {
		tmp->tm_wday += DAYSPERWEEK;
	}
	y = EPOCH_YEAR;
	if (days >= 0) {
		for (;;) {
			yleap = isleap(y);
			if (days < (long)__years_len[yleap])
				break;
			++y;
			days = days - (long)__years_len[yleap];
		}
	} else do {
		--y;
		yleap = isleap(y);
		days = days + (long) __years_len[yleap];
	} while (days < 0);
	tmp->tm_year = y - TM_YEAR_BASE;
	tmp->tm_yday = (int) days;
	ip = __months_len[yleap];
	for (tmp->tm_mon = 0; days >= (long)ip[tmp->tm_mon]; ++(tmp->tm_mon)) {
		days = days - (long) ip[tmp->tm_mon];
	}
	tmp->tm_mday = (int)(days + 1);
	tmp->tm_isdst = 0;
	tmp->tm_gmtoff = offset;
}

/*
 * Adapted from code provided by Robert Elz, who writes:
 *
 *	The "best" way to do mktime I think is based on an idea of Bob
 *	Kridle's (so its said...) from a long time ago. (mtxinu!kridle now).
 *	It does a binary search of the time_t space.  Since time_t's are
 *	just 32 bits, its a max of 32 iterations (even at 64 bits it
 *	would still be very reasonable).
 */

#ifndef WRONG
#define WRONG (-1)
#endif /* !defined WRONG */

/*
 * normalise()
 */
static void
normalise(int *tensptr, int *unitsptr, int base)
{
	if (*unitsptr >= base) {
		*tensptr += *unitsptr / base;
		*unitsptr %= base;
	} else if (*unitsptr < 0) {
		*tensptr -= 1 + (-*unitsptr) / base;
		*unitsptr = base - (-*unitsptr) % base;
	}
}

/*
 * tmcomp()
 */
static int
tmcomp(const struct tm *atmp, const struct tm *btmp)
{
	int result;

	if ((result = (atmp->tm_year - btmp->tm_year)) == 0 &&
		(result = (atmp->tm_mon - btmp->tm_mon)) == 0 &&
		(result = (atmp->tm_mday - btmp->tm_mday)) == 0 &&
		(result = (atmp->tm_hour - btmp->tm_hour)) == 0 &&
		(result = (atmp->tm_min - btmp->tm_min)) == 0) {
			result = atmp->tm_sec - btmp->tm_sec;
	}
	return(result);
}

/*
 * time2()
 */
static time_t
time2(struct tm *tmp, void (*funcp)(), long offset, int *okayp)
{
	const struct state *sp;
	int dir;
	int bits;
	int i, j;
	int saved_seconds;
	time_t newt;
	time_t t;
	struct tm yourtm, mytm;

	*okayp = FALSE;
	yourtm = *tmp;
	if (yourtm.tm_sec >= SECSPERMIN + 2 || yourtm.tm_sec < 0) {
		normalise(&yourtm.tm_min, &yourtm.tm_sec, SECSPERMIN);
	}
	normalise(&yourtm.tm_hour, &yourtm.tm_min, MINSPERHOUR);
	normalise(&yourtm.tm_mday, &yourtm.tm_hour, HOURSPERDAY);
	normalise(&yourtm.tm_year, &yourtm.tm_mon, MONSPERYEAR);
	while (yourtm.tm_mday <= 0) {
		--yourtm.tm_year;
		yourtm.tm_mday +=
			__years_len[isleap(yourtm.tm_year + TM_YEAR_BASE)];
	}
	for (;;) {
		i = __months_len[isleap(yourtm.tm_year +
			TM_YEAR_BASE)][yourtm.tm_mon];
		if (yourtm.tm_mday <= i) {
			break;
		}
		yourtm.tm_mday -= i;
		if (++yourtm.tm_mon >= MONSPERYEAR) {
			yourtm.tm_mon = 0;
			++yourtm.tm_year;
		}
	}
	saved_seconds = yourtm.tm_sec;
	yourtm.tm_sec = 0;
	/*
	 * Calculate the number of magnitude bits in a time_t
	 * (this works regardless of whether time_t is
	 * signed or unsigned, though lint complains if unsigned).
	 */
	for (bits = 0, t = 1; t > 0; ++bits, t <<= 1)
		;
	/*
	 * If time_t is signed, then 0 is the median value,
	 * if time_t is unsigned, then 1 << bits is median.
	 */
	t = (t < 0) ? 0 : ((time_t) 1 << bits);
	for (;;) {
		(*funcp)(&t, offset, &mytm);
		dir = tmcomp(&mytm, &yourtm);
		if (dir != 0) {
			if (bits-- < 0) {
				return(WRONG);
			}
			if (bits < 0) {
				--t;
			} else if (dir > 0) {
				t -= (time_t) 1 << bits;
			} else {
				t += (time_t) 1 << bits;
			}
			continue;
		}
		if (yourtm.tm_isdst < 0 || mytm.tm_isdst == yourtm.tm_isdst) {
			break;
		}
		/*
		 * Right time, wrong type.
		 * Hunt for right time, right type.
		 * It's okay to guess wrong since the guess
		 * gets checked.
		 */
		sp = (const struct state *)
			((funcp == localsub) ? lclptr : gmtptr);
		if (sp == NULL) {
			return(WRONG);
		}
		for (i = 0; i < sp->typecnt; ++i) {
			if (sp->ttis[i].tt_isdst != yourtm.tm_isdst) {
				continue;
			}
			for (j = 0; j < sp->typecnt; ++j) {
				if (sp->ttis[j].tt_isdst == yourtm.tm_isdst) {
					continue;
				}
				newt = t + sp->ttis[j].tt_gmtoff
				       - sp->ttis[i].tt_gmtoff;
				(*funcp)(&newt, offset, &mytm);
				if (tmcomp(&mytm, &yourtm) != 0) {
					continue;
				}
				if (mytm.tm_isdst != yourtm.tm_isdst) {
					continue;
				}
				/*
				 * We have a match.
				 */
				t = newt;
				goto label;
			}
		}
		return(WRONG);
	}
label:
	t += saved_seconds;
	(*funcp)(&t, offset, tmp);
	*okayp = TRUE;
	return(t);
}

/*
 * time1()
 */
static time_t
time1(struct tm *tmp, void (*funcp)(), long offset)
{
	time_t t;
	const struct state *sp;
	int samei, otheri;
	int okay;

	if (tmp->tm_isdst > 1) {
		tmp->tm_isdst = 1;
	}
	t = time2(tmp, funcp, offset, &okay);
	if (okay || tmp->tm_isdst < 0) {
		return(t);
	}
	/*
	 * We're supposed to assume that somebody took a time of one type
	 * and did some math on it that yielded a "struct tm" that's bad.
	 * We try to divine the type they started from and adjust to the
	 * type they need.
	 */
	sp = (const struct state *) ((funcp == localsub) ? lclptr : gmtptr);
	if (sp == NULL) {
		return(WRONG);
	}
	for (samei = 0; samei < sp->typecnt; ++samei) {
		if (sp->ttis[samei].tt_isdst != tmp->tm_isdst) {
			continue;
		}
		for (otheri = 0; otheri < sp->typecnt; ++otheri) {
			if (sp->ttis[otheri].tt_isdst == tmp->tm_isdst) {
				continue;
			}
			tmp->tm_sec += sp->ttis[otheri].tt_gmtoff
				       - sp->ttis[samei].tt_gmtoff;
			tmp->tm_isdst = !tmp->tm_isdst;
			t = time2(tmp, funcp, offset, &okay);
			if (okay) {
				return(t);
			}
			tmp->tm_sec -= sp->ttis[otheri].tt_gmtoff
				       - sp->ttis[samei].tt_gmtoff;
			tmp->tm_isdst = !tmp->tm_isdst;
		}
	}
	return(WRONG);
}

/*
 * mktime()
 *	Convert struct tm to a local timezone based time_t
 */
time_t
mktime(struct tm *tmp)
{
	return(time1(tmp, localsub, 0L));
}

/*
 * timegm()
 *	Convert struct tm to a GMT timezone based time_t
 */
time_t
timegm(struct tm *tmp)
{
	return(time1(tmp, gmtsub, 0L));
}
