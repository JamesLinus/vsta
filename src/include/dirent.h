#ifndef _DIRENT_H
#define _DIRENT_H
/*
 * dirent.h
 *	Stuff for the readdir package of routines
 */
#include <stdio.h>

#define _NAMLEN (254)		/* Max length of entry name */

/*
 * One of these returned per dir entry read
 */
struct dirent {
	unsigned char
		d_namlen;	/* Length of entry name */
	char d_name[_NAMLEN];	/* 1 or more bytes of actual name */
};

/*
 * One of these per currently open directory
 */
typedef struct {
	int d_state;		/* State machine (see readdir()) */
	long d_elems;		/* Count total elems read */
	FILE *d_fp;		/* Buffered view into dir file */
	char *d_path;		/* Absolute path we're examining */
	struct dirent		/* Current entry read into here */
		d_de;
} DIR;

extern DIR *opendir(const char *);
extern int closedir(DIR *);
extern struct dirent *readdir(DIR *);
extern void seekdir(DIR *, long), rewinddir(DIR *);
extern long telldir(DIR *);

#endif /* _DIRENT_H */
