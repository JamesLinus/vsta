#ifndef _STAT_H
#define _STAT_H
/*
 * stat.h
 *	The "stat" structure
 *
 * No such thing exists in VSTa; this structure is mocked up for
 * programs requiring such an interface.  Down with large binary
 * structures!
 */
#include <sys/types.h>

struct	stat {
	ushort st_dev, st_ino;
	ushort st_mode, st_nlink;
	ulong st_uid, st_gid;
	ulong st_rdev, st_size;
	ulong st_atime, st_mtime, st_ctime;
	ulong st_blksize;
};

#define S_IFMT		0xF000	/* file type mask */
#define S_IFDIR		0x4000	/* directory */
#define S_IFIFO		0x1000	/* FIFO special */
#define S_IFCHR		0x2000	/* character special */
#define S_IFBLK		0x3000	/* block special */
#define S_IFREG		0x0000	/* no bits--regular file */
#define S_IREAD		0x0400	/* owner may read */
#define S_IWRITE 	0x0200	/* owner may write */
#define S_IEXEC		0x0100	/* owner may execute */

#endif /* _STAT_H */
