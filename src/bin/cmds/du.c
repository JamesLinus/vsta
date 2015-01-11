/*
 * du.c
 *	Disk usage
 */
#include <stat.h>
#include <dirent.h>
#include <alloc.h>
#include <fcntl.h>

static int kflag = 1;	/* Display in K (default) */

#ifdef LATER

#include <hash.h>

static struct hash *nodes;

/*
 * Record of files visited, so we can count hard links only once
 */
struct node {
	port_name n_fs;		/* Server */
	ulong n_ino;		/* Unique index within server */
	struct node *n_next;	/* For hash overflow */
};

/*
 * visited()
 *	Tell if we've seen this file before
 */
static int
visited(struct stat *s, port_name pn)
{
	uint hval = s->st_ino ^ pn;
	struct node *n, **np;

	/*
	 * See if there are any possible collisions
	 */
	n = hash_lookup(nodes, hval);
	if (n) {
		/*
		 * Yup.  Scan all matches on this hash value
		 */
		while (n) {
			if ((n->n_fs == pn) && (n->n_ino == s->st_ino)) {
				return(1);
			}
			np = &n->n_next;
			n = *np;
		}
	} else {
		np = 0;
	}

	/*
	 * No match.  Get a node to record our visit here.
	 */
	n = malloc(sizeof(struct node));
	if (n == 0) {
		perror("du: malloc");
		exit(1);
	}
	n->n_fs = pn;
	n->n_ino = s->st_ino;
	n->n_next = 0;

	/*
	 * Tack us onto an existing list, or hash us as a new entry
	 */
	if (np) {
		*np = n;
	} else {
		if (hash_insert(nodes, hval, n)) {
			perror("du: hash");
			exit(1);
		}
	}
	return(0);
}

#else /* LATER */

static int
visited(struct stat *s, port_name pn)
{
	return(0);
}

#endif /* LATER */
/*
 * du()
 *	Do "disk use" for named entry
 */
static ulong
du(char *path)
{
	struct stat sb;
	DIR *dir;
	struct dirent *de;
	ulong tally = 0;
	port_name pn = 0;
	char *delim;

	/*
	 * Prepare to walk entry list
	 */
	dir = opendir(".");
	if (dir == 0) {
		perror(".");
		return;
	}
	delim = path + strlen(path);

	/*
	 * Walk entries
	 */
	while (de = readdir(dir)) {
		/*
		 * Get basics
		 */
		if (stat(de->d_name, &sb) < 0) {
			perror(de->d_name);
			continue;
		}

		/*
		 * Calculate usage
		 */
		switch (sb.st_mode & S_IFMT) {
		case S_IFDIR:		/* Recurse on subdirs */
			sprintf(delim, "/%s", de->d_name);
			chdir(de->d_name);
			tally += du(path);
			chdir("..");
			*delim = '\0';

			/* VVV fall into IFREG to tally dir entries */
		case S_IFREG:
			if (pn == 0) {
				int fd;

				fd = open(".", O_READ);
				if (fd >= 0) {
					pn = msg_portname(__port(fd));
					close(fd);
				}
			}
			if (!visited(&sb, pn)) {
				tally += sb.st_blocks * sb.st_blksize;
			}
			break;
		default:		/* Ignore others */
			break;
		}
	}
	closedir(dir);
	printf("%8D %s\n",
		kflag ? (tally / 1024) : (tally / 512), path);
	return(tally);
}

int
main(int argc, char **argv)
{
	int x, done = 0;
	char pathbuf[1024];

#ifdef LATER
	/*
	 * Get the hash, pick some reasonably large size
	 */
	nodes = hash_alloc(1023);
#endif

	/*
	 * Parse args
	 */
	for (x = 1; (x < argc) && !done; ++x) {
		if (argv[x][0] == '-') {
			switch (argv[x][1]) {
			case 'k': kflag = 1; break;
			case 'b': kflag = 0; break;
			case '-': done = 1; break;
			default:
				fprintf(stderr, "Unknown switch: %s\n",
					argv[x]);
				exit(1);
			}
		} else {
			break;
		}
	}
	if (x >= argc) {
		strcpy(pathbuf, ".");
		(void)du(pathbuf);
	} else {
		char cwdbuf[1024];

		getcwd(cwdbuf, sizeof(cwdbuf));
		for ( ; x < argc; ++x) {
			if (argv[x][0] != '/') {
				chdir(cwdbuf);
			}
			strcpy(pathbuf, argv[x]);
			chdir(pathbuf);
			(void)du(pathbuf);
		}
	}
	return(0);
}
