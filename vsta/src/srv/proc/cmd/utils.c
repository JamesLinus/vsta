/*
 * utils.c
 *	Functions useful to both kill and ps
 */
#include "ps.h"
#include <mnttab.h>

extern port_t path_open(char *, int);

/*
 * mount_procfs()
 *	Mount /proc into our filesystem namespace
 */
void
mount_procfs(void)
{
	port_t port;
	
	port = path_open("fs/proc", ACC_READ);
	if (port < 0) {
		perror("/proc");
		exit(1);
	}
	(void)mountport("/proc", port);
}
