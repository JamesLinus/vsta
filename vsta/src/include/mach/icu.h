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

/*
 * End Of Interrupt flag
 */
#define EOI_FLAG (0x20)

#endif /* _ICU_H */
