#ifndef _ICU_H
#define _ICU_H
/*
 * icu.h
 *	i386/ISA Interrupt Control Unit definitions
 *
 * The i386 has a pair of them, chained together.
 */

/* Base addresses of the two units */
#define ICU0 (0x20)
#define ICU1 (0xA0)

/* Clear an interrupt and allow new ones to arrive */
#define EOI() {outportb(ICU0, 0x20); outportb(ICU1, 0x20);}

/* Set interrupt mask */
#define SETMASK(mask) {outportb(ICU0+1, mask & 0xFF); \
	outportb(ICU1+1, (mask >> 8) & 0xFF); }

#endif /* _ICU_H */
