/*
 * time.c
 *	Time-oriented services
 */
#include <sys/types.h>

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
