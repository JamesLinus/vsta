#include <sys/fs.h>

main(int argc, char **argv)
{
	int x, pid, pgrp = 0;
	char *event = EKILL;

	for (x = 1; x < argc; ++x) {
		if (argv[x][0] == '-') {
			if (!strcmp(argv[x], "-p")) {
				pgrp = 1;
			} else {
				event = argv[x]+1;
			}
		} else {
			pid = atoi(argv[x]);
			notify(pid, pgrp ? -1 : 0, event);
		}
	}
	return(0);
}
