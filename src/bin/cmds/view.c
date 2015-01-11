/*
 * view.c
 *	Simple/dumb wrapper to vector to vim instead
 */
int
main(int argc, char **argv)
{
	argv[0] = "view";
	execv("/vsta/bin/vi", argv);
	return(1);
}
