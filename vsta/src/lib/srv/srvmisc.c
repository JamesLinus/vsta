/*
 * srvmisc.c
 *	Miscellaneous functions
 *
 * Written here in their smallest fashion, in support of boot servers
 */
#include <time.h>

/*
 * atoi()
 *	Convert ASCII to integer value
 */
int
atoi(const char *p)
{
	int val = 0;
	char c;

	while (c = *p++) {
		if ((c < '0') || (c > '9')) {
			break;
		}
		val = val * 10 + (c - '0');
	}
	return(val);
}

/*
 * __ptr_errno()
 *	Handle linkage for errno emulation
 *
 * Provides a dummy; boot servers don't need errno emulation.
 */
static int dummy_errno;

int *
__ptr_errno(void)
{
	return(&dummy_errno);
}

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
