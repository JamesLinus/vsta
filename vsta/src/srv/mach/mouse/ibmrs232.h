/* 
 * ibmrs232.h
 *    Polling driver for the IBM serial mouse.
 *
 * Copyright (C) 1993 by G.T.Nicol, all rights reserved.
 *
 */

#ifndef __MOUSE_IBM_SERIAL_H__
#define __MOUSE_IBM_SERIAL_H__

#define IBM_SERIAL_BUFSIZ 512

typedef enum {
  RS_MICROSOFT    = 0,
  RS_MOUSE_SYS_3  = 1,
  RS_MOUSE_SYS_5  = 2,
  RS_MM           = 3,
  RS_LOGITECH     = 4
} ibm_model_t;

extern int  ibm_serial_initialise(int argc, char **argv);
extern void ibm_serial_poller_entry_point(void);
extern void ibm_serial_coordinates(ushort x, ushort y);
extern void ibm_serial_bounds(ushort x1, ushort y1, ushort x2, ushort y2);
extern void ibm_serial_update_period(ushort period);

#endif /* __MOUSE_IBM_SERIAL_H__ */
