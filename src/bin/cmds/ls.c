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
#include <termios.h>
#include <fdl.h>
#include <getopt.h>

static int ndir;	/* # dirs being displayed */
static int cols = 80;	/* Columns on display */

static int lflag = 0,	/* -l flag */
	Fflag = 0,	/* -F flag */
	oneflag = 0,	/* -1 flag */
	dflag = 0,	/* -d flag */
	aflag = 0;	/* -a flag */

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
 * get_attrs()
 *	Return attribute->value pairs for named entry
 *
 * Returns NULL if it can't get them for any reason
 */
static char **
get_attrs(const char *path)
{
	char *s, **sv = NULL;
	int fd;

	fd = open(path, O_RDONLY);
	if (fd >= 0) {
		s = rstat(__fd_port(fd), (char *)0);
		close(fd);
		if (s) {
			sv = explode(s);
		}
	}
	return(sv);
}

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
	 * Calculate how many columns that makes, and how many
	 * entries will end up in each column.
	 */
	col = cols / (maxlen+1);
	entcol = nelem/col;
	if (nelem % col) {
		entcol += 1;
	}
	if (oneflag) {
		entcol = nelem;
		col = 1;
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
 * usage()
 *	Print usage
 */
void
usage(int val)
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "\t-a\t\tDo not hide entries starting with .\n");
	fprintf(stderr, "\t-d\t\tList directory entries instead of contents\n");
	fprintf(stderr, "\t-F\t\tAppend indicator (one of */) to entries\n");
	fprintf(stderr, "\t-l\t\tUse a long listing format\n");
	fprintf(stderr, "\t-1\t\tList one file per line\n");
	fprintf(stderr, "\t-h, --help\tDisplay this help and exit\n");
	exit(val);
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

	if (!args) {
		return(deflt);
	}
	len = strlen(field);
	for (x = 0; (p = args[x]); ++x) {
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
	char protstr[32];
	char tmpstr[32];

	/*
	 * Heading, need both fields
	 */
	if (!perm || !acc) {
		printf("-----        ???.???     ");
		return;
	}


	/*
	 * Now display access bits symbolically
	 */
	p = acc;
	x = 0;
	strcpy(protstr, "");
	while (p) {
		if (x) strcat(protstr, ".");
		x = atoi(p);
		if (x & ACC_READ) {strcat(protstr, "R"); x &= ~ACC_READ;}
		if (x & ACC_WRITE) {strcat(protstr, "W"); x &= ~ACC_WRITE;}
		if (x & ACC_CHMOD) {strcat(protstr, "C"); x &= ~ACC_CHMOD;}
		if (x & ACC_EXEC) {strcat(protstr, "X"); x &= ~ACC_EXEC;}
		if (x) {sprintf(tmpstr, "|0x%x", x); strcat(protstr, tmpstr);}
		p = strchr(p, '/'); if (p) ++p;
		x = 1;
	}
	printf("%-12s ", protstr);
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
	printf("%-12s", p); free(p);
}

/*
 * printname()
 *	Print the name of the file with -F formatting if needed
 */
char *
printname(char *name, char *acc, char *type)
{
	static char fname[_NAMLEN];
	char *p;
	uint x;

	if (Fflag==0)
	{
		return name;
	}
	if (strcmp("d", type) == 0)
	{
		sprintf(fname, "%s/", name);	
		return fname;
	}
	p = acc;
	x = 0;
	while (p) {
		x = atoi(p);
		if (x & ACC_READ) {x &= ~ACC_READ;}
		if (x & ACC_WRITE) {x &= ~ACC_WRITE;}
		if (x & ACC_CHMOD) {x &= ~ACC_CHMOD;}
		if (x & ACC_EXEC) {sprintf(fname, "%s*", name); return fname;}
		p = strchr(p, '/'); if (p) ++p;
		x = 1;
	}
	return name;
}

/*
 * ls_l()
 *	List stuff with full stats
 */
