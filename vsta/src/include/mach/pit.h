#ifndef _MACHPIT_H
#define _MACHPIT_H
/*
 * pit.h
 *	Constants relating to the 8254 (or 8253) programmable interval timers
 */
#include <mach/param.h>
 
/*
 * Port address of the control port and timer channels
 */
#define PIT_CTRL 0x43
#define PIT_CH0 0x40
#define PIT_CH1 0x41
#define PIT_CH2 0x42

/*
 * Command to set rate generator mode
 */
#define CMD_SQR_WAVE 0x34

/*
 * Command to latch the timer registers
 */
#define CMD_LATCH 0x00

/*
 * The internal tick rate in ticks per second
 */
#define PIT_TICK 1193180

/*
 * The latch count value for the current HZ setting
 */
#define PIT_LATCH ((PIT_TICK + (HZ / 2)) / HZ)

#endif /* _MACHPIT_H */
