#include <dirent.h>
#include <stdlib.h>
#include <sys/fs.h>
#include <fcntl.h>

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
	
	d = opendir("/proc");
	if (d == 0) {
		perror("/proc");
		exit(1);
	}

	pids = malloc(space * sizeof(pid_t));
	if (pids == 0) {
		perror("malloc");
		exit(1);
	}
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

	qsort((void *)pids, nelem, sizeof(pid_t), sort);

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
