/*
 * ls.c
 *	A simple ls utility
 */
#include <stdio.h>
#include <dirent.h>
#include <std.h>
#include <pwd.h>
#include <sys/param.h>
#include <sys/fs.h>
#include <sys/perm.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>

static int ndir;	/* # dirs being displayed */
static int cols = 80;	/* Columns on display */

static int lflag = 0;	/* -l flag */

/*
 * sort()
 *	Sort entries by name
 */
static int
sort(void *v1, void *v2)
{
	return(strcmp(*(char **)v1, *(char **)v2));
}

/*
 * prcols()
 *	Print array of strings in columnar fashion
 */
static void
prcols(char **v)
{
	int maxlen, x, col, entcol, nelem;

	/*
	 * Scan once to find longest string
	 */
	maxlen = 0;
	for (nelem = 0; v[nelem]; ++nelem) {
		x = strlen(v[nelem]);
		if (x > maxlen) {
			maxlen = x;
		}
	}

	/*
	 * Put entries in order
	 */
	qsort((void *)v, nelem, sizeof(char *), sort);

	/*
	 * Calculate how many columns that makes, and how many
	 * entries will end up in each column.
	 */
	col = cols / (maxlen+1);
	entcol = nelem/col;
	if (nelem % col) {
		entcol += 1;
	}

	/*
	 * Dump out the strings
	 */
	for (x = 0; x < entcol; ++x) {
		int y;

		for (y = 0; y < col; ++y) {
			int idx;

			idx = (y*entcol)+x;
			if (idx < nelem) {
				printf("%s", v[idx]);
			} else {
				/*
				 * No more out here, so finish
				 * inner loop now.
				 */
				putchar('\n');
				break;
			}

			/*
			 * Pad all but last column--put newline
			 * after last column.
			 */
			if (y < (col-1)) {
				int l;

				for (l = strlen(v[idx]); l <= maxlen; ++l) {
					putchar(' ');
				}
			} else {
				putchar('\n');
			}
		}
	}
}

/*
 * setopt()
 *	Set option flag
 */
static void
setopt(char c)
{
	switch (c) {
	case 'l':
		lflag = 1;
		break;
	default:
		fprintf(stderr, "Unknown option: %c\n", c);
		exit(1);
	}
}

/*
 * fld()
 *	Pick a field from the entire stat string
 */
static char *
fld(char **args, char *field, char *deflt)
{
	uint x, len;
	char *p;

	len = strlen(field);
	for (x = 0; p = args[x]; ++x) {
		/*
		 * See if we match
		 */
		if (!strncmp(p, field, len)) {
			if (p[len] == '=') {
				return(p+len+1);
			}
		}
	}
	return(deflt);
}

/*
 * explode()
 *	Return vector out to fields in stat buffer
 */
char **
explode(char *statbuf)
{
	uint nfield = 0;
	char *p, **args = 0;

	p = statbuf;
	while (p && *p) {
		nfield += 1;
		args = realloc(args, (nfield+1) * sizeof(char *));
		if (args == 0) {
			return(0);
		}
		args[nfield-1] = p;
		p = strchr(p, '\n');
		if (p) {
			*p++ = '\0';
		}
		if ((args[nfield-1] = strdup(args[nfield-1])) == 0) {
			return(0);
		}
	}
	args[nfield] = 0;
	return(args);
}

/*
 * prprot()
 *	Grind the protection info into a presentable format
 */
static void
prprot(char *perm, char *acc)
{
	char *p;
	uchar ids[PERMLEN];
	uint x;
	extern char *cvt_id();

	/*
	 * Heading, need both fields
	 */
	printf("\tprot: ");
	if (!perm || !acc) {
		printf("not available\n");
		return;
	}

	/*
	 * Convert dotted notation to binary, look up in our
	 * ID table and print.
	 */
	p = perm;
	x = 0;
	while (p) {
		ids[x] = atoi(p);
		p = strchr(p, '/'); if (p) ++p;
		x += 1;
	}
	p = cvt_id(ids, x);
	printf("%s, access: ", p); free(p);

	/*
	 * Now display access bits symbolically
	 */
	p = acc;
	x = 0;
	while (p) {
		if (x) printf(".");
		x = atoi(p);
		if (x & ACC_READ) {printf("R"); x &= ~ACC_READ;}
		if (x & ACC_WRITE) {printf("W"); x &= ~ACC_WRITE;}
		if (x & ACC_CHMOD) {printf("C"); x &= ~ACC_CHMOD;}
		if (x & ACC_EXEC) {printf("X"); x &= ~ACC_EXEC;}
		if (x) printf("|0x%x", x);
		p = strchr(p, '/'); if (p) ++p;
		x = 1;
	}
	printf("\n");
}

