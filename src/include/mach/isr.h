#ifndef _MACHISR_H
#define _MACHISR_H
/*
 * isr.h
 *	Constants relating to the ISA interrupt architecture
 */
#define MAX_IRQ 16	/* Max different IRQ levels */
#define SLAVE_IRQ 8	/* 0..7 are master, 8..15 are slave */
#define MASTER_SLAVE 2	/* IRQ where slave hooks to master */

#endif /* _MACHISR_H */
