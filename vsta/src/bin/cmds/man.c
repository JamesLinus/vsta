/*
 * man.c
 *	A simple interface to the VSTa documentation files
 */
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <std.h>

/*
 * Where map of topics to man files is kept
 */
#define MANDIR "/vsta/doc/man"
#define MANDB MANDIR "/map"

/*
 * Number of matches we handle on ambiguity
 */
#define MAXMATCHES 20

/*
 * Number of sections
 */
#define NUMSECT 10

/*
 * Structure describing an entry in the manual mapfile
 */
struct mapent {
	int m_section;		/* Section number */
	char *m_name;		/* Full name */
	char *m_file;		/* File stored in */
	struct mapent *m_next;	/* Linked list under a section */
};

/*
 * Preserve order of entries found in DB
 */
struct maphd {
	struct mapent		/* Head/tail of list of entries */
		*m_hd, *m_tl;
} map[NUMSECT];

/*
 * load_mandb()
 *	Read in database so we can scan it quickly
 */
static void
load_mandb(void)
{
	FILE *fp;
	unsigned int section = 1;
	char buf[128], *p;
	struct mapent *m;

	/*
	 * Open manual database
	 */
	fp = fopen(MANDB, "r");
	if (fp == 0) {
		perror(MANDB);
		exit(1);
	}

	/*
	 * Read entries
	 */
	while (fgets(buf, sizeof(buf), fp)) {
		/*
		 * Ignore comments and blank lines
		 */
		if ((buf[0] == '\n') || (buf[0] == '#')) {
			continue;
		}

		/*
		 * Remove trailing \n
		 */
		buf[strlen(buf)-1] = '\0';

		/*
		 * Section header
		 */
		if (buf[0] == '/') {
			section = atoi(buf+1);
			if (section >= NUMSECT) {
				printf("Error: %s is corrupt\n", MANDB);
				exit(1);
			}
			continue;
		}

		/*
		 * File entry.  Break into name and file.
		 */
		p = strchr(buf, ':');
		if (p == 0) {
			printf("Error: %s is corrupt\n", MANDB);
			exit(1);
		}
		*p++ = '\0';
		while (isspace(*p)) {
			++p;
		}
		if (*p == '\0') {
			printf("Error: %s is corrupt\n", MANDB);
			exit(1);
		}

		/*
		 * Allocate an element and fill in
		 */
		m = malloc(sizeof(struct mapent));
		if (m == 0) {
			perror("man: init");
			exit(1);
		}
		m->m_name = strdup(buf);
		m->m_section = section;
		m->m_file = strdup(p);
		if ((m->m_name == 0) || (m->m_file == 0)) {
			perror("man: init");
			exit(1);
		}

		/*
		 * Add to list for appropriate section
		 */
		if (map[section].m_tl) {
			map[section].m_tl->m_next = m;
		}
		map[section].m_tl = m;
		if (map[section].m_hd == 0) {
			map[section].m_hd = m;
		}
	}
	fclose(fp);
}

/*
 * find_matches2()
 *	Do scan of single section
 *
 * Same kind of return value as find_matches()
 */
static int
find_matches2(struct maphd *mh, char *name, struct mapent **matches)
{
	struct mapent *m;
	int len, nmatch;

	/*
	 * Scan section
	 */
	len = strlen(name);
	nmatch = 0;
	for (m = mh->m_hd; m; m = m->m_next) {
		if (!strncmp(m->m_name, name, len)) {
			/*
			 * Special case for exact match
			 */
			if (!strcmp(m->m_name, name)) {
				matches[0] = m;
				return(1);
			}

			/*
			 * Check for too many matches
			 */
			if (nmatch >= MAXMATCHES) {
				return(MAXMATCHES+1);
			}

			/*
			 * Save match, increment count
			 */
			matches[nmatch++] = m;
		}
	}
	return(nmatch);
}

/*
 * find_matches()
 *	Scan in-core copy of database for matches
 *
 * Returns count of matches found, and fills matches[] with pointers
 * to those entries.  Returns MAXMATCHES+1 if too many are found.
 */
static int
find_matches(int sect, char *name, struct mapent **matches)
{
	int x, base;
	struct mapent *tmpmatch[MAXMATCHES];

	/*
	 * If specific section, just do it and return
	 */
	if (sect) {
		return(find_matches2(&map[sect], name, matches));
	}

	/*
	 * Otherwise accumulate scans of successive sections
	 */
	for (x = 0, base = 0; x < NUMSECT; ++x) {
		int nmatch;

		nmatch = find_matches2(&map[x], name, tmpmatch);
		if ((base + nmatch) > MAXMATCHES) {
			return(nmatch);
		}
		bcopy(tmpmatch, matches+base, nmatch * sizeof(char *));
		base += nmatch;
	}
	return(base);
}

/*
 * show_man()
 *	Display man page for given entry
 */
static void
show_man(struct mapent *m)
{
	char buf[128], *pager;
	static char tmpf[] = "/tmp/mantXXXXXX";

	mktemp(tmpf);
	sprintf(buf, "roff %s/%d/%s > %s",
		MANDIR, m->m_section, m->m_file, tmpf);
	system(buf);
	pager = getenv("PAGER");
	if (pager == 0) {
		pager = "less";
	}
	sprintf(buf, "%s %s", pager, tmpf);
	system(buf);
	unlink(tmpf);
}

/*
 * show_matches()
 *	Display multiple matches nicely
 */
static void
show_matches(struct mapent **matches, int nmatch)
{
	int x, nent = 0, nline = 0, col = 0;
	char buf[128], buf2[128];

	/*
	 * Display comma-seperated list, wrapping at end of line
	 */
	buf[0] = '\0';
	for (x = 0; x < nmatch; ++x) {
		struct mapent *m = matches[x];

		/*
		 * Generate image of what we'd like to add
		 */
		sprintf(buf2, "%s%s(%d)",
			nent ? ", " : "",
			m->m_name, m->m_section);

		/*
		 * If it won't fit, flush the line, reset the counters,
		 * and push "x" back a notch so we can restart processing
		 * of the current entry.
		 */
		if ((col + strlen(buf2)) > 76) {
			printf("%s,\n", buf);
			strcpy(buf, " ");
			nline += 1;
			col = nent = 0;
			x -= 1;
			continue;
		}

		/*
		 * Add on this entry, update counts
		 */
		strcat(buf, buf2);
		nent += 1;
		col += strlen(buf2);
	}
	if (nent) {
		printf("%s\n", buf);
	}
}

main(int argc, char **argv)
{
	int x, section = 0;
	struct mapent *matches[MAXMATCHES];

	load_mandb();
	for (x = 1; x < argc; ++x) {
		if (isdigit(argv[x][0])) {
			section = atoi(argv[x]);
		} else {
			int y;

			y = find_matches(section, argv[x], matches);
			if (y > MAXMATCHES) {
				printf("More than %d matches for %s\n",
					MAXMATCHES, argv[x]);
				continue;
			}
			switch (y) {
			case 0:
				printf("No man page for %s\n", argv[x]);
				break;
			case 1:
				show_man(matches[0]);
				break;
			default:
				printf("Matches:\n");
				show_matches(matches, y);
			}
		}
	}
	return(0);
}
