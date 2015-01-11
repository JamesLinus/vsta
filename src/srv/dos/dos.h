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
#include <time.h>

#define SECSZ (512)		/* Bytes in a sector */
#define MSDOS_FAT12 (4085)	/* Max sectors in a FAT-12 filesystem */

/*
 * This represents the cluster allocation for a directory or file.
 * The c_clust field points to an array of claddr_t's which
 * are the clusters allocated.  It is malloc()'ed, and realloc()'ed
 * as needed to change storage allocation.
 */
typedef uint claddr_t;
struct clust {
	claddr_t *c_clust;	/* Clusters allocated */
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
	ulong n_inum;		/* Inode #, for stat/FS_FID */

	/* For T_FILE only */
	ulong n_len;		/* Our byte length */

	/* For T_DIR only */
	struct hash		/* Hash for index->file mapping */
		*n_files;
};

/*
 * Values for n_type
 */
#define T_DIR 1		/* Directory */
#define T_FILE 2	/* File */
#define T_SYM 3		/* Symlink */

/*
 * Bits for n_flags
 */
#define N_DIRTY 1	/* Contents modified */
#define N_DEL 2		/* Node has been removed */
#define N_FID 4		/* Node has had an FS_FID done, may be cached */

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
	char name[8];		/* 0  file name */
	char ext[3];		/* 8  file extension */
	uint attr:8;		/* 11 attribute byte */
	uchar reserved[8];	/* 12 DOS reserved */
	uint startHi:16;	/* 20 starting cluster (hi 16 bits, FAT32) */
	uint time:16;		/* 22 time stamp */
	uint date:16;		/* 24 date stamp */
	uint start:16;		/* 26 starting cluster (low 16 bits) */
	uint size:32;		/* 28 size of the file */
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
#define DA_VFAT (0x0f)		/* Illegal combo == vFAT */

/*
 * Special char for name, indicating deleted
 */
#define DN_DEL (0xE5)

/*
 * The start/startHi fields need to be handled differently
 * for FAT-32 as opposed to earlier filesystems.
 */
extern uint fat_size;
#define START(d) ((fat_size == 32) ? \
	(((d)->startHi << 16) | (d)->start) : \
	(d)->start)
#define SETSTART(d, v) if (fat_size == 32) { \
		(d)->startHi = ((v) >> 16); (d)->start = (v);} \
	else { (d)->start = (v); }

/*
 * VFAT sub-entry
 */
#define VSE_1SIZE (5)
#define VSE_2SIZE (6)
#define VSE_3SIZE (2)
#define VSE_NAME_SIZE (VSE_1SIZE + VSE_2SIZE + VSE_3SIZE)
struct dirVSE {
	uchar dv_id;			/* See flags below */
	uchar dv_name1[VSE_1SIZE*2];
	uchar dv_attr;			/* Fixed value, below */
	uchar dv_hash;			/* Always 0 */
	uchar dv_sum;			/* Checksum of short name */
	uchar dv_name2[VSE_2SIZE*2];
	ushort dv_sector;		/* Always 0 */
	uchar dv_name3[VSE_3SIZE*2];
};
#define VSE_ID_LAST (0x40)		/* Last VSE segment */
#define VSE_ID_MASK (0x1F)		/*  ...low bits are ID index */
#define VSE_ID_MAX (20)			/* 20 VSE's permits 256 char name */
#define VSE_ATTR_VFAT DA_VFAT		/* Map struct directory -> dirVSE */
#define VSE_MAX_NAME (256)		/* Max filename length */

/*
 * Shape of bytes at offset 36 in boot block for FAT 12/16
 * (unused, included here for completeness only)
 */
struct oldboot {
	uchar physdrive;	/* 36 physical drive ? */
	uchar reserved;		/* 37 reserved */
	uchar dos4;		/* 38 dos > 4.0 diskette */
	uint serial;       	/* 39 serial number */
	char label[11];		/* 43 disk label */
	char fat_type[8];	/* 54 FAT type */

