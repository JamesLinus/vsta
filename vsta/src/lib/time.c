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
