#ifndef _ERRNO_H
#define _ERRNO_H
/*
 * errno.h
 *	Compatibility wrapper mapping VSTa errors into old-style values
 */

/*
 * The sick part is that errno must be a global lvalue; it can be
 * written.  Here, we call a function to ask it the address.  Even
 * with this, errno's treatment is painful, since we can't tell if
 * its value will be an rval or lval in the call.
 */
extern int *__ptr_errno(void);
#define errno (*__ptr_errno())

#define	EPERM (1)	/* Not sufficient permissions */
#define	ENOENT (2)	/* No such file or directory */
#define	ESRCH (ENOENT)	/* No such process */
#define	EINTR (4)	/* Interrupted system call */
#define	EIO (5)		/* I/O error */
#define	ENXIO (6)	/* No such device or address */
#define	E2BIG (7)	/* Arg list too long */
#define	ENOEXEC (8)	/* Exec format error */
#define	EBADF (9)	/* Bad file number */
#define	ECHILD (10)	/* No children */
#define	EAGAIN (11)	/* No more processes */
#define EWOULDBLOCK (EAGAIN)
#define	ENOMEM (12)	/* Not enough core */
#define	EACCES (13)	/* Permission denied */
#define	EFAULT (14)	/* Bad address */
#define	ENOTBLK (15)	/* Block device required */
#define	EBUSY (16)	/* Mount device busy */
#define	EEXIST (17)	/* File exists */
#define	EXDEV (18)	/* Cross-device link */
#define	ENODEV (19)	/* No such device */
#define	ENOTDIR (20)	/* Not a directory */
#define	EISDIR (21)	/* Is a directory */
#define	EINVAL (22)	/* Invalid argument */
#define	ENFILE (23)	/* File table overflow */
#define	EMFILE (24)	/* Too many open files */
#define	ENOTTY (25)	/* Not a typewriter */
#define	ETXTBSY (26)	/* Text file busy */
#define	EFBIG (27)	/* File too large */
#define	ENOSPC (28)	/* No space left on device */
#define	ESPIPE (29)	/* Illegal seek */
#define	EROFS (30)	/* Read only file system */
#define	EMLINK (31)	/* Too many links */
#define	EPIPE (32)	/* Broken pipe */
#define	EDOM (33)	/* Math arg out of domain of func */
#define	ERANGE (34)	/* Math result not representable */
#define	ENOMSG (35)	/* No message of desired type */
#define	EIDRM (36)	/* Identifier removed */
#define	ECHRNG (37)	/* Channel number out of range */
#define	EL2NSYNC (38)	/* Level 2 not synchronized */
#define	EL3HLT (39)	/* Level 3 halted */
#define	EL3RST (40)	/* Level 3 reset */
#define	ELNRNG (41)	/* Link number out of range */
#define	EUNATCH (42)	/* Protocol driver not attached */
#define	ENOCSI (43)	/* No CSI structure available */
#define	EL2HLT (44)	/* Level 2 halted */
#define	EDEADLK (45)	/* Deadlock condition */
#define	ENOLCK (46)	/* No record locks available */
#define EBADE (50)	/* Invalid exchange */
#define EBADR (51)	/* Invalid request descriptor */
#define EXFULL (52)	/* Exchange full */
#define ENOANO (53)	/* No anode */
#define EBADRQC (54)	/* Invalid request code */
#define EBADSLT (55)	/* Invalid slot */
#define EDEADLOCK (56)	/* File locking deadlock error */
#define EBFONT (57)	/* Bad font file fmt */
#define ENOSTR (60)	/* Device not a stream */
#define ENODATA (61)	/* No data (for no delay io) */
#define ETIME (62)	/* Timer expired */
#define ENOSR (63)	/* Out of streams resources */
#define ENONET (64)	/* Machine is not on the network */
#define ENOPKG (65)	/* Package not installed */
#define EREMOTE (66)	/* The object is remote */
#define ENOLINK (67)	/* The link has been severed */
#define EADV (68)	/* Advertise error */
#define ESRMNT (69)	/* Srmount error */
#define	ECOMM (70)	/* Communication error on send */
#define EPROTO (71)	/* Protocol error */
#define	EMULTIHOP (74)	/* Multihop attempted */
#define	ELBIN (75)	/* Inode is remote (not really error) */
#define	EDOTDOT (76)	/* Cross mount point (not really error) */
#define EBADMSG (77)	/* Trying to read unreadable message */
#define ENOTUNIQ (80)	/* Given log. name not unique */
#define EBADFD (81)	/* f.d. invalid for this operation */
#define EREMCHG (82)	/* Remote address changed */
#define ELIBACC (83)	/* Can't access a needed shared lib */
#define ELIBBAD (84)	/* Accessing a corrupted shared lib */
#define ELIBSCN (85)	/* .lib section in a.out corrupted */
#define ELIBMAX (86)	/* Attempting to link in too many libs */
#define ELIBEXEC (87)	/* Attempting to exec a shared library */
#define ENOSYS (88)	/* Function not implemented */

#define EMATH (100)	/* VSTa math error */
#define EILL (101)	/* VSTa illegal instruction */
#define EKILL (102)	/* VSTa kill */
#define EBALIGN (103)	/* Block alignment required */
#define ESYMLINK (104)	/* Symbolic link */
#define ELOOP (105)	/* Symbolic link loop */

/*
 * For mapping these values into VSTa error strings
 */
extern char *__map_errno(int);

#endif /* _ERRNO_H */