	uchar res_2m;		/* 62 reserved by 2M */
	uchar CheckSum;		/* 63 2M checksum (not used) */
	uchar fmt_2mf;		/* 64 2MF format version */
	uchar wt;		/* 65 1 if write track after format */
	uchar rate_0;		/* 66 data transfer rate on track 0 */
	uchar rate_any;		/* 67 data transfer rate on track<>0 */
	ushort BootP;		/* 68 offset to boot program */
	ushort Infp0;		/* 70 T1: information for track 0 */
	ushort InfpX;		/* 72 T2: information for track<>0 */
	ushort InfTm;		/* 74 T3: track sectors size table */
	ushort DateF;		/* 76 Format date */
	ushort TimeF;		/* 78 Format time */
};

/*
 * Shape of bytes at offset 36 in boot block for FAT-32
 */
struct fat32 {
	uint bigFat;		/* 36 nb of sectors per FAT */
	ushort extFlags;     	/* 40 extension flags */
	ushort fsVersion;	/* 42 ? */
	uint rootCluster;	/* 44 start cluster of root dir */
	ushort infoSector;	/* 48 changeable global info */
	ushort backupBoot;	/* 50 back up boot sector */
};

/*
 * Format of sector 0 in filesystem
 */
struct boot {
	uint jump1:16;		/*  0 Jump to boot code */
	uint jump2:8;
	char banner[8];		/*  3 OEM name & version */
	uint secsize0:8;	/* 11 Bytes per sector hopefully 512 */
	uint secsize1:8;
	uint clsize:8;		/* 13 Cluster size in sectors */
	uint nrsvsect:16;	/* 14 Number of reserved (boot) sectors */
	uint nfat:8;		/* 16 Number of FAT tables hopefully 2 */
	uint dirents0:8;	/* 17 Number of directory slots */
	uint dirents1:8;
	uint psect0:8;		/* 19 Total sectors on disk */
	uint psect1:8;
	uint descr:8;		/* 21 Media descriptor=first byte of FAT */
	uint fatlen:16;		/* 22 Sectors in FAT */
	uint nsect:16;		/* 24 Sectors/track */
	uint nheads:16;		/* 26 Heads */
	uint nhs:32;		/* 28 number of hidden sectors */
	uint bigsect:32;	/* 32 big total sectors */
	union {
		struct fat32 fat32;
		struct oldboot oldboot;
	} u;
};

/*
 * Parameters for block cache
 */
#define NCACHE (320)
extern uint clsize;
#define CLSIZE (bootb.clsize)
#define BLOCKSIZE (clsize)

/*
 * Mapping between cluster numbers and underlying byte offsets
 */
#define BOFF(clnum) (((clnum) - 2) * CLSIZE + data0)

/*
 * Node handling routines
 */
extern void rw_init(void);
extern struct node *rootdir, *procroot;
extern struct node *do_lookup(struct node *, char *);
extern void ref_node(struct node *),
	deref_node(struct node *);

/*
 * Cluster handling routines
 */
extern void clust_init(void);
extern struct clust *alloc_clust(struct directory *);
extern void free_clust(struct clust *);
extern void fat_sync(void);
extern int clust_setlen(struct clust *, ulong),
	clust_prealloc(struct clust *, ulong);
extern claddr_t get_clust(struct clust *, uint);

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
	dos_wstat(struct msg *, struct file *),
	dos_fid(struct msg *, struct file *),
	dos_prealloc(struct msg *, struct file *, ulong);
extern void timestamp(struct directory *, time_t),
	dir_timestamp(struct node *, time_t);
extern int dir_set_type(struct file *, char *);
extern void dir_readonly(struct file *f, int);
extern ulong inum(struct node *);
extern void do_unhash(ulong);
extern int assemble_vfat_name(char *name, struct directory *d,
	intfun nextd, void *statep);
extern uchar short_checksum(char *f1, char *f2);
extern void pack_name(struct directory *d, char *buf);

/*
 * Global data
 */
extern int blkdev;
extern struct boot bootb;
extern uint dirents;
extern struct node *rootdir;
extern ulong data0;
extern claddr_t root_cluster;
extern char *namer_name, *blk_name;

#endif /* _DOS_H */
