#ifndef _BFS_H
#define _BFS_H
/*
 * bfs.h
 *	Data structures in boot filesystem
 *
 * BFS is a very simple contiguous-allocation filesystem.  Free blocks
 * are always consumed at the end of the filesystem; when these are
 * exhausted, the filesystem is compacted and all current free space
 * coalesced at the end.
 *
 * BFS is not written as an interactive filesystem; it is single-
 * threaded.
 */
#include <sys/types.h>
#include <sys/fs.h>

#define BLOCKSIZE 1024		/* Size of blocks in filesystem */
#define NDIRBLOCKS 4		/* # blocks used for directory entries */
#define NAMELEN 16		/* Max chars in filename (-1 for null) */
#define SMAGIC 0x121314		/* Magic # to tell a BFS superblock */
#define NCACHE 32		/* # blocks to cache in memory */
#define MAXINODE 64		/* # inodes open at once */
#define ROOTINO \
	((struct inode *)0)	/* Special inode ptr for root */
/* Convert bytes to # block which would hold that */
#define BLOCKS(bytes) \
	((bytes + (BLOCKSIZE-1)) / BLOCKSIZE)
/*
 * Structure of directory entries
 */
struct dirent {
	char d_name[NAMELEN];	/* Name of file */
	uint d_inum;		/* Inode # */
	uint d_start;		/* Starting block # for entry */
	uint d_len;		/* Length of file in bytes */
};

/*
 * Structure of first block in filesystem
 */
struct super {
	uint s_magic;		/* Magic # ID for superblock */
	uint s_nblocks;		/* Total blocks in filesystem */
	uint s_free;		/* Number currently unused */
	uint s_nextfree;	/* Next free block */
	struct dirent		/* Directory entries start here */
		s_dir[1];
};

/*
 * A way to talk about the position of a directory entry on the
 * block device.
 */
struct dirmap {
	uint d_blkno;	/* Block # of dir slot */
	uint d_off;	/* Offset within that block */
};

/*
 * Our per-open-file data structure
 */
struct file {
	struct inode	/* Current inode */
		*f_inode;
	uint f_pos;	/* Current file offset */
	int f_write;	/* Flag if this open allowed to write */
};

/*
 * Our per-file info
 */
struct inode {
	uint i_num;	/* Inode # */
	struct dirmap	/* Corresponding directory entry info */
		i_dir;
	uint i_refs;	/* # open files on the node */
};

#endif /* _BFS_H */
