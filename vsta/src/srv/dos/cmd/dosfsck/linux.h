#ifndef LINUX_H
#define LINUX_H
/*
 * linux.h
 *	Wrapper around the Linux environment
 *
 * This lets us emulate a Linux-like environment when porting these tools
 * to non-Linux OS's.
 * Created by Andy Valencia, 12/2000
 */

/*
 * Variant for VSTa microkernel
 */
#if defined(VSTA)
#include "../../dos.h"
#include <sys/types.h>

#define llseek lseek		/* Map long offset type to standard */
typedef off_t loff_t;		/*  ...TBD: > 4Gig support */

typedef unsigned char __u8;	/* Some Linux type names */
typedef unsigned short __u16;
typedef unsigned long __u32;
typedef signed char __s8;

/* Directories per sector */
#define MSDOS_DPS (SECSZ / sizeof(struct directory))

/* Linux's name for sector size */
#define SECTOR_SIZE (SECSZ)

/* Room for an 8.3 filename */
#define MSDOS_NAME (8+3)

/* Names for attributes */
#define ATTR_DIR DA_DIR
#define ATTR_RO DA_READONLY
#define ATTR_HIDDEN DA_HIDDEN
#define ATTR_SYS DA_SYSTEM
#define ATTR_VOLUME DA_VOLUME

/* Deleted filename marker */
#define DELETED_FLAG DN_DEL

/* Names for dot and dotdot */
#define MSDOS_DOT (".")
#define MSDOS_DOTDOT ("..")

/* VSTa doesn't have any limit, so we just pick a value here */
#define PATH_MAX (1024)

/* TBD: handle endian for big endian targets */
#define CT_LE_L(v) (v)
#define CT_LE_W(v) (v)
#define CF_LE_L(v) (v)
#define CF_LE_W(v) (v)

/* Decode if a dirent is free */
#define IS_FREE(name) ((name[0] == 0) || (((__u8)name[0]) == DN_DEL))

/*
 * Default is Linux target
 */
#else

#include <linux/msdos_fs.h>
#define _LINUX_STAT_H		/* hack to avoid inclusion of <linux/stat.h> */

#if defined(OUR_LINUX_IO)
#include <sys/ioctl.h>
#include <linux/fd.h>
#endif

#endif

#endif /* LINUX_H */
