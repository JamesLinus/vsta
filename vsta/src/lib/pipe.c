/*
 * pipe.c
 *	Set up pipe using pipe manager
 */
#include <sys/fs.h>
#include <fcntl.h>

/*
 * pipe()
 *	Set up pipe, place file descriptor value in array
 */
pipe(int *fds)
{
	port_t portw, portr;
	int fdw, fdr;
	char *p, buf[80];
	extern char *rstat();

	/*
	 * Create a new node, for reading/writing.  Reading, so we
	 * can do the rstat() and get its inode number.
	 */
	portw = path_open("fs/pipe:#", ACC_READ | ACC_WRITE | ACC_CREATE);
	if (portw < 0) {
		return(-1);
	}
	fdw = __fd_alloc(portw);

	/*
	 * Get its "inode number"
	 */
	p = rstat(portw, "inode");
	if (p == 0) {
		close(fdw);
		return(-1);
	}

	/*
	 * Open a distinct file descriptor for reading
	 */
	sprintf(buf, "fs/pipe:%s", p);
	portr = path_open(buf, ACC_READ);
	if (portr < 0) {
		close(fdw);
		return(-1);
	}
	fdr = __fd_alloc(portr);

	/*
	 * Fill in pair of fd's
	 */
	fds[0] = fdr;
	fds[1] = fdw;

	return(0);
}
