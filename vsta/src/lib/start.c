/*
 * __start()
 *	Do the VSTa-specific stuff to set up argc/argv
 */
#include <sys/types.h>
#include <std.h>
#include <fdl.h>

/*
 * __start()
 *	Do some massaging of arguments during C startup
 */
void
__start(int *argcp, char ***argvp, char *a)
{
	char *p, **argv = 0;
	int x, argc = 0, len;

	/*
	 * Boot programs get this
	 */
	if (a == 0) {
noargs:
		*argcp = 0;
		*argvp = 0;
		return;
	}

	/*
	 * Get count of arguments
	 */
	argc = *(ulong *)a;
	a += sizeof(ulong);
	argv = malloc((argc+1) * sizeof(char *));
	if (argv == 0) {
		goto noargs;
	}

	/*
	 * Walk the argument area.  Copy each string, as we wish
	 * to tear down the shared memory once we're finished.
	 */
	for (x = 0; x < argc; ++x) {
		len = strlen(a)+1;
		argv[x] = malloc(len);
		if (!argv[x]) {
			goto noargs;
		}
		bcopy(a, argv[x], len);
		a += len;
	}
	argv[x] = 0;	/* Traditional */

	/*
	 * Stuff our values back to our caller
	 */
	*argcp = argc;
	*argvp = argv;

	/*
	 * Restore our fdl state
	 */
	__fdl_restore(a);
}
