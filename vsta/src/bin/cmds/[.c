/*
 * [.c
 *	Simple/dumb wrapper to vector to test instead
 */
int
main(int argc, char **argv)
{
	argv[0] = "[";
	execv("/vsta/bin/test", argv);
	return(1);
}
