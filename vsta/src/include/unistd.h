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

/*
 * Third argument to lseek (and ftell)
 */
#define SEEK_SET (0)
#define SEEK_CUR (1)
#define SEEK_END (2)

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

#endif /* _UNISTD_H */
