/*
 * env.c
 *	Give a getenv/setenv environment from the /env server
 *
 * These routines assume the /env server is indeed mounted
 * at /env in the user's mount table.
 */
#include <std.h>
#include <fcntl.h>
#include <alloc.h>
#include <dirent.h>
#include <stat.h>
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
 * putenv()
 *	Another POSIX interface, with name=val format
 */
int
putenv(const char *p)
{
	char *name, *sep;
	int len;

	/*
	 * Find '=' part of NAME=VAL
	 */
	sep = strchr(p, '=');
	if (!sep) {
		return(__seterr(EINVAL));
	}

	/*
	 * Get a private copy of the NAME part on the local stack
	 */
	len = sep - p;
	name = alloca(len + 1);
	bcopy(p, name, len);
	name[len] = '\0';

	/*
	 * Use our primary routine
	 */
	return(setenv(name, sep+1));
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

	/*
	 * Record our login environment root as an environment
	 * variable.
	 */
	if (base) {
		sprintf(buf, "/env/%s/ENVROOT", base);
		fd = open(buf, O_WRITE | O_CREAT, 0600);
		if (fd >= 0) {
			sprintf(buf, "/env/%s", base);
			(void)write(fd, buf, strlen(buf));
			close(fd);
		}
	}

	return(0);
}

/*
 * add_envs()
 *	Walk the dir, add in any new entries
 */
static char **
add_envs(char *path, char **envp, DIR *d)
{
	struct dirent *de;
	int x, len, fd;
	char buf[256];
	struct stat sb;

	while (de = readdir(d)) {
		/*
		 * Scan our currently known names, and see if
		 * any match this one.
		 */
		len = strlen(de->d_name);
		for (x = 0; envp[x]; ++x) {
			if (!strncmp(envp[x], de->d_name, len) &&
					(envp[x][len] == '=')) {
				break;
			}
		}

		/*
		 * Already had it; keep going
		 */
		if (envp[x]) {
			continue;
		}

		/*
		 * We didn't find one; increase the size of the
		 * array and put it in.
		 */
		envp = realloc(envp, (x+2)*sizeof(char *));
		sprintf(buf, "%s/%s", path, de->d_name);

		/*
		 * Access the node; we need to know its size,
		 * and then we'll read it as the variable's
		 * value.
		 */
		fd = open(buf, O_READ);
		if ((fd < 0) || (fstat(fd, &sb) < 0) ||
				((sb.st_mode & S_IFMT) != S_IFREG)) {
			envp[x] = 0;
			continue;
		}
		envp[x] = malloc(len + 1 + sb.st_size + 1);
		sprintf(envp[x], "%s=", de->d_name);
		(void)read(fd, envp[x]+len+1, sb.st_size);

		/*
		 * We have our new entry; clean up and continue
		 */
		close(fd);
		envp[x+1] = 0;
	}
	return(envp);
}

/*
 * __get_environ()
 *	Convert snapshot of dynamic hierarchical environ into array
 *
 * We have to gather our per-process environment, then walk upwards
 * from our login environment path, filling in further values which
 * haven't been overriden by previous ones.  Bleh.
 */
const char **
__get_environ(void)
{
	static char **my_env;
	DIR *d;
	char *path, *p;

	/*
	 * If we've already calculated it, just return the answer.
	 * If they wanted dynamically updated data, they should've
	 * used getenv() in the first place.
	 */
	if (my_env) {
		return((const char **)my_env);
	}

	/*
	 * Create the initial, empty, array.  It'll be realloc()'ed
	 * as needed.
	 */
	my_env = malloc(sizeof(char *));
	my_env[0] = 0;

	/*
	 * Get our per-process copy-on-write environment variables
	 */
	d = opendir("/env/#");
	if (d) {
		my_env = add_envs("/env/#", my_env, d);
		closedir(d);
	}

	/*
	 * Get our environment path, as specified at login
	 */
	path = getenv("ENVROOT");
	if (!path) {
		return((const char **)my_env);
	}
	p = alloca(strlen(path)+1);
	strcpy(p, path);
	path = p;

	/*
	 * Walk up each level, accumulating entries
	 */
	for (;;) {

		/*
		 * Access next level.  Drop out when we can't.
		 */
		d = opendir(path);
		if (!d) {
			break;
		}

		/*
		 * Fold in its entries
		 */
		my_env = add_envs(path, my_env, d);
		closedir(d);

		/*
		 * Trim one path element from the user environment
		 * path; we're done when we've processed the last
		 * (highest) element, or we've at /env.
		 */
		p = strrchr(path, '/');
		if (p) {
			if (!strcmp(p, "/env")) {
				break;
			}
			*p = '\0';
		} else {
			break;
		}
	}
	return((const char **)my_env);
}
