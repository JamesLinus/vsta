/* 
 * logitech_bus.h
 *	Interrupt driven driver for the Logitech bus mouse.
 *
 * Copyright (C) 1993 by G.T.Nicol, all rights reserved.
 */
#ifndef __MOUSE_LOGITECH_BUS_H__
#define __MOUSE_LOGITECH_BUS_H__

#define LOGITECH_BUS_IRQ 0x5
#define LOGITECH_BUS_DATA_PORT 0x23c
#define LOGITECH_BUS_ID_PORT 0x23d
#define LOGITECH_BUS_CONTROL_PORT 0x23e
#define LOGITECH_BUS_CONFIG_PORT 0x23f

#define LOGITECH_LOW_PORT LOGITECH_BUS_DATA_PORT
#define LOGITECH_HIGH_PORT LOGITECH_BUS_CONFIG_PORT

#define LOGITECH_BUS_ENABLE_INTERRUPTS 0x00
#define LOGITECH_BUS_DISABLE_INTERRUPTS 0x10

#define LOGITECH_BUS_READ_X_HIGH 0xa0
#define LOGITECH_BUS_READ_X_LOW 0x80
#define LOGITECH_BUS_READ_Y_HIGH 0xe0
#define LOGITECH_BUS_READ_Y_LOW 0xc0

#define LOGITECH_BUS_CONFIG_BYTE 0x91
#define LOGITECH_BUS_DEFAULT_MODE 0x90
#define LOGITECH_BUS_ID_BYTE 0xa5

#define LOGITECH_BUS_INT_OFF() \
    outportb(LOGITECH_BUS_CONTROL_PORT, LOGITECH_BUS_DISABLE_INTERRUPTS)

#define LOGITECH_BUS_INT_ON() \
    outportb(LOGITECH_BUS_CONTROL_PORT, LOGITECH_BUS_ENABLE_INTERRUPTS)

extern int logitech_bus_initialise(int, char **);
extern void logitech_bus_interrupt(void);
 
#endif /* __MOUSE_LOGITECH_BUS_H__ */
