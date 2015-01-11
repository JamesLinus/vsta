/*
 * cdfs.h - CDFS definitions.
 */

#ifndef	__CDFS_H__
#define	__CDFS_H__

#include <time.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef	__VSTA__
typedef	char *caddr_t;
#include <unistd.h>
#endif

#define	__P(x)		()
#include "iso.h"
#include "highsier.h"

/*
 * ISO 9660 date formts.
 */
struct	iso_date7 {
	char	year;				/* years since 1900 */
	char	month;				/* month of year (1 - 12) */
	char	day;				/* day of month (1 - 31) */
	char	hour;				/* hour of day (0 - 23) */
	char	minute;				/* minute of hour (0 - 59) */
	char	second;				/* second of minute (0 - 59) */
	char	tz;				/* timezone offset */
};

union	iso_date {
	struct	iso_date7 date7;
};

/*
 * iso_directory_record flags.
 */
#define	ISO_HIDDEN	1			/* 1 = not visible */
#define	ISO_DIRECTORY	2			/* 1 = is a directory */
#define	ISO_ASSOC_FILE	4			/* 1 = associated file */
#define	ISO_RECORD_FMT	8			/* 1 = structured file */
#define	ISO_PROTECTED	0x10			/* 1 = see owner/perm's */
#define	ISO_MULT_EXTENT	0x80			/* 1 = !file's final extent */

#define	ISO_MAX_LEN_DR	255			/* max. directory record size */

/*
 * Per-CDFS structure.
 */
struct	cdfs {
	struct	iso_directory_record root_dir;	/* root directory record */
	int	lbsize;				/* logical block size */
	uint	flags;				/* filesystem flags */
	int	susp_lenskp;			/* SUSP LEN_SKP */
};

#define	CDFS_HIGH_SIERRA	1		/* High Sierra format */
#define	CDFS_RRIP		2		/* Rock Ridge format */

/*
 * POSIX file attributes.
 */
struct	cdfs_posix_attrs {
	mode_t	mode;				/* file mode */
	uint	nlink;				/* number of links */
	ulong	uid;				/* user ID */
	ulong	gid;				/* group ID */
};

/*
 * Per-open file structure.
 */
struct	cdfs_file {
	uint	flags;				/* misc. flags */
	ulong	position;			/* Byte position in file */
	struct	cdfs *cdfs;			/* incore superblock */
	struct	iso_directory_record node;	/* file's directory node */
	struct	iso_extended_attributes extnd_attrs;
						/* extended file attributes */
	struct	cdfs_posix_attrs posix_attrs;	/* POSIX file attributes */
	char	symname[MAXPATH];		/* symbolic link name */
};

#define	CDFS_FILE_COPY		1		/* MDUP'ed file copy */
#define	CDFS_EXTND_ATTRS	2		/* extended attr's valid */
#define	CDFS_POSIX_ATTRS	4		/* POSIX file attr's valid */
#define	CDFS_ALL_ATTRS		(CDFS_EXTND_ATTRS | CDFS_POSIX_ATTRS)

/*
 * Internal error codes.
 */
enum	{
	CDFS_SUCCESS,
	CDFS_EPERM,
	CDFS_ENOENT,
	CDFS_EIO,
	CDFS_ENOMEM,
	CDFS_ENOTDIR,
	CDFS_EINVAL,
	CDFS_ROFS
};

/*
 * Error flags.
 */
#define	CDFS_PRINT_SYSERR	1		/* system error messages */

/*
 * Debug support.
 */
#define	CDFS_DBG_FCN_ENTRY	1		/* function entry messages */
#define	CDFS_DBG_FCN_EXIT	2		/* function exit messages */
#define	CDFS_DBG_MSG		4		/* generic messages */

#define	CDFS_DBG_ALL		(CDFS_DBG_FCN_ENTRY |			\
				 CDFS_DBG_FCN_EXIT | CDFS_DBG_MSG)

#define	CDFS_DEBUG(_when, _myname, _msg, _arg)				\
		cdfs_debug(_when, _myname, _msg, _arg)

#define	CDFS_DEBUG_FCN_ENTRY(_myname)					\
		cdfs_debug(CDFS_DBG_FCN_ENTRY, _myname, "...")

#define	CDFS_DEBUG_FCN_EXIT(_myname, _status)				\
		cdfs_debug(CDFS_DBG_FCN_EXIT, _myname,			\
		          "exitting with status %d", _status)

extern	uint cdfs_debug_flags;

/*
 * Function prototypes.
 */
#ifdef	__STDC__
long	cdfs_mount(struct cdfs *cdfs);
void	cdfs_unmount(struct cdfs *cdfs);
long	cdfs_open(struct cdfs_file *file, char *name, int flags);
long	cdfs_lookup_name(struct cdfs_file *file, char *name, int update);
long	cdfs_read(struct cdfs_file *file, char *buffer, long *length);
long	cdfs_read_dir(struct cdfs_file *file, char *buffer, long *length);
long	cdfs_get_extnd_attrs(struct cdfs_file *file,
	                     struct iso_extended_attributes *attrs);
long	cdfs_bmap(struct cdfs_file *file, long offset);
int	cdfs_cvt_date(union iso_date *date, int fmt, int hs, int base_year,
	              time_t *time);
void	*cdfs_getblk(struct cdfs *cdfs, off_t start, int nblocks, void **data);
void	cdfs_relblk(struct cdfs *cdfs, void *cookie);
void	cdfs_error(uint flags, char *myname, char *fmt, ...);
void	cdfs_debug(uint when, char *myname, char *fmt, ...);
void	*cdfs_alloc_mem(size_t size, void *ptr, unsigned int align);
void	cdfs_free_mem(void *ptr, unsigned int align);
void	cdfs_init_file(struct cdfs *cdfs, int perm, struct cdfs_file *file);
int	cdfs_cksusp(struct iso_directory_record *dp, int *skip);
void	cdfs_get_altname(struct iso_directory_record *dp,
	                 char **name, int *name_len);
long	cdfs_get_posix_attrs(struct iso_directory_record *dp,
	                     struct cdfs_posix_attrs *attrs);
#endif

/*
 * What's needed from the buffer cache functions.
 */
void	*bget(), *bdata(), bfree(), bcache_inval();

#endif	/*__CDFS_H__*/ 
