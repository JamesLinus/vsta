#ifndef _PASSWD_H
#define _PASSWD_H
/*
 * passwd.h
 *	Definitions for user information
 *
 * u_uid and u_gid are probably not what you expect; they are not
 * used to determine your abilities.  That is provided by u_perms.
 * u_uid is simply used to provide a unique identification of what
 * account was logged into such that the permission was granted.
 * u_gid is simply used to allow a group of permissions to easily
 * be grouped and granted to several accounts.
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/perm.h>

#define UI_STRLEN (32)			/* Max string length */

/*
 * All the dope on a user
 */
struct uinfo {
	char u_acct[UI_STRLEN];		/* Account name */
	char u_passwd[UI_STRLEN];	/* Password */
	char u_home[UI_STRLEN];		/* Home dir */
	char u_shell[UI_STRLEN];	/* Shell */
	char u_env[UI_STRLEN];		/* Environment path */
	struct perm			/* Permissions holding */
		u_perms[PROCPERMS];
	ulong u_uid;			/* UID associated with account */
	ulong u_gid;			/* GID mapping to more perms */
};

extern int getuinfo_name(char *, struct uinfo *);

#endif /* _PASSWD_H */
