/* 
 * ibmrs232.h
 *    Polling driver for the IBM serial mouse.
 *
 * Copyright (C) 1993 by G.T.Nicol, all rights reserved.
 *
 */

#ifndef __MOUSE_IBM_SERIAL_H__
#define __MOUSE_IBM_SERIAL_H__

typedef enum {
  RS_UNKNOWN      = 0,
  RS_MICROSOFT    = 1,
  RS_MOUSE_SYS_3  = 2,
  RS_MOUSE_SYS_5  = 3,
  RS_MM           = 4,
  RS_LOGITECH     = 5,
} ibm_model_t;

/*
 * I/O address of registers
 */
#define IBM_RS_LINEREG  (ibm_serial_iobase+0xB)  /* Format of RS-232 data  */
#define IBM_RS_LOWBAUD  (ibm_serial_iobase+0x8)  /* low parts of baud rate */
#define IBM_RS_HIBAUD   (ibm_serial_iobase+0x9)  /* low parts of baud rate */
#define IBM_RS_LINESTAT (ibm_serial_iobase+0xD)  /* Status of line         */
#define IBM_RS_DATA     (ibm_serial_iobase+0x8)  /* Read/write data here   */
#define IBM_RS_INTREG   (ibm_serial_iobase+0x9)  /* Interrupt control      */
#define IBM_RS_INTID    (ibm_serial_iobase+0xA)  /* Why "interrupted"      */
#define IBM_RS_MODEM    (ibm_serial_iobase+0xC)  /* Modem lines            */

#define IBM_RS_HIGH_PORT IBM_RS_MODEM
#define IBM_RS_LOW_PORT  IBM_RS_LOWBAUD

extern int  ibm_serial_initialise(int argc, char **argv);
extern void ibm_serial_poller_entry_point(void);
extern void ibm_serial_coordinates(ushort x, ushort y);
extern void ibm_serial_bounds(ushort x1, ushort y1, ushort x2, ushort y2);
extern void ibm_serial_update_period(ushort period);

#endif /* __MOUSE_IBM_SERIAL_H__ */









