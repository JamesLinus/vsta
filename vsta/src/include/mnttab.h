#ifndef _MNTTAB_H
#define _MNTTAB_H
/*
 * mnttab.h
 *	Data structures for mount table
 *
 * The mount table is organized as an array of entries describing
 * mount point strings.  Hanging off of each entry is then a linked
 * list of directories mounted at this name.  A pathname lookup
 * thus entails finding the longest matching path from the mnttab,
 * and then trying the entries under this mnttab slot in order until
 * an open is successful or you run out of entries to try.
 */
#include <sys/types.h>

/*
 * The mount table is an array of these
 */
struct mnttab {
	char *m_name;			/* Name mounted under */
	int m_len;			/* strlen(m_name) */
	struct mntent *m_entries;	/* Entries to lookup for this point */
};

/*
 * One or more of these per mount point
 */
struct mntent {
	port_t m_port;		/* Port to ask */
	struct mntent *m_next;	/* Next in list */
};

/*
 * Routines for manipulating mounts
 */
extern int mount(char *, char *), mountport(char *, port_t);
extern int umount(char *, port_t);
extern ulong __mount_size(void);
extern void __mount_save(char *);
extern char *__mount_restore(char *);

#endif /* _MNTTAB_H */
