/*
 * ps.c
 *	Report process status
 */
#include "ps.h"
#include <dirent.h>

/*
 * sort()
 *	Tell qsort() to sort by PID value
 */
static int
sort(void *v1, void *v2)
{
	const pid_t p1 = *(const pid_t *)v1;
	const pid_t p2 = *(const pid_t *)v2;
	return p1 - p2;
}

int
main(int argc, char **argv)
{
	DIR *d;
	struct dirent *de;
	pid_t *pids;
	int space = 32;
	int nelem = 0;
	int i;
	
	/*
	 * Set up, get ready to read through list of all processes
	 */
	mount_procfs();
	d = opendir("/proc");
	if (d == 0)
		exit(1);

	pids = malloc(space * sizeof(pid_t));
	if (pids == 0) {
		perror("malloc");
		exit(1);
	}

	/*
	 * Read list
	 */
	while (de = readdir(d)) {
		if (!strcmp(de->d_name, "kernel")) {
			continue;
		}
		pids[nelem] = atoi(de->d_name);
		nelem++;
		if (nelem == space) {
			space += 32;
			pids = realloc(pids, space * sizeof(pid_t));
			if (pids == 0) {
				perror("realloc");
				exit(1);
			}
		}
	}
	closedir(d);

	/*
	 * Sort by PID
	 */
	qsort((void *)pids, nelem, sizeof(pid_t), sort);

	/*
	 * Now dump them
	 */
	for (i = 0; i < nelem; i++) {
		char path[32];
		char status[128];
		int fd, n;

		sprintf(path, "/proc/%d/status", pids[i]);
		fd = open(path, O_RDONLY);
		if (fd == -1) {
			continue;
		}
		n = read(fd, status, sizeof(status));
		status[n] = '\0';
		printf(status);
		close(fd);
	}
	return(0);
}
