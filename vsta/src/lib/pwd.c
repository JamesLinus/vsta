/*
 * pwd.c
 *	Group file functions
 */
#include <pwd.h>
#include <stdio.h>
#include <lib/hash.h>
#include <std.h>
#include <string.h>

static struct hash *uidhash = 0;	/* Mapping GID->struct passwd */

/*
 * field()
 *	Return indexed field from colon-separated list
 */
static char *
field(char *buf, int idx, char *deflt)
{
	char *p, *q;
	int x, len;
	static char *fldval = 0;

	/*
	 * Walk forward to indexed field
	 */
	p = buf;
	for (x = 0; x < idx; ++x) {
		p = strchr(p, ':');
		if (p == 0) {
			return(deflt);
		}
		++p;
	}

	/*
	 * Find terminator
	 */
	for (q = p+1; q && (*q != ':'); ++q)
		;

	/*
	 * Calculate length, get static string to hold value
	 */
	len = (q-p)+1;
	fldval = realloc(fldval, len);
	if (fldval == 0) {
		return(deflt);
	}

	/*
	 * Put null-terminated value in place, return it
	 */
	bcopy(p, fldval, len-1);
	p[len] = '\0';
	return(fldval);
}

/*
 * fill_hash()
 *	One-time read of passwd file into local cache
 */
static void
fill_hash(void)
{
	FILE *fp;
	char buf[256], *p;
	struct passwd *pw;

	/*
	 * Access passwd file
	 */
	if ((fp = fopen("/vsta/etc/passwd", "r")) == 0) {
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
		 * Allocate new passwd
		 */
		pw = malloc(sizeof(struct passwd));
		if (pw == 0) {
			break;
		}

		/*
		 * Fill in
		 */
		pw->pw_name = strdup(field(buf, 0, "???"));
		pw->pw_uid = atoi(field(buf, 2, "2"));
		pw->pw_gid = atoi(field(buf, 3, "2"));
		pw->pw_dir = strdup(field(buf, 6, "/vsta"));
		pw->pw_shell = strdup(field(buf, 8, "/vsta/bin/testsh"));

		/*
		 * Add to hash
		 */
		if (hash_insert(uidhash, pw->pw_uid, pw)) {
			/*
			 * XXX we lose the strdup()'ed fields, but no
			 * easy way to tell if they were the default
			 * values or strdup()'ed.
			 */
			free(pw);
			break;
		}
	}
	fclose(fp);
}

/*
 * getpwuid()
 *	Get struct passwd given UID
 */
struct passwd *
getpwuid(uid_t uid)
{
	/*
	 * Fill hash table once only
	 */
	if (uidhash == 0) {
		fill_hash();
	}

	/*
	 * Look up, return result
	 */
	return(hash_lookup(uidhash, uid));
}

/*
 * Encapsulates arguments to foreach function
 */
struct hasharg {
	char *name;
	struct passwd *passwd;
};

/*
 * namecheck()
 *	Check for match on name, end foreach when find it
 */
static
namecheck(struct passwd *pw, struct hasharg *ha)
{
	if (!strcmp(pw->pw_name, ha->name)) {
		ha->passwd = pw;
		return(1);
	}
	return(0);
}

/*
 * getpwnam()
 *	Get struct passwd given passwd name
 */
struct passwd *
getpwnam(char *name)
{
	struct hasharg ha;

	ha.passwd = 0;
	ha.name = name;
	hash_foreach(uidhash, namecheck, &ha);
	return(ha.passwd);
}