static void
ls_l(char *path)
{
	char **sv;
	struct passwd *pwd;
	time_t time;
	char timestr[32];

	/*
	 * Inhibit display of '.<name>' unless -a (all)
	 */
	if ((aflag == 0) && (path[0] == '.')) {
		return;
	}

	/*
	 * Read in the stat string for the entry
	 */
	sv = get_attrs(path);

	/*
	 * Print out various fields
	 */
	pwd = getpwuid(atoi(fld(sv, "owner", "0")));
	time = atoi(fld(sv, "mtime", "0"));
	strftime(timestr, 18, "%b %d %H:%M %Y", gmtime(&time));
	printf("%s ", fld(sv, "type", "-"));
	prprot(fld(sv, "perm", 0), fld(sv, "acc", 0));
	printf("%-6s %8d %-17s %s\n",
		pwd ? (pwd->pw_name) : ("not set"),
		atoi(fld(sv, "size", "0")),
		timestr,
		printname(path, fld(sv, "acc", 0), fld(sv, "type", "-")));
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
	char **v = NULL, **sv;
	int x, nelem;
	struct stat st;
	char buf[_NAMLEN];

	if (stat(path, &st)) {
		perror(path);
		return;
	}

	/*
	 * If we've been pointed at a single entry, just list it
	 */
	if (!S_ISDIR(st.st_mode) || (dflag == 1)) {
		char *vp[2];

		/*
		 * ls -l of a particular node
		 */
		if (lflag) {
			struct dirent de;

			de.d_namlen = strlen(path);
			strcpy(de.d_name, path);
			ls_l(de.d_name);
			return;
		}

		/*
		 * If Fflag, read in the stat string for the entry and
		 * display it prettily.  If not Fflag, avoid the overhead
		 * of gathering the rstat() information.
		 */
		if (Fflag) {
			sv = get_attrs(path);

			/*
			 * Display it, and return
			 */
			sprintf(buf, "%s",
				sv ?  printname(path, fld(sv, "acc", 0),
					fld(sv, "type", "f")) :
				    path);
			vp[0] = buf;
		} else {
			vp[0] = path;
		}
		vp[1] = NULL;
		prcols(vp);
		return;
	}

	/*
	 * Prefix with name of dir if multiples
	 */
	if (ndir > 1) {
		printf("%s:\n", path);
	}

	/*
	 * Open access to named place
	 */
	d = opendir(path);
	if (d == NULL) {
		perror(path);
		return;
	}
	chdir (path);

	/*
	 * Read elements
	 */
	nelem = 0;
	while ((de = readdir(d))) {
		/*
		 * Ignore "hidden" unless -a
		 */
		if (!aflag && (de->d_name[0] == '.')) {
			continue;
		}

		/*
		 * Ok, we're going to have another entry;
		 * make room for it in our vector of names.
		 */
		nelem += 1;
		v = realloc(v, sizeof(char *)*(nelem+1));
		if (v == 0) {
			perror("ls");
			exit(1);
		}
		v[nelem-1] = strdup(de->d_name);
	}
	closedir(d);

	/*
	 * Dump them (if any)
	 */
	if (nelem > 0) {
		/*
		 * Put terminating null
		 */
		v[nelem] = 0;

		/*
		 * Put entries in order
		 */
		qsort((void *)v, nelem, sizeof(char *), sort);

		/*
		 * Display them either as a column, or individually
		 * with ls -l detail.
		 */
		if (lflag) {
			for (x = 0; x < nelem; ++x) {
				ls_l(v[x]);
			}
		} else {
			if (Fflag) {
				for (x = 0; x < nelem; ++x) {
					v[x] = realloc(v[x],
						strlen(v[x]) + 3);

					/*
					 * Read in the stat string for
					 * the entry and modify the name
					 * with its -F attributes.
					 */
					sv = get_attrs(v[x]);
					if (sv) {
						strcpy(v[x],
						 printname(v[x],
						 fld(sv, "acc", 0),
						 fld(sv, "type", "f")));
					}
				}
			}

			/*
			 * Display file names in columns
			 */
			prcols(v);
		}

		/*
		 * Free memory
		 */
		for (x = 0; x < nelem; ++x) {
			free(v[x]);
		}
		free(v);
	}
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	int x, rows;
	char currpath[_NAMLEN];

	while ((x = getopt(argc, argv, "lF1dah")) > 0) {
		switch (x) {
		case 'l':
			lflag = 1;
			break;
		case 'F':
			Fflag = 1;
			break;
		case '1':
			oneflag = 1;
			break;
		case 'd':
			dflag = 1;
			break;
		case 'a':
			aflag = 1;
			break;
		case 'h':
			usage(0);
		default:
			fprintf(stderr, "Unknown option: %c\n", x);
			usage(1);
		}
	}

	/*
	 * If from a terminal and can get geometry, override default
	 */
	(void)tcgetsize(0, &rows, &cols);

	/*
	 * Do ls on rest of file or dirnames
	 */
	ndir = argc-optind;
	if (ndir == 0) {
		ls(".");
	} else {
		for (x = optind; x < argc; ++x) {
			getcwd(currpath, sizeof (currpath));
			ls(argv[x]);
			chdir(currpath);
		}
	}

	return(0);
}
