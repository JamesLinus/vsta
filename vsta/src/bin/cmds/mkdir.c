/*
 * mkdir.c
 *	Create directories
 */
main(int argc, char **argv)
{
	int x, errs = 0;
	
	for (x = 1; x < argc; ++x) {
		if (mkdir(argv[x]) < 0) {
			perror(argv[x]);
			errs++;
		}
	}
	return(errs);
}
