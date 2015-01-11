#ifndef _JOYSTICK_H
#define _JOYSTICK_H


/*
 * Filename:	joystick.h
 * Author:	Dave Hudson <dave@humbug.demon.co.uk>
 * Started:	5th January 1994
 * Last Update: 21st March 1994
 * Implemented:	GNU GCC version 2.5.7
 *
 * Description: Server specific declarations for a PC/AT joystick/games port.
 *		This file is not intended for inclusion into user programs.
 */


#include <sys/types.h>


/*
 * I/O port used
 */
#define JS_DATA 0x201


/*
 * Bit masks for the data port
 */
#define JS_CH_A 0x01
#define JS_CH_B 0x02
#define JS_CH_C 0x04
#define JS_CH_D 0x08
#define JS_CH_MASK (JS_CH_A | JS_CH_B | JS_CH_C | JS_CH_D)

#define JS_BTN_A 0x10
#define JS_BTN_B 0x20
#define JS_BTN_C 0x40
#define JS_BTN_D 0x80
#define JS_BTN_MASK (JS_BTN_A | JS_BTN_B | JS_BTN_C | JS_BTN_D)


/*
 * Number of joystick channels supported
 */
#define JS_MAX 4


/*
 * What we say to signify a channel was not available
 */
#define JS_NONE 0xffff
#define JS_TIMEOUT 0xfffe


/*
 * Structure for per-connection operations
 */
struct file {
  int f_sender;			/* Sender of current operation */
  uint f_gen;			/* Generation of access */
  uint f_flags;			/* User access bits */
  uint f_count;			/* # bytes wanted for current op */
};


/*
 * Definitions in rw.c
 */
extern void js_read(struct msg *, struct file*);
extern void js_init(void);


/*
 * Definitions in stat.c
 */
extern void js_stat(struct msg *, struct file *);
extern void js_wstat(struct msg *, struct file *);


#endif /* _JOYSTICK_H */
