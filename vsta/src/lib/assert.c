/*
 * assert.c
 *	Supporting routines for the ASSERT/ASSERT_DEBUG macros
 */
#include <stdio.h>
#include <syslog.h>

/*
 * assfail()
 *	Called from ASSERT-type macros on failure
 */
void
assfail(const char *msg, const char *file, int line)
{
	if (__get_logopt()) {
		syslog(LOG_EMERG, "Assertion failed %s/%d: %s",
			file, line, msg);
	} else {
		fprintf(stderr, "Assertion failed in file %s, line %d\n",
			file, line);
		fprintf(stderr, "Fatal error: %s\n", msg);
	}
	abort();
}
