/*
 * test23.c
 *	See if arg passing via tfork() works
 */
#include <std.h>

static void
test2(ulong arg)
{
	printf("Got %U\n", arg);
	_exit(0);
}

main(void)
{
	tfork(test2, 1234U);
	sleep(1);
	return(0);
}
