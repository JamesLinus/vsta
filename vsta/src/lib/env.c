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
extern int wstat();

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

/*
 * setenv()
 *	Set variable in environment to value
 */
setenv(char *name, char *val)
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
 * Usually called after a fork to establish a new base and
 * copy-on-write node.  Returns 0 on success, -1 on failure.
 */
setenv_init(char *base)
{
	char buf[80];
	int fd;

	/*
	 * If we have a new base, move down to it creating
	 * path elements as needed.
	 */
	if (base) {
		char *p, *q;

		/*
		 * Trim leading '/'s, add prefix /env and suffix
		 * "#".
		 */
		while (base[0] == '/') {
			++base;
		}
		sprintf(buf, "/env/%s/#", base);

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

	/*
	 * Otherwise use wstat() to move to our own environment node
	 */
	fd = open("/env", O_READ);
	return(wstat(__fd_port(fd), "fork"));
}
