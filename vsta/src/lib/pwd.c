/*
 * pwd.c
 *	Group file functions
 */
#include <sys/perm.h>
#include <pwd.h>
#include <stdio.h>
#include <hash.h>
#include <std.h>
#include <string.h>
#include <paths.h>

static struct hash *uidhash = NULL;	/* Mapping UID to struct passwd */
static FILE *pwfp = NULL;		/* Pointer to passwd file */

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
	for (q = p + 1; *q && (*q != ':'); ++q)
		;

	/*
	 * Calculate length, get static string to hold value
	 */
	len = q - p;
	fldval = realloc(fldval, len + 1);
	if (fldval == 0) {
		return(deflt);
	}

	/*
	 * Put null-terminated value in place, return it
	 */
	bcopy(p, fldval, len);
	fldval[len] = '\0';
	return(fldval);
}

/*
 * read_pfile()
 *	Read the next passwd entry from a specified file
 *
 * We take a pointer to the file to be interrogated and read the next entry
 * into a passwd structure.  If there are no more entries or there's an error
 * we return NULL
 */
static struct passwd *
read_pwfile(FILE *fp, struct passwd *pw)
{
	char buf[256], *p;

	if (!fgets(buf, sizeof(buf)-1, fp)) {
		return(NULL);
	}

	/*
	 * Trim newline
	 */
	buf[strlen(buf)-1] = '\0';

	/*
	 * Fill in
	 */
	pw->pw_name = strdup(field(buf, 0, "???"));
	pw->pw_uid = atoi(field(buf, 2, "2"));
	pw->pw_gid = atoi(field(buf, 3, "2"));
	pw->pw_dir = strdup(field(buf, 6, "/vsta"));
	pw->pw_shell = strdup(field(buf, 8, _PATH_DEFAULT_SHELL));

	return(pw);
}

/*
 * fill_hash()
 *	One-time read of password file into local cache
 */
static void
fill_hash(void)
{
	FILE *fp;
	struct passwd *pw;
	int all_done = 0;

	/*
	 * Access password file
	 */
	if ((fp = fopen(_PATH_PASSWD, "r")) == 0) {
		return;
	}

	/*
	 * Allocate hash
	 */
	if ((uidhash = hash_alloc(16)) == NULL) {
		abort();
	}	

	/*
	 * Read lines
	 */
	while (!all_done) {
		/*
		 * Allocate new passwd structure
		 */
		if ((pw = malloc(sizeof(struct passwd))) == NULL) {
			all_done = 1;
			break;
		}

		if (read_pwfile(fp, pw) == NULL) {
			all_done = 1;
			break;
		}

		/*
		 * Add to hash
		 */
		if (hash_insert(uidhash, pw->pw_uid, pw)) {
			/*
			 * XXX we lose the strdup()'d fields, but there's no
			 * easy way to tell if they were the default
			 * values or the strdup()'d ones
			 */
			free(pw);
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
static int
namecheck(uid_t uid, struct passwd *pw, struct hasharg *ha)
{
	if (!strcmp(pw->pw_name, ha->name)) {
		ha->passwd = pw;
		return(1);
	}
	return(0);
}

/*
 * getpwnam()
 *	Get struct passwd given a user name
 */
struct passwd *
getpwnam(char *name)
{
	struct hasharg ha;

	/*
	 * Fill hash table once only
	 */
	if (uidhash == 0) {
		fill_hash();
	}
	ha.passwd = 0;
	ha.name = name;
	hash_foreach(uidhash, namecheck, &ha);
	return(ha.passwd);
}

/*
 * getpwent()
 *	Get next passwd file entry
 */
struct passwd *
getpwent(void)
{
	static struct passwd *pwd = NULL;

	if (pwfp == NULL) {
		if ((pwfp = fopen(_PATH_PASSWD, "r")) == NULL) {
			return(NULL);
		}
	}
	if (pwd == NULL) {
		if ((pwd = (struct passwd *)malloc(sizeof(struct passwd)))
				== NULL) {
			return(NULL);
		}
	}

	/*
	 * Now read the next entry
	 */
	return(read_pwfile(pwfp, pwd));
}

/*
 * fgetpwent()
 *	Get the next passwd file entry from the specified file
 */
struct passwd *
fgetpwent(FILE *stream)
{
	static struct passwd *pwd = NULL;

	if (stream == NULL) {
		return(NULL);
	}
	if (pwd == NULL) {
		if ((pwd = (struct passwd *)malloc(sizeof(struct passwd)))
				== NULL) {
			return(NULL);
		}
	}

	/*
	 * Now read the next entry
	 */
	return(read_pwfile(stream, pwd));
}

/*
 * setpwent()
 *	Make sure the next getpwent() references the first entry of the
 *	passwd file
 */
void
setpwent(void)
{
	if (pwfp) {
		rewind(pwfp);
	}
}

/*
 * endpwent()
 *	Close access to the passwd file
 */
void
endpwent(void)
{
	if (pwfp) {
		fclose(pwfp);
		pwfp = NULL;
	}
}

/*
 * getuid()
 *	Get UID from first permission record
 */
uid_t
getuid(void)
{
	struct perm me;

	if (perm_ctl(0, 0, &me) < 0) {
		return(0);
	}
	return(me.perm_uid);
}

/*
 * geteuid()
 *	Get effective UID - we don't have these of course!
 */
uid_t
geteuid(void)
{
	return(getuid());
}

/*
 * getlogin()
 *	Get UID from first permission record, map to a name
 */
char *
getlogin(void)
{
	struct passwd *pw;

	pw = getpwuid(getuid());
	if (pw == 0) {
		return(0);
	}
	return(pw->pw_name);
}

/*
 * getgid()
 *	Get group ID for current user
 */
gid_t
getgid(void)
{
	struct passwd *pw;

	pw = getpwuid(getuid());
	if (pw == 0) {
		return(0);
	}
	return(pw->pw_gid);
}

/*
 * getegid()
 *	Get effective GID - we don't have these either
 */
gid_t
getegid(void)
{
	return(getgid());
}
