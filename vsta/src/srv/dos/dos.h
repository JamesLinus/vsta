#ifndef _DOS_H
#define _DOS_H
/*
 * dos.h
 *	Definitions for DOS-type filesystem
 *
 * Throughout this filesystem server no attempt has been made to
 * insulate from the endianness of the processor.  It is assumed that
 * if you are running with DOS disks you're an Intel-type processor.
 * If you're on something else, you probably will make a DOS-ish
 * filesystem for yourself, with your own byte order, and enjoy the
 * savings in CPU time.  The exception is floppies; for those,
 * perhaps the mtools would suffice.  Or just use tar.
 */
#include <sys/types.h>

#define SECSZ (512)		/* Bytes in a sector */

/*
 * This represents the cluster allocation for a directory or file.
 * The c_clust field points to an array of unsigned short's which
 * are the clusters allocated.  It is malloc()'ed, and realloc()'ed
 * as needed to change storage allocation.
 */
struct clust {
	ushort *c_clust;	/* Clusters allocated */
	uint c_nclust;		/* # entries in c_clust */
};

/*
 * An open DOS file/dir has one of these.  It is thrown away on last close
 * of a file; it is kepts indefinitely for directories, unless the
 * directory itself is deleted.  In the future, it might be worth keeping
 * it for a while to see if it'll be needed again.
 */
struct node {
	uint n_type;		/* Type field */
	struct clust *n_clust;	/* Block allocation for this file */
	uint n_mode;		/* Protection bits */
	uint n_refs;		/* Current open files on us */
	uint n_flags;		/* Flags */
	struct node *n_dir;	/* Dir we exist within */
	uint n_slot;		/*  ...index within */

	/* For T_FILE only */
	ulong n_len;		/* Our byte length */

	/* For T_DIR only */
	struct hash		/* Hash for index->file mapping */
		*n_files;
};

/*
 * Values for n_type.  The point is that this field is at the
 * same place for both, so you can tell one from the other.
 */
#define T_DIR 1		/* Directory */
#define T_FILE 2	/* File */

/*
 * Bits for n_flags
 */
#define N_DIRTY 1	/* Contents modified */

/*
 * Each open client has this state
 */
struct file {
	uint f_perm;		/* Things he can do */
	ulong f_pos;		/* Byte position in file */
	struct node *f_node;	/* Either a dosdir or a dosfile */
};

/*
 * A DOS directory entry
 */
struct directory {
	char name[8];		/* file name */
	char ext[3];		/* file extension */
	uint attr:8;		/* attribute byte */
	uchar reserved[10];	/* DOS reserved */
	uint time:16;		/* time stamp */
	uint date:16;		/* date stamp */
	uint start:16;		/* starting cluster number */
	uint size:32;		/* size of the file */
};

/*
 * Bits in "attr"
 */
#define DA_READONLY 0x01	/* Read only */
#define DA_HIDDEN 0x02		/* Hidden */
#define DA_SYSTEM 0x04		/* System */
#define DA_VOLUME 0x08		/* Volume label */
#define DA_DIR 0x10		/* Subdirectory */
#define DA_ARCHIVE 0x20		/* Needs archiving */

/*
 * Format of sector 0 in filesystem
 */
struct boot {
	uint jump:24;		/* Jump to boot code */
	char banner[8];		/* OEM name & version */
	uint secsize:16;	/* Bytes per sector hopefully 512 */
	uint clsize:8;		/* Cluster size in sectors */
	uint nrsvsect:16;	/* Number of reserved (boot) sectors */
	uint nfat:8;		/* Number of FAT tables hopefully 2 */
	uint dirents:16;	/* Number of directory slots */
	uint psect:16;		/* Total sectors on disk */
	uint descr:8;		/* Media descriptor=first byte of FAT */
	uint fatlen:16;		/* Sectors in FAT */
	uint nsect:16;		/* Sectors/track */
	uint nheads:16;		/* Heads */
	uint nhs:32;		/* number of hidden sectors */
	uint bigsect:32;	/* big total sectors */
};

/*
 * Parameters for block cache
 */
#define NCACHE (40)
extern uint clsize;
#define BLOCKSIZE (clsize)

/*
 * Values for FAT slots.  These are FAT16 values; FAT12 values are
 * mapped into these.
 */
#define FAT_RESERVED (0xFFF0)	/* Start of reserved value range */
#define FAT_DEFECT (0xFFF7)	/* Cluster w. defective block */
#define FAT_EOF (0xFFF8)	/* Start of EOF range */
#define FAT_END (0xFFFF)	/* End of reserved range */

/*
 * Node handling routines
 */
extern void rw_init(void);	/* Set up root directory */
extern struct node *rootdir;	/*  ...it's always here */
extern struct node		/* Look up name in directory */
	*do_lookup(struct node *, char *);
extern void			/* Add/remove a reference to a node */
	ref_node(struct node *),
	deref_node(struct node *);

/*
 * Cluster handling routines
 */
extern void clust_init(void);	/* Bring FAT tables into memory */
extern struct clust		/* Allocate representation of chain */
	*alloc_clust(uint);
extern void			/*  ...free this representation */
	free_clust(struct clust *);
extern void clust_sync(void);	/* Sync FAT table to disk */
extern int			/* Set cluster allocation */
	clust_setlen(struct clust *, ulong);

/*
 * Block cache
 */
extern void *bget(int), *bdata(void *), bdirty(void *);
extern void binit(void), bsync(void), bfree(void *);

/*
 * Directory stuff
 */
extern struct node *dir_look(struct node *, char *),
	*dir_newfile(struct file *, char *, int);
extern void dir_remove(struct node *);
extern int dir_copy(struct node *, uint, struct directory *);

#endif /* _DOS_H */