/*
 * ls_l()
 *	List stuff with full stats
 */
static void
ls_l(struct dirent *de)
{
	char *s, **sv;
	int fd;
	struct passwd *pwd;
	extern char *rstat();

	/*
	 * Read in the stat string for the entry
	 */
	fd = open(de->d_name, O_RDONLY);
	if (fd < 0) {
		perror(de->d_name);
		return;
	}
	s = rstat(__fd_port(fd), (char *)0);
	close(fd);
	if (s == 0) {
		perror(de->d_name);
		return;
	}
	sv = explode(s);
	if (sv == 0) {
		perror(de->d_name);
		return;
	}

	/*
	 * Convert UID to a printing string
	 */
	pwd = getpwuid(atoi(fld(sv, "owner", "0")));

	/*
	 * Print out various fields, use prprot() for the protection
	 * labels.
	 */
	printf("%s: %s, %d bytes, owner %s\n",
		de->d_name,
		fld(sv, "type", "file"),
		atoi(fld(sv, "size", "0")),
		pwd ? (pwd->pw_name) : ("not set"));
	prprot(fld(sv, "perm", 0), fld(sv, "acc", 0));
}

/*
 * ls()
 *	Do ls with current options on named place
 */
static void
ls(char *path)
{
	DIR *d;
	struct dirent *de;
	char **v = 0;
	int nelem;
	struct stat st;

	if (stat(path, &st)) {
		perror("stat failed");
		return;
	}
	if (!S_ISDIR(st.st_mode)) {
		if (lflag) {
			struct dirent de;

			de.d_namlen = strlen(path);
			strcpy(de.d_name, path);
			ls_l(&de);
		} else {
			char *v[2];
			v[0] = path;
			v[1] = 0;
			prcols(v);
		}
		return;
	} else {
		/*
		 * Prefix with name of dir if multiples
		 */
		if (ndir > 1) {
			printf("%s:\n", path);
		}
	}

	/*
	 * Open access to named place
	 */
	d = opendir(path);
	if (d == 0) {
		perror(path);
		return;
	}

	/*
	 * Read elements
	 */
	nelem = 0;
	while (de = readdir(d)) {
		if (lflag) {
			ls_l(de);
			continue;
		}
		nelem += 1;
		v = realloc(v, sizeof(char *)*(nelem+1));
		if (v == 0) {
			perror("ls");
			exit(1);
		}
		if ((v[nelem-1] = strdup(de->d_name)) == 0) {
			perror("ls");
			exit(1);
		}
	}
	closedir(d);

	/*
	 * Dump them (if any)
	 */
	if (nelem > 0) {
		int x;

		/*
		 * Put terminating null, then print
		 */
		v[nelem] = 0;
		prcols(v);

		/*
		 * Free memory
		 */
		for (x = 0; x < nelem; ++x) {
			free(v[x]);
		}
		free(v);
	}
}

main(argc, argv)
	int argc;
	char **argv;
{
	int x;

	/*
	 * Parse leading args
	 */
	for (x = 1; x < argc; ++x) {
		char *p;

		if (argv[x][0] != '-') {
			break;
		}
		for (p = &argv[x][1]; *p; ++p) {
			setopt(*p);
		}
	}

	/*
	 * Do ls on rest of file or dirnames
	 */
	ndir = argc-x;
	if (ndir == 0) {
		ls(".");
	} else {
		for (; x < argc; ++x) {
			ls(argv[x]);
		}
	}

	return(0);
}
