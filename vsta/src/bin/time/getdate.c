/*
 * getdate.c
 *	Routines to pull date out of PC hardware
 *
 * These routines are based on algorithms present in Mach and 4.4 BSD.
 * They should be considered derivative works of the analogous algorithms
 * in said systems.  All this good stuff comes from Bill Jolitz, and he
 * is thanked for yet another piece of SW!
 */
#include <sys/types.h>
#include <mach/nvram.h>

/*
 * Ports in PC NV hardware for reading date/time
 */
static const uint
	rtc_sec = 0x00,
	rtc_min = 0x02,
	rtc_hrs = 0x04,
	rtc_wday = 0x06,
	rtc_day = 0x07,
	rtc_month = 0x08,
	rtc_year = 0x09,
	rtc_statusA = 0x0a;

static const uint
	rtc_tup = 0x80;		/* Time UPdate in progress */

/*
 * nvread()
 *	Read from NV config
 */
static uint
nvread(uint port)
{
	static int setup = 0;

	if (!setup) {
		enable_io(RTCSEL, RTCDATA);
		setup = 1;
	}
	outportb(RTCSEL, port);
	return(inportb(RTCDATA));
}

/*
 * bcd()
 *	convert 2 digit BCD number
 */
static uint
bcd(uint i)
{
	return ((i/16)*10 + (i%16));
}

/*
 * ytos()
 *	convert years to seconds (from 1990)
 */
static ulong
ytos(uint y)
{
	uint i;
	ulong ret;

	if (y < 1990) {
		printf("Warning: year %d is less than 1990!\n", y);
	}
	ret = 0;
	for (i = 1990; i < y; i++) {
		if (i % 4) {
			ret += 365*24*60*60;
		} else {
			ret += 366*24*60*60;
		}
	}
	return(ret);
}

/*
 * mtos()
 *	convert months to seconds
 */
static ulong
mtos(uint m, int leap)
{
	uint i;
	ulong ret;

	ret = 0;
	for (i = 1; i < m; i++) {
		switch(i){
		case 1: case 3: case 5: case 7: case 8: case 10: case 12:
			ret += 31*24*60*60; break;
		case 4: case 6: case 9: case 11:
			ret += 30*24*60*60; break;
		case 2:
			if (leap) ret += 29*24*60*60;
			else ret += 28*24*60*60;
		}
	}
	return ret;
}


/*
 * read_time()
 *	Set up system time based on PC's hardware clock
 *
 * Returns seconds since 1990, or 0L on failure.
 */
ulong
read_time(void)
{
	ulong sec;
	uint leap, day_week, t, yd;
	uint sa, s;
	const uint dayst = 119,		/* Daylight savings, sort of */
		dayen = 303;

	/*
	 * Bail if no real-time clock
	 */
	sa = nvread(rtc_statusA);
	if ((sa == 0xff) || (sa == 0)) {
		printf("Error: hardware clock not found.\n");
		return(0L);
	}

	/*
	 * Wait for clock to be readable
	 */
	while ((sa & rtc_tup) == rtc_tup) {
		__msleep(10);
		sa = nvread(rtc_statusA);
	}
	/* Probably a race here, sigh */

	sec = bcd(nvread(rtc_year)) + 1900;
	if (sec < 1970) {
		sec += 100;
	}
	leap = !(sec % 4); sec = ytos(sec);			/* year */
	yd = mtos(bcd(nvread(rtc_month)),leap); sec += yd;	/* month */
	t = (bcd(nvread(rtc_day))-1) * 24*60*60; sec += t;
		yd += t;					/* date */
	day_week = nvread(rtc_wday);				/* day */
	sec += bcd(nvread(rtc_hrs)) * 60*60;			/* hour */
	sec += bcd(nvread(rtc_min)) * 60;			/* minutes */
	sec += bcd(nvread(rtc_sec));				/* seconds */

	/* Convert to daylight saving */
	yd = yd / (24*60*60);
	if ((yd >= dayst) && ( yd <= dayen)) {
		sec -= 60*60;
	}
	return(sec);
}
