/*
 * fgrep.c
 *	Simple/dumb wrapper to vector to grep instead
 */
int
main(int argc, char **argv)
{
	argv[0] = "fgrep";
	execv("/vsta/bin/grep", argv);
	return(1);
}
