/*
 * time.c
 *	Common routines for setime and date
 */
#include <time.h>
#include <stdio.h>

/*
 * prtime()
 *	Read system clock, show time
 */
void
prtime(void)
{
	time_t t;

	(void)time(&t);
	(void)printf("%s", ctime(&t));
}
