#ifndef _PWD_H
#define _PWD_H
/*
 * pwd.h
 *	Password file functions
 *
 * This represents a subset of what VSTa puts in the passwd file
 */
#include <sys/types.h>

/*
 * Types
 */
struct passwd {
	char *pw_name;		/* Name of account */
	uid_t pw_uid;		/* UID */
	gid_t pw_gid;		/* GID */
	char *pw_dir;		/* Home dir */
	char *pw_shell;		/* Shell */
};

/*
 * Prototypes
 */
extern struct passwd *getpwuid(uid_t),
	*getpwnam(char *);

#endif /* _PWD_H */
