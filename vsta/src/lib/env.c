/*
 * env.c
 *	Give a getenv/setenv environment from the /env server
 *
 * These routines assume the /env server is indeed mounted
 * at /env in the user's mount table.
 */
#include <std.h>
#include <fcntl.h>

extern char *rstat();

/*
 * getenv()
 *	Ask /env server for value
 */
char *
getenv(char *name)
{
	char *p, buf[256];
	int fd, len;

	/*
	 * Slashes in path mean absolute path to environment
	 * variable.
	 */
	if (strchr(name, '/')) {
		while (name[0] == '/') {
			++name;
		}
		sprintf(buf, "/env/%s", name);
	} else {
		/*
		 * Otherwise use our "home" node
		 */
		sprintf(buf, "/env/#/%s", name);
	}

	/*
	 * Access name, return 0 if not found
	 */
	fd = open(buf, O_RDONLY);
	if (fd < 0) {
		return(0);
	}

	/*
	 * Get # byte needed to represent, allocate buffer, and
	 * read into the buffer.
	 */
	p = rstat(__fd_port(fd), "size");
	if (p == 0) {
		close(fd);
		return(0);
	}
	len = atoi(p);
	p = malloc(len+1);
	p[len] = '\0';
	(void)read(fd, p, len);
	close(fd);
	return(p);
}
