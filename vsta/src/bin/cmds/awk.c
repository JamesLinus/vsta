/*
 * awk.c
 *	Simple/dumb wrapper to vector to gawk instead
 */
int
main(int argc, char **argv)
{
	argv[0] = "awk";
	execv("/vsta/bin/awk", argv);
	return(1);
}
