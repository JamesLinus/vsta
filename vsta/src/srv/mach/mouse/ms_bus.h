/* 
 * ms_bus.h
 *    Interrupt driven driver for the Microsoft bus mouse.
 *
 * Copyright (C) 1993 by G.T.Nicol, all rights reserved.
 *
 */

#ifndef __MOUSE_MS_BUS_H__
#define __MOUSE_MS_BUS_H__

#define MS_BUS_IRQ                      0x5
#define MS_BUS_DATA_PORT                0x23d
#define MS_BUS_ID_PORT                  0x23e
#define MS_BUS_CONTROL_PORT             0x23c
#define MS_BUS_CONFIG_PORT              0x23f

#define MICROSOFT_LOW_PORT         MS_BUS_CONTROL_PORT
#define MICROSOFT_HIGH_PORT        MS_BUS_CONFIG_PORT

#define MS_BUS_ENABLE_INTERRUPTS        0x11
#define MS_BUS_DISABLE_INTERRUPTS       0x10

#define MS_BUS_READ_BUTTONS             0x00
#define MS_BUS_READ_X                   0x01
#define MS_BUS_READ_Y                   0x02

#define MS_BUS_START                    0x80
#define MS_BUS_COMMAND_MODE             0x07

/*
 * A couple of useful macros
 */
#define MS_BUS_INT_OFF()                                      \
{                                                             \
   outportb(MS_BUS_CONTROL_PORT, MS_BUS_COMMAND_MODE);        \
   outportb(MS_BUS_DATA_PORT,    MS_BUS_DISABLE_INTERRUPTS);  \
}

#define MS_BUS_INT_ON()                                       \
{                                                             \
   outportb(MS_BUS_CONTROL_PORT, MS_BUS_COMMAND_MODE);        \
   outportb(MS_BUS_DATA_PORT,    MS_BUS_ENABLE_INTERRUPTS);   \
}

extern int  ms_bus_initialise(int argc, char **argv);
extern void ms_bus_interrupt(void);
extern void ms_bus_coordinates(ushort x, ushort y);
extern void ms_bus_bounds(ushort x1, ushort y1, ushort x2, ushort y2);
 
#endif /* __MOUSE_MS_BUS_H__ */

 







