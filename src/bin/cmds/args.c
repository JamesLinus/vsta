main(argc, argv)
	int argc;
	char **argv;
{
	int x;

	printf("argc = %d\n", argc);
	for (x = 0; x < argc; ++x) {
		printf(" argv[%d] = '%s'\n", x, argv[x]);
	}
	exit(0);
}
