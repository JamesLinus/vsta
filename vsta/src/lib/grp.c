/*
 * grp.c
 *	Group file functions
 */
#include <grp.h>
#include <stdio.h>
#include <hash.h>
#include <std.h>
#include <string.h>

static struct hash *gidhash = 0;	/* Mapping GID->struct group */

/*
 * fill_hash()
 *	One-time read of group file into local cache
 */
static void
fill_hash(void)
{
	FILE *fp;
	char buf[256], *p;
	struct group *g;

	/*
	 * Access group file
	 */
	if ((fp = fopen("/vsta/etc/groups", "r")) == 0) {
		return;
	}

	/*
	 * Read lines
	 */
	buf[sizeof(buf)-1] = '\0';
	while (fgets(buf, sizeof(buf)-1, fp)) {
		/*
		 * Trim newline
		 */
		buf[strlen(buf)-1] = '\0';

		/*
		 * Allocate new group
		 */
		g = malloc(sizeof(struct group));
		if (g == 0) {
			break;
		}

		/*
		 * Fill in
		 */
		p = strchr(buf, ':');
		if (p == 0) {
			free(g);
			continue;
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

		/*
		 * Add to hash
		 */
		if (hash_insert(gidhash, g->gr_gid, g)) {
			free(g->gr_name);
			free(g);
			break;
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
static
namecheck(struct group *g, struct hasharg *ha)
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
