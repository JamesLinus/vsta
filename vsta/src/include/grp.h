#ifndef _GRP_H
#define _GRP_H
/*
 * grp.h
 *	"groups", ala VSTa
 *
 * Much of the POSIX group concept is missing; we emulate
 * what we can.
 */
#include <sys/types.h>

/*
 * Group information
 */
struct group {
	char *gr_name;		/* Name of group */
	gid_t gr_gid;		/* GID */
	char **gr_mem;		/* Members (not tabulated) */
	char *gr_ids;		/* List of IDs granted this group */
};

/*
 * Procedures
 */
extern struct group *getgrgid(gid_t),
	*getgrnam(char *);

#endif /* _GRP_H */
