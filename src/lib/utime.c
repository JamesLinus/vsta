/*
 * utime.c
 *	File timestamp interface
 */
#include <sys/msg.h>
#include <time.h>
#include <fcntl.h>
#include <utime.h>

/*
 * utimes()
 *	Set file's timestamp from a timeval
 */
int
utimes(const char *path, struct timeval *tvp)
{
	port_t port;
	int fd, x;
	char buf[64];

	/*
	 * Access file
	 */
	fd = open(path, O_WRITE);
	if (fd < 0) {
		return(-1);
	}
	port = __fd_port(fd);

	/*
	 * Null time means now
	 */
	if (tvp == 0) {
		struct timeval tv[2];
		struct time now;

		time_get(&now);
		tv[0].tv_sec = tv[1].tv_sec = now.t_sec;
		tv[0].tv_usec = tv[1].tv_usec = now.t_usec;
		tvp = tv;
	}

	/*
	 * Ask for atime to be set
	 */
	sprintf(buf, "atime=%d\n", tvp->tv_sec);
	x = wstat(port, buf);
	if (x >= 0) {
		/*
		 * If successful, ask for mtime to be set
		 */
		tvp += 1;
		sprintf(buf, "mtime=%d\n", tvp->tv_sec);
		x = wstat(port, buf);
	}

	/*
	 * Done with file, return result
	 */
	close(fd);
	return(x);
}

/*
 * utime()
 *	Just like utimes(), but make up new types to call it with
 */
int
utime(const char *path, struct utimbuf *ut)
{
	struct timeval tv[2];

	if (ut == 0) {
		return(utimes(path, 0));
	}
	tv[0].tv_sec = ut->actime;
	tv[0].tv_usec = 0;
	tv[1].tv_sec = ut->modtime;
	tv[1].tv_usec = 0;
	return(utimes(path, tv));
}
