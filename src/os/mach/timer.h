#ifndef _MACH_TIMER_H
#define _MACH_TIMER_H
/*
 * timer.h
 *	Inlined PC timer operations that allow us greater timer accuracy
 */
#include <mach/pit.h>
#include <mach/icu.h>
#include "../mach/locore.h"

/*
 * get_itime()
 *	Get the time in microseconds between what we run our system clock
 *	interrupt rate as and the real elapsed time
 *
 * Only call this with interrupts disabled!  BTW for code spotters I hold
 * my hands up now and admit this is basically lifted from Linux :-)
 *					-- Dave (15th February 1995)
 *
 * When we latch the data in from the timer we must have the interrupts
 * disabled so that we can look to see if a timer interrupt is pending.
 * If one is pending we add an additional HZ value's worth of ticks into
 * the result from here to compensate
 */
inline extern ulong
get_itime(void)
{
	extern ulong latch_ticks;
	ulong count;
	ulong offset = 0;

	/*
	 * Latch the interval timer count
	 */
	outportb(PIT_CTRL, CMD_LATCH);
	count = (ulong)inportb(PIT_CH0);
	count |= (ulong)inportb(PIT_CH0) << 8;

	/*
	 * Unhandled interrupts will only really occur if we're within
	 * 1% of the rollover point - only look if this is the case.
	 * I guess this should be latch_ticks instead of PIT_LATCH, but
	 * this is only an approximation and is *much* faster!
	 */
	if (count > (PIT_LATCH - (PIT_LATCH / 100))) {
		/*
		 * Look at the interrupt pending flag for the timer
		 */
		outportb(ICU0, 0x0a);
		if (inportb(ICU0) & 0x01) {
			offset = (1000000L / HZ);
		}
	}
	count = ((latch_ticks - 1) - count) * (1000000L / HZ);
	count = (count + (latch_ticks / 2)) / latch_ticks;
	
	return(count + offset);
}

#endif /* _MACH_TIMER_H */
