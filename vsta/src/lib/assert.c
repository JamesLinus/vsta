/*
 * assert.c
 *	Supporting routines for the ASSERT/ASSERT_DEBUG macros
 */
#ifndef KERNEL
#include <stdio.h>
#endif

/*
 * assfail()
 *	Called from ASSERT-type macros on failure
 */
void
assfail(msg, file, line)
{
#ifdef KERNEL
	printf("Assertion failed in file %s, line %d\n", file, line);
	panic(msg);
#else
	fprintf(stderr, "Assertion failed in file %s, line %d\n",
		file, line);
	fprintf(stderr, "Fatal error: %s\n", msg);
	abort();
#endif
}
