/*
 * env.c
 *	Give a getenv/setenv environment from the /env server
 *
 * These routines assume the /env server is indeed mounted
 * at /env in the user's mount table.
 */
#include <std.h>
#include <fcntl.h>
#include <sys/fs.h>
#include <sys/ports.h>

extern char *rstat();

/*
 * getenv()
 *	Ask /env server for value
 */
char *
getenv(const char *name)
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

/*
 * setenv()
 *	Set variable in environment to value
 */
setenv(const char *name, const char *val)
{
	char buf[256];
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
	 * Write name, create as needed
	 */
	fd = open(buf, O_WRITE|O_CREAT, 0600);
	if (fd < 0) {
		return(-1);
	}

	/*
	 * Write value
	 */
	len = strlen(val);
	if (write(fd, val, len) != len) {
		return(-1);
	}
	close(fd);

	return(0);
}

/*
 * setenv_init()
 *	Initialize environment
 *
 * Usually called after a login to establish a new base and
 * copy-on-write node.  Returns 0 on success, -1 on failure.
 */
setenv_init(char *base)
{
	char buf[80];
	int fd;
	port_t port;
	char *p, *q;

	/*
	 * Connect afresh to the server
	 */
	port = msg_connect(PORT_ENV, ACC_READ);
	if (port < 0) {
		return(-1);
	}

	/*
	 * Throw away the current /env mount
	 */
	umount("/env", -1);

	/*
	 * Mount our new /env point
	 */
	if (mountport("/env", port) < 0) {
		return(-1);
	}

	/*
	 * Put home in root or where specified
	 */
	if (base) {
		while (base[0] == '/') {
			++base;
		}
		sprintf(buf, "/env/%s/#", base);
	} else {
		sprintf(buf, "/env/#");
	}

	/*
	 * Skip first two slashes (from above; they should
	 * definitely exist)
	 */
	p = strchr(buf, '/');
	p = strchr(p, '/');
	++p;

	/*
	 * mkdir successive elements
	 */
	do {
		p = strchr(p, '/');
		if (p) {
			*p = '\0';
		}
		if ((fd = open(buf, O_READ)) < 0) {
			if (mkdir(buf) < 0) {
				return(-1);
			}
		} else {
			close(fd);
		}
		if (p) {
			*p++ = '/';
		}
	} while(p);

	return(0);
}
