#ifndef _MACHPIT_H
#define _MACHPIT_H
/*
 * pit.h
 *	Constants relating to the 8254 (or 8253) programmable interval timers
 */
 
/*
 * Port address of the control port and timer channels
 */
#define PIT_CTRL 0x43
#define PIT_CH0 0x40
#define PIT_CH1 0x41
#define PIT_CH2 0x42

/*
 * Command to set square wave clock mode
 */
#define CMD_SQR_WAVE 0x36

/*
 *The internal tick rate in ticks per second
 */
#define PIT_TICK 1193180

#endif /* _MACHPIT_H */
