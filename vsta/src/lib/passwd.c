/*
 * passwd.c
 *	Routines for getting VSTa-style account information
 *
 * Format in password file is:
 *	account:password:uid:gid:name:baseperm:home:env:shell
 */
#include <passwd.h>
#include <stdio.h>
#include <std.h>

extern void parse_perm();

#define DEFSHELL "/vsta/bin/sh"
#define DEFHOME "/vsta"
#define DEFENV "/env"

/*
 * fillin()
 *	Given account record, fill in uinfo struct
 */
static void
fillin(char *rec, struct uinfo *u)
{
	int x;
	char *p;

	/*
	 * Default to something harmless
	 */
	u->u_gid = 2;
	strcpy(u->u_passwd, "*");
	strcpy(u->u_home, DEFHOME);
	strcpy(u->u_shell, DEFSHELL);
	strcpy(u->u_env, DEFENV);
	for (x = 0; x < PROCPERMS; ++x) {
		u->u_perms[x].perm_len = PERMLEN+1;
	}

	/*
	 * Password
	 */
	p = strchr(rec, ':');
	if (p) {
		*p++ = '\0';
	}
	strcpy(u->u_passwd, rec);
	if ((rec = p) == 0) {
		return;
	}

	/*
	 * UID
	 */
	u->u_uid = atoi(rec);
	rec = strchr(rec, ':');
	if (rec == 0) {
		return;
	}
	rec++;

	/*
	 * GID
	 */
	u->u_gid = atoi(rec);
	rec = strchr(rec, ':');
	if (rec == 0) {
		return;
	}
	rec++;

	/*
	 * Skip name
	 */
	rec = strchr(rec, ':');
	if (rec == 0) {
		return;
	}
	rec++;

	/*
	 * Parse permission string
	 */
	p = strchr(rec, ':');
	if (p) {
		*p++ = '\0';
	}
	parse_perm(&u->u_perms[0], rec);
	if ((rec = p) == 0) {
		return;
	}

	/*
	 * Parse home
	 */
	p = strchr(rec, ':');
	if (p) {
		*p++ = '\0';
	}
	strcpy(u->u_home, rec);
	if ((rec = p) == 0) {
		return;
	}

	/*
	 * Parse environment
	 */
	p = strchr(rec, ':');
	if (p) {
		*p++ = '\0';
	}
	strcpy(u->u_env, rec);
	if ((rec = p) == 0) {
		return;
	}

	/*
	 * The rest is the shell
	 */
	strcpy(u->u_shell, rec);
}

/*
 * getuinfo_name()
 *	Get account information given name
 *
 * Returns 0 on success, 1 on failure
 */
getuinfo_name(char *name, struct uinfo *u)
{
	FILE *fp;
	char *p, buf[80];

	/*
	 * Open password file
	 */
	fp = fopen(PASSWD, "r");
	if (fp == 0) {
		return(1);
	}

	/*
	 * Get lines out of file until EOF or match
	 */
	while (fgets(buf, sizeof(buf), fp)) {
		buf[strlen(buf)-1] = '\0';

		/*
		 * Chop off first field--account name
		 */
		p = strchr(buf, ':');
		if (p == 0) {
			continue;	/* Malformed */
		}

		/*
		 * Match?
		 */
		*p++ = '\0';
		if (!strcmp(name, buf)) {
			fclose(fp);
			strcpy(u->u_acct, name);
			fillin(p, u);
			return(0);
		}
	}
	fclose(fp);
	return(1);
}
