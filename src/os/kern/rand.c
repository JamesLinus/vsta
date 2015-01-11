/*
 * rand.c
 *	Simple random number generator
 */
#include <sys/types.h>

static ulong state = 0L;

ulong
random(void)
{
	return((state = (state * 1103515245L + 12345L)) & 0x7fffffffL);
}

void
srandom(ulong l)
{
	state = l;
}
