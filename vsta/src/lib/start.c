/*
 * __start()
 *	Do the VSTa-specific stuff to set up argc/argv
 */
#include <sys/vm.h>
#include <lib/alloc.h>

/*
 * __start()
 *	Do some massaging of arguments during C startup
 */
void
__start(int *argcp, char ***argvp)
{
#ifdef LATER
	char *p, **argv = 0;
	int argc = 0;

	/*
	 * Walk the argument area.  Each argument is terminated with
	 * a null byte; two nulls in a row marks the end of all
	 * arguments.
	 */
	for (p = VADDR_ARGS; *p; ++p) {
		argc += 1;
		argv = realloc(argv, sizeof(char *)*argc);
		argv[argc-1] = p;
		while (*p) {
			++p;
		}
	}

	/*
	 * Stuff our values back to our caller
	 */
	*argcp = argc;
	*argvp = argv;
#else
	return;
#endif
}
