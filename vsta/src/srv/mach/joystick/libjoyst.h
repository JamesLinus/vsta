#ifndef _LIBJOYSTICK_H
#define _LIBJOYSTICK_H


/*
 * Filename:	libjoystick.h
 * Author:	Dave Hudson <dave@humbug.demon.co.uk>
 * Started:	5th January 1994
 * Last Update: 21st March 1994
 * Implemented:	GNU GCC version 2.5.7
 *
 * Description:	Declarations for PC/AT joystick/games port
 */


#include <sys/types.h>

/*
 * Bit masks for the data port
 */
#ifndef _JOYSTICK_H
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
#endif /* _JOYSTICK_H */

/*
 * Button status declarations
 */
#define JS_BTN_UP 1
#define JS_BTN_DOWN 0

/*
 * What we say to signify a channel was not available
 */
#ifndef _JOYSTICK_H
#define JS_NONE 0xffff
#define JS_TIMEOUT 0xfffe
#endif /* _JOYSTICK_H */

/*
 * Connection/disconnection management functions 
 */
extern int joystick_connect(void);
extern int joystick_disconnect(void);

/*
 * Low level I/O functions
 */
extern int joystick_read(ushort *ch_a, ushort *ch_b, ushort *ch_c,
			 ushort *ch_d, uchar *btns);

#endif /* _LIBJOYSTICK_H */
