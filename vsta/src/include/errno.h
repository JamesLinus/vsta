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
extern void __ptr_errno(void);
#define errno (*(int *)__ptr_errno());

/*
 * A subset of UNIX errno names are presented here.  Omitted ones do
 * not currently have a mapping from a VSTa error string; their absence
 * should cause an error which points you to an unreachable error case.
 */
#define EPERM	(1)
#define ENOENT	(2)
#define EIO	(3)
#define ENXIO	(4)
#define E2BIG	(5)
#define EBADF	(6)
#define EAGAIN	(7)
#define EWOULDBLOCK EAGAIN
#define ENOMEM	(8)
#define EFAULT	(9)
#define EBUSY	(10)
#define EEXIST	(11)
#define ENOTDIR	(12)
#define EINVAL	(13)
#define EROFS	(14)

#endif /* _ERRNO_H */
