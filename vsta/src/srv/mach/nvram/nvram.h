#ifndef __SRV_NVRAM_H
#define __SRV_NVRAM_H


/*
 * Filename:	nvram.h
 * Author:	Dave Hudson <dave@humbug.demon.co.uk>
 * Started:	12th March 1994
 * Last Update: 10th May 1994
 * Implemented:	GNU GCC 1.42 (VSTa v1.3.1)
 *
 * Description: Function prototypes and definitions for the "nvram" server
 */


#include <sys/types.h>
#include <mach/nvram.h>

/*
 * Define the min and max number of bytes we support
 */
#define MIN_NVRAM_BYTES 64
#define MAX_NVRAM_BYTES 256


/*
 * Structure for per-connection operations
 */
struct file {
  int f_sender;			/* Sender of current operation */
  uint f_gen;			/* Generation of access */
  uint f_flags;			/* User access bits */
  uint f_count;			/* Number of bytes wanted for current op */
  uint f_pos;			/* Current file pointer */
  uint f_node;			/* Current "file" node */
};


/*
 * Fixed "inode" definition
 */
#define ROOT_INO 0		/* Root dir */
#define ALL_INO 1		/* All of the NVRAM */
#define RTC_INO 2		/* Real time clock */
#define FD0_INO 3		/* fd0 config details */
#define FD1_INO 4		/* fd1 config details */
#define WD0_INO 5		/* wd0 config details */
#define WD1_INO 6		/* wd1 config details */
#define MAX_INO 7		/* Max number of inodes */


/*
 * Structure for the pseudo file entries
 */
struct fentries {
  char fe_name[16];		/* Filename */
  int fe_size;			/* Size of the "file" */
};


/*
 * Function prototypes for rw.c
 */
extern void nvram_readwrite(struct msg *m, struct file *f);
extern void nvram_init(void);
extern int nvram_get_count_mode(void);
extern int nvram_get_clock_mode(void);
extern int nvram_get_daysave_mode(void);


/*
 * Function prototypes for dir.c
 */
void nvram_readdir(struct msg *m, struct file *f);
void nvram_open(struct msg *m, struct file *f);


/*
 * Function prototypes for stat.c
 */
extern void nvram_stat(struct msg *m, struct file *f);
extern void nvram_wstat(struct msg *m, struct file *f);

 
#endif /* __SRV_NVRAM_H */
