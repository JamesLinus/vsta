/*
 * test22.c
 *	See if waitpid() works
 */
main()
{
	int x, st, pid;

	pid = fork();
	if (pid == 0) {
		sleep(2);
		exit(0);
	}
	x = waitpid(pid, &st, 0);
	printf("Waitpid returns %d, status %d\n", x, st);
	return(0);
}
