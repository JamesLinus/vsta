/* 
 * nec_bus.h
 *    Combined polling/interrupt driven driver for the NEC bus mouse.
 *
 * Copyright (C) 1993 by G.T.Nicol, all rights reserved.
 *
 * This driver does not currently support high-resolution mode.
 */

#ifndef __MOUSE_NEC_BUS_H__
#define __MOUSE_NEC_BUS_H__

#define PC98_MOUSE_TIMER_PORT     0xbfdb
#define PC98_MOUSE_RPORT_A        0x7fd9
#define PC98_MOUSE_RPORT_B        0x7fdb
#define PC98_MOUSE_RPORT_C        0x7fdd
#define PC98_MOUSE_WPORT_C        0x7fdd
#define PC98_MOUSE_MODE_PORT      0x7fdf
#define PC98_MOUSE_LOW_PORT       PC98_MOUSE_RPORT_A
#define PC98_MOUSE_HIGH_PORT      PC98_MOUSE_MODE_PORT

extern int  pc98_bus_initialise(int argc, char **argv);
extern void pc98_bus_interrupt(void);
extern void pc98_bus_poller_entry_point(void);
extern void pc98_bus_coordinates(ushort x, ushort y);
extern void pc98_bus_bounds(ushort x1, ushort y1, ushort x2, ushort y2);
extern void pc98_bus_update_period(ushort period);

#endif /* __MOUSE_BUS_NEC_H__ */









