/*
 * Try out scanf
 */
extern char *rstat();

main()
{
	int x;
	char *p;

	printf("isatty stdin -> %d\n", isatty(0));
	p = rstat(__fd_port(0), "type");
	printf("type stdin -> '%s'\n", p ? p : "(null)");
	printf("Enter number:\n");
	scanf("%d", &x);
	printf("Got: %d\n", x);
	return(0);
}
