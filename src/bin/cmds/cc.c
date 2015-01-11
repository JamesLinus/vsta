/*
 * cc.c
 *	Simple/dumb wrapper to vector to gcc instead
 */
int
main(int argc, char **argv)
{
	argv[0] = "gcc";
	execv("/vsta/bin/gcc", argv);
	return(1);
}
