#ifndef _STAT_H
#define _STAT_H
/*
 * stat.h
 *	The "stat" structure and stat function prototypes
 *
 * The "stat" structure doesn't exist in VSTa; this structure is mocked up
 * for programs requiring such an interface.  Down with large binary
 * structures!
 */
#include <sys/types.h>
#include <time.h>

typedef ulong mode_t;	/* POSIX types, "A name for everybody" */
typedef ulong nlink_t;
typedef ulong dev_t;
typedef ulong ino_t;

struct	stat {
	dev_t st_dev;
	ino_t st_ino;
	mode_t st_mode;
	nlink_t st_nlink;
	ulong st_uid, st_gid;
	dev_t st_rdev;
	off_t st_size;
	time_t st_atime, st_mtime, st_ctime;
	ulong st_blksize;
	ulong st_blocks;
};

#define S_IFMT		0xF000	/* file type mask */
#define S_IFDIR		0x4000	/* directory */
#define S_IFIFO		0x1000	/* FIFO special */
#define S_IFCHR		0x2000	/* character special */
#define S_IFBLK		0x3000	/* block special */
#define S_IFREG		0x0000	/* no bits--regular file */
#define S_IREAD		0x0004	/* owner may read */
#define S_IWRITE 	0x0002	/* owner may write */
#define S_IEXEC		0x0001	/* owner may execute */

#define S_ISREG(m)	(((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)	(((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)	(((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)	(((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m)	(((m) & S_IFMT) == S_IFIFO)

/*
 * Function prototypes for POSIX that use the VSTa stat mechanism
 */
extern int fstat(int fd, struct stat *s);
extern int stat(const char *f, struct stat *s);
extern int isatty(int fd);

#endif /* _STAT_H */
