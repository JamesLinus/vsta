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
#include <sys/msg.h>

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
#define N_DEL 2		/* Node has been removed */

/*
 * Each open client has this state
 */
struct file {
	uint f_perm;		/* Things he can do */
	ulong f_pos;		/* Byte position in file */
	struct node *f_node;	/* Either a dosdir or a dosfile */
	long f_rename_id;	/* Transaction # for rename() */
	struct msg		/*  ...message for that transaction */
		f_rename_msg;
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
	uint jump1:16;		/* Jump to boot code 0 */
	uint jump2:8;
	char banner[8];		/* OEM name & version  3 */
	uint secsize0:8;	/* Bytes per sector hopefully 512  11 */
	uint secsize1:8;
	uint clsize:8;		/* Cluster size in sectors 13 */
	uint nrsvsect:16;	/* Number of reserved (boot) sectors 14 */
	uint nfat:8;		/* Number of FAT tables hopefully 2 16 */
	uint dirents0:8;	/* Number of directory slots 17 */
	uint dirents1:8;
	uint psect0:8;		/* Total sectors on disk 19 */
	uint psect1:8;
	uint descr:8;		/* Media descriptor=first byte of FAT 21 */
	uint fatlen:16;		/* Sectors in FAT 22 */
	uint nsect:16;		/* Sectors/track 24 */
	uint nheads:16;		/* Heads 26 */
	uint nhs:32;		/* number of hidden sectors 28 */
	uint bigsect:32;	/* big total sectors 32 */
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
extern void fat_sync(void);	/* Sync FAT table to disk */
extern int			/* Set cluster allocation */
	clust_setlen(struct clust *, ulong);

/*
 * Block cache
 */
extern void *bget(int), *bdata(void *), bdirty(void *);
extern void binit(void), bsync(void), bfree(void *);

/*
 * Other routines
 */
extern int valid_fname(char *, int);
extern struct node *dir_look(struct node *, char *),
	*dir_newfile(struct file *, char *, int);
extern int dir_empty(struct node *);
extern void dir_remove(struct node *);
extern int dir_copy(struct node *, uint, struct directory *);
extern void dir_setlen(struct node *);
extern void root_sync(void), sync(void);
extern void fat_init(void), binit(void), dir_init(void);
extern void dos_open(struct msg *, struct file *),
	dos_close(struct file *),
	dos_rename(struct msg *, struct file *),
	cancel_rename(struct file *),
	dos_read(struct msg *, struct file *),
	dos_write(struct msg *, struct file *),
	dos_remove(struct msg *, struct file *),
	dos_stat(struct msg *, struct file *),
	dos_fid(struct msg *, struct file *);

/*
 * Global data
 */
extern int blkdev;
extern struct boot bootb;
extern uint dirents;
extern struct node *rootdir;

#endif /* _DOS_H */
