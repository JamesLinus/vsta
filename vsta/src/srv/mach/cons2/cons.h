#ifndef _SCREEN_H
#define _SCREEN_H
/*
 * screen.h
 *	Common definitions for PC/AT console display
 */
#include <sys/types.h>

/*
 * An open file
 */
struct file {
	uint f_gen;	/* Generation of access */
	uint f_flags;	/* User access bits */
};

#define NORMAL 7		/* Attribute for normal characters */
#define BLANK (0x07200720)	/* Normal attribute, blank char */
				/*  ... two of them in a longword */

#define ROWS 25		/* Screen dimensions */
#define COLS 80
#define CELLSZ 2	/* Bytes per character cell */

/*
 * Top of respective adaptors
 */
#define CONS_LOW MGAIDX

#define MGATOP 0xB0000
#define MGAIDX 0x3B4
#define MGADAT 0x3B5
#define CGATOP 0xB8000
#define CGAIDX 0x3D4
#define CGADAT 0x3D5

#define CONS_HIGH CGADAT

/*
 * Which one we're using
 */
#ifdef CGA
#define DISPLAY CGATOP
#define IDX CGAIDX
#define DAT CGADAT
#else
#define DISPLAY MGATOP
#define IDX MGAIDX
#define DAT MGADAT
#endif

#endif /* _SCREEN_H */
