#ifndef _FS_H
#define _FS_H
/*
 * Custom message types for a generic filesystem
 */
#include <sys/msg.h>

#define FS_OPEN 100
#define FS_READ 101
#define FS_SEEK 102
#define FS_WRITE 103
#define FS_REMOVE 104
#define FS_STAT 105
#define FS_WSTAT 106

/*
 * The following are recommended, but not required.  They are
 * currently only needed to support swap operations.
 */
#define FS_ABSREAD 150
#define FS_ABSWRITE 151

/*
 * How to ask if an operation is a bulk read/write
 */
#define FS_RW(op) (((op) == FS_READ) || ((op) == FS_WRITE) || \
	((op) == FS_ABSREAD) || ((op) == FS_ABSWRITE))

/*
 * Access modes
 */
#define ACC_READ 0x4
#define ACC_WRITE 0x2
#define ACC_EXEC 0x1
/* #define ACC_TRUNC 0x8	XXX does ACC_CREATE cover this? */
#define ACC_CREATE 0x10
#define ACC_DIR 0x20
#define ACC_CHMOD 0x40

/*
 * Standard error strings
 */
#define EPERM "perm"
#define ESRCH "no entry"
#define EINVAL "invalid"
#define E2BIG "too big"
#define ENOMEM "no mem"
#define EBUSY "busy"
#define ENOSPC "no space"
#define ENOTDIR "not dir"
#define EIO "io err"
#define ENXIO "no io"
#define EFAULT "fault"
#define EINTR "intr"
#define EMATH "math"
#define EILL "ill instr"
#define EKILL "kill"

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
 */

#endif /* _FS_H */
