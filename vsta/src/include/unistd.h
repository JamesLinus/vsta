#ifndef _UNISTD_H
#define _UNISTD_H
/*
 * unistd.h
 *	A standards-defined file
 *
 * I don't have a POSIX manual around here!  I'll just throw in some
 * stuff I'm pretty sure it needs and hope to borrow a manual some
 * time soon.
 */
#include <sys/types.h>

/*
 * Third argument to lseek (and ftell) - also defined in <stdio.h>
 */
#if !defined(SEEK_SET) && !defined(SEEK_CUR) && !defined(SEEK_END)
#define SEEK_SET (0)
#define SEEK_CUR (1)
#define SEEK_END (2)
#endif

/*
 * Second argument to access()
 */
#define R_OK (4)
#define W_OK (2)
#define X_OK (1)
#define F_OK (0)

/*
 * Standard file number definitions
 */
#define STDIN_FILENO (0)
#define STDOUT_FILENO (1)
#define STDERR_FILENO (2)

/*
 * UID/GID related functions
 */
extern uid_t getuid(void);
extern uid_t geteuid(void);
extern gid_t getgid(void);
extern gid_t getegid(void);
extern char *getlogin(void);
extern int setuid(uid_t), seteuid(uid_t), setgid(gid_t), setegid(gid_t),
	setreuid(uid_t);

/*
 * POSIX things - not really useful to VSTa, but we try to emulate them as
 * much as possible
 */
extern int umask(int);
extern int chmod(const char *, int), fchmod(int, int);
extern int access(const char *, int);
extern int chown(const char *, uid_t, gid_t);

#endif /* _UNISTD_H */
