/*
 * perm.c
 *	Permission/protection mapping
 *
 * This is mostly a study in frustration; the POSIX style of interface
 * disables much of the expressive power of the VSTa protection system.
 */
#include <unistd.h>
#include <fcntl.h>

/*
 * umask()
 *	Set default protection mask
 */
umask(int newmask)
{
	return(0600);
}

/*
 * chmod()
 *	Change mode of file
 */
chmod(char *file, int mode)
{
	return(0);
}

/*
 * access()
 *	Tell if we can access a file
 *
 * Ignores the effective/real dichotomy, since we don't really
 * have it as such.
 */
access(char *file, int mode)
{
	int fd;

	if (mode & W_OK) {
		fd = open(file, O_READ|O_WRITE);
	} else {
		fd = open(file, O_READ);
	}
	if (fd >= 0) {
		close(fd);
		return(0);
	}
	return(-1);
}
