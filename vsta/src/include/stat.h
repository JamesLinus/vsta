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

/*
 * The POSIX stat structure
 */
struct stat {
	dev_t	st_dev;		/* ID of device containing dir entry */
	ino_t	st_ino;		/* inode number */
	mode_t	st_mode;	/* file mode - POSIX permissions */
	nlink_t st_nlink;	/* number of links */
	uid_t	st_uid;		/* user ID of the file's owner */
	gid_t	st_gid;		/* group ID of the file's group */
	dev_t	st_rdev;	/* ID of device if special, eg block special */
	off_t	st_size;	/* file size in bytes */
	time_t	st_atime;	/* last access time */
	time_t	st_mtime;	/* last modification time */
	time_t	st_ctime;	/* last status change time */
	ulong	st_blksize;	/* prefered I/O block size */
	ulong	st_blocks;	/* number of blocks allocated */
};

/*
 * major/minor device number emulation - definitions
 */
#define major(dev) (((dev) >> 8) & 0xffffff)
#define minor(dev) ((dev) & 0xff)
#define makedev(maj, min) (((maj) << 8) | ((min) & 0x0ff))

/*
 * POSIX mode definitions
 */
#define S_IFMT		0xF000	/* file type mask */
#define S_IFDIR		0x4000	/* directory */
#define S_IFIFO		0x1000	/* FIFO special */
#define S_IFCHR		0x2000	/* character special */
#define S_IFBLK		0x3000	/* block special */
#define S_IFREG		0x0000	/* no bits--regular file */

#define S_IRWXU		00700	/* owner may read, write and execute */
#define S_IRUSR		00400	/* owner may read */
#define S_IWUSR		00200	/* owner may write */
#define S_IXUSR		00100	/* owner may execute */

#define S_IRWXG		00070	/* group may read, write and execute */
#define S_IRGRP		00040	/* group may read */
#define S_IWGRP		00020	/* group may write */
#define S_IXGRP		00010	/* group may execute */

#define S_IRWXO		00007	/* other may read, write and execute */
#define S_IROTH		00004	/* other may read */
#define S_IWOTH		00002	/* other may write */
#define S_IXOTH		00001	/* other may execute */

#define S_IREAD		S_IRUSR	/* owner may read */
#define S_IWRITE	S_IWUSR	/* owner may write */
#define S_IEXEC		S_IXUSR	/* owner may execute */

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
