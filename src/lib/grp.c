/*
 * grp.c
 *	Group file functions
 */
#include <grp.h>
#include <stdio.h>
#include <hash.h>
#include <std.h>
#include <string.h>
#include <paths.h>

static struct hash *gidhash = NULL;	/* Mapping GID to struct group */
static FILE *gfp = NULL;		/* Pointer to group file */

/*
 * read_gfile()
 *	Read the next group entry from a specified file
 *
 * We take a pointer to the file to be interrogated and read the next entry
 * into a group structure.  If there are no more entries or there's an error
 * we return NULL
 */
static struct group *
read_gfile(FILE *fp, struct group *g)
{
	char buf[256], *p;

	if (!fgets(buf, sizeof(buf) - 1, fp)) {
		return(NULL);
	}

	/*
	 * Trim newline
	 */
	buf[strlen(buf) - 1] = '\0';

	/*
	 * Fill in
	 */
	p = strchr(buf, ':');
	if (p == 0) {
		return(NULL);
	}
	*p++ = '\0';
	g->gr_name = strdup(buf);
	g->gr_gid = atoi(p);
	g->gr_mem = 0;
	p = strchr(p, ':');
	if (p) {
		++p;
		g->gr_ids = strdup(p);
	} else {
		g->gr_ids = 0;
	}

	return(g);
}

/*
 * fill_hash()
 *	One-time read of group file into local cache
 */
static void
fill_hash(void)
{
	FILE *fp;
	struct group *g;
	int all_done = 0;

	/*
	 * Access group file
	 */
	if ((fp = fopen(_PATH_GROUP, "r")) == 0) {
		return;
	}

	/*
	 * Allocate hash
	 */
	if ((gidhash = hash_alloc(16)) == NULL) {
		abort();
	}	

	/*
	 * Read lines
	 */
	while (!all_done) {
		/*
		 * Allocate new group
		 */
		if ((g = malloc(sizeof(struct group))) == NULL) {
			all_done = 1;
			break;
		}

		if (read_gfile(fp, g) == NULL) {
			all_done = 1;
			break;
		}

		/*
		 * Add to hash
		 */
		if (hash_insert(gidhash, g->gr_gid, g)) {
			free(g->gr_name);
			free(g);
		}
	}
	fclose(fp);
}

/*
 * getgrgid()
 *	Get struct group given group ID
 */
struct group *
getgrgid(gid_t gid)
{
	/*
	 * Fill hash table once only
	 */
	if (gidhash == 0) {
		fill_hash();
	}

	/*
	 * Look up, return result
	 */
	return(hash_lookup(gidhash, gid));
}

/*
 * Encapsulates arguments to foreach function
 */
struct hasharg {
	char *name;
	struct group *group;
};

/*
 * namecheck()
 *	Check for match on name, end foreach when find it
 */
static int
namecheck(gid_t gid, struct group *g, struct hasharg *ha)
{
	if (!strcmp(g->gr_name, ha->name)) {
		ha->group = g;
		return(1);
	}
	return(0);
}

/*
 * getgrnam()
 *	Get struct group given group name
 */
struct group *
getgrnam(char *name)
{
	struct hasharg ha;

	ha.group = 0;
	ha.name = name;
	hash_foreach(gidhash, namecheck, &ha);
	return(ha.group);
}

/*
 * getgrent()
 *	Get next group file entry
 */
struct group *
getgrent(void)
{
	static struct group *gr = NULL;

	if (gfp == NULL) {
		if ((gfp = fopen(_PATH_GROUP, "r")) == NULL) {
			return(NULL);
		}
	}
	if (gr == NULL) {
		if ((gr = (struct group *)malloc(sizeof(struct group)))
				== NULL) {
			return(NULL);
		}
		gr->gr_name = NULL;
	} else {
		if (gr->gr_name != NULL) {
			free(gr->gr_name);
		}
	}

	/*
	 * Now read the next entry
	 */
	return(read_gfile(gfp, gr));
}

/*
 * fgetgrent()
 *	Get the next group file entry from the specified file
 */
struct group *
fgetgrent(FILE *stream)
{
	static struct group *gr = NULL;

	if (stream == NULL) {
		return(NULL);
	}
	if (gr == NULL) {
		if ((gr = (struct group *)malloc(sizeof(struct group)))
				== NULL) {
			return(NULL);
		}
		gr->gr_name = NULL;
	} else {
		if (gr->gr_name != NULL) {
			free(gr->gr_name);
		}
	}

	/*
	 * Now read the next entry
	 */
	return(read_gfile(stream, gr));
}

/*
 * setgrent()
 *	Make sure the next getgrent() references the first entry of the
 *	group file
 */
void
setgrent(void)
{
	if (gfp) {
		rewind(gfp);
	}
}

/*
 * endgrent()
 *	Close access to the group file
 */
void
endgrent(void)
{
	if (gfp) {
		fclose(gfp);
		gfp = NULL;
	}
}
