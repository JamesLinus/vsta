/*
 * Filename:	mount.c
 * Author:	Dave Hudson <dave@humbug.demon.co.uk>
 * Started:	7th May 1994
 * Last Update: 7th May 1994
 * Implemented:	GNU GCC 1.42 (VSTa v1.3.1 port)
 *
 * Description:	Extension to the VSTa port of ash to provide the builtin
 *		commands mount and umount.  I've tried as much as possible
 *		to remain consistent with the "ash" coding style.
 */

#include <mnttab.h>
#include <sys/fs.h>
#include "error.h"


/*
 * mountcmd()
 *	Mount a specified port/path at a mount point
 */

int
mountcmd(argc, argv)  char **argv; {
	port_t port;

	if (argc != 3) {
		error("usage: mount <namer-path || port> <mount-point>");
		return -1;
	}
	port = path_open(argv[1], ACC_READ);
	if (port < 0) {
		error("can't get connection to server");
		return -1;
	}
	if (mountport(argv[2], port) < 0) {
		error("mount failed");
		return -1;
	}

	return 0;
}


/*
 * umountcmd()
 *	Un-mount all ports from the specified mount point
 */
 
int
umountcmd(argc, argv)  char **argv; {
	if (argc != 2) {
		error("usage: umount <mount-point>");
		return -1;
	}
	if (umount(argv[1], -1)) {
		error("umount failed");
		return -1;
	}
	
	return 0;
}
