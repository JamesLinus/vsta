#ifndef _FCNTL_H
#define _FCNTL_H
/*
 * fcntl.h
 *	Definitions for control of files
 */
#include <sys/types.h>

/*
 * Options for an open()
 */
#define O_READ (1)
#define O_RDONLY O_READ
#define O_WRITE (2)
#define O_WRONLY O_WRITE
#define O_RDWR (O_READ|O_WRITE)
#define O_CREAT (4)
#define O_TRUNC O_CREAT
#define O_EXCL (8)
#define O_APPEND (16)
#define O_TEXT (0)
#define O_BINARY (32)
#define O_DIR (64)

/*
 * Max # characters in a path.  Not clear this belongs here.
 */
#define MAXPATH (512)

extern int open(char *, int, ...), close(int),
	read(int fd, void *buf, int nbyte),
	write(int fd, void *buf, int nbyte),
	close(int), mkdir(char *),
	unlink(char *);
extern off_t lseek(int fd, off_t offset, int whence);

#endif /* _FCNTL_H */
