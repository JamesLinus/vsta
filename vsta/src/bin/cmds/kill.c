#include <sys/fs.h>

main(int argc, char **argv)
{
	int x, pid;
	char *event = EKILL;

	for (x = 1; x < argc; ++x) {
		if (argv[x][0] == '-') {
			event = argv[x]+1;
		} else {
			pid = atoi(argv[1]);
			notify(pid, 0, event);
		}
	}
	return(0);
}
