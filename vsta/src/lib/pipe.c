/*
 * pipe.c
 *	Set up pipe using pipe manager
 */
#include <sys/fs.h>
#include <fcntl.h>
#include <stdio.h>
#include <std.h>

/*
 * For mapping open FILE's to popen() PID's
 */
struct pipemap {
	FILE *p_fp;		/* FP open */
	pid_t p_pid;		/* Corresponding PID */
	struct pipemap *p_next;	/* List of them */
};
static struct pipemap *pipes;

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

/*
 * popen()
 *	Open a pipe to a program
 */
FILE *
popen(const char *cmd, const char *mode)
{
	int fds[2];
	pid_t pid;
	FILE *fp;

	/*
	 * Get the underlying pipe
	 */
	if (pipe(fds) < 0) {
		return(0);
	}

	/*
	 * Get child process
	 */
	pid = fork();

	/*
	 * For child, plumb I/O so we hold only appropriate side of pipe
	 */
	if (pid == 0) {
		close(0); close(1); close(2);
		if (*mode == 'r') {
			close(fds[0]);
			(void)open("/dev/null", O_READ);
			dup2(fds[1], 1);
			dup2(fds[1], 2);
			close(fds[1]);
		} else {
			close(fds[1]);
			dup2(fds[0], 0);
			(void)open("/dev/null", O_READ);
			dup2(1, 2);
			close(fds[0]);
		}
		system(cmd);
		_exit(1);
	}

	/*
	 * Parent
	 */
	if (pid > 0) {
		if (*mode == 'r') {
			close(fds[1]);
			fp = fdopen(fds[0], mode);
			if (fp == 0) {
				close(fds[0]);
			}
		} else {
			close(fds[0]);
			fp = fdopen(fds[1], mode);
			if (fp == 0) {
				close(fds[1]);
			}
		}
		if (fp) {
			struct pipemap *p;

			/*
			 * Success.  Record FP->PID mapping for cleanup
			 */
			p = malloc(sizeof(struct pipemap));
			if (!p) {
				close(fds[0]); close(fds[1]);
				fclose(fp);
				return(0);
			}
			p->p_fp = fp;
			p->p_pid = pid;
			p->p_next = pipes;
			pipes = p;
		}
		return(fp);
	}

	/*
	 * Error
	 */
	close(fds[0]); close(fds[1]);
	return(0);
}

/*
 * pclose()
 *	Close resources associated with pipe
 */
int
pclose(FILE *fp)
{
	struct pipemap *p, **pp;
	pid_t pid;
	int status;

	/*
	 * Find our entry, remove us from the list
	 */
	for (pp = &pipes, p = *pp; p; p = *pp) {
		if (p->p_fp == fp) {
			*pp = p->p_next;
			break;
		}
		pp = &p->p_next;
	}

	/*
	 * Not a known popen() session
	 */
	if (!p) {
		return(__seterr(EBADF));
	}

	/*
	 * Close I/O, then wait for him to terminate
	 */
	pid = p->p_pid;
	free(p);
	fclose(fp);
	if (waitpid(pid, &status, 0) < 0) {
		return(-1);
	}

	return(status);
}
