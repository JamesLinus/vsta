#ifndef _FS_H
#define _FS_H
/*
 * Custom message types for a generic filesystem
 */
#include <sys/msg.h>

/*
 * FS operations which should basically be offered by all
 * servers.
 */
#define FS_OPEN 100
#define FS_READ 101
#define FS_SEEK 102
#define FS_WRITE 103
#define FS_REMOVE 104
#define FS_STAT 105
#define FS_WSTAT 106

/*
 * The following are recommended, but not required
 */
#define FS_ABSREAD 150		/* Mostly for swap/paging operations */
#define FS_ABSWRITE 151
#define FS_FID 152		/* For caching of mapped files */
#define FS_RENAME 153		/* Renaming of files */

/*
 * Used for tunneling an error code within a struct msg
 */
#define FS_ERR (154)

/*
 * How to ask if an operation is a bulk read/write
 */
#define FS_RW(op) (((op) == FS_READ) || ((op) == FS_WRITE) || \
	((op) == FS_ABSREAD) || ((op) == FS_ABSWRITE))

/*
 * Access modes
 */
#define ACC_EXEC	0x1
#define ACC_WRITE	0x2
#define ACC_READ	0x4
#define ACC_SYM		0x8
#define ACC_CREATE	0x10
#define ACC_DIR		0x20
#define ACC_CHMOD	0x40

/*
 * Standard error strings
 */
#define EPERM "perm"
#define ENOENT "no file"
#define ESRCH "no entry"
#define EINTR "intr"
#define EIO "io err"
#define ENXIO "no io"
#define E2BIG "too big"
#define ENOEXEC "exec fmt"
#define EBADF "bad file"
#define ECHILD "no child"
#define EAGAIN "again"
#define EWOULDBLOCK EAGAIN
#define ENOMEM "no mem"
#define EACCES "access"
#define EFAULT "fault"
#define ENOTBLK "not blk dev"
#define EBUSY "busy"
#define EEXIST "exists"
#define ENODEV "not dev"
#define ENOTDIR "not dir"
#define EISDIR "is dir"
#define EINVAL "invalid"
#define ENFILE "file tab ovfl"
#define EMFILE "too many files"
#define ENOTTY "not tty"
#define ETXTBSY "txt file busy"
#define EFBIG "file too large"
#define ENOSPC "no space"
#define ESPIPE "ill seek"
#define EROFS "RO fs"
#define EMLINK "too many links"
#define EPIPE "broken pipe"
#define EDOM "math domain"
#define ERANGE "math range"
#define EMATH "math"
#define EILL "ill instr"
#define EKILL "kill"
#define EXDEV "cross dev"
#define EISDIR "is dir"
#define EBALIGN "blk align"
#define ESYMLINK "symlink"
#define ELOOP "symlink loop"
#define ENOTSUP "!supported"
#define EOPNOTSUPP ENOTSUP

/*
 * A stat of an entry returns a set of strings with newlines
 * between them.  Each string has the format "key=value".  The
 * following keys and their meaning are required by any file
 * server.
 *
 * perm		List of IDs on protection label
 * acc		List of bits (see ACC_* above) corresponding to perm
 * size		# bytes in file
 * owner	ID of creator of file
 * inode	Unique index number for file
 *
 * Not required, but with a common interpretation:
 * dev		Block device's port number
 * ctime	Create/Modify/Access times as 14-digit decimal number
 * mtime
 * atime
 * gen		Access generation--for protecting TTY's between sessions
 * block	Bytes in a block used on device
 * blocks	Actual blocks used by entry
 */

/*
 * Function prototypes for the VSTa stat mechanism
 */
extern char *rstat(port_t fd, char *field);
extern int wstat(port_t fd, char *field);

/*
 * Additional function used to help servers process the general stat fields
 */
extern int do_wstat(struct msg *m, struct prot *prot,
		    int acc, char **fieldp, char **valp);

#endif /* _FS_H */
