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

typedef enum { RS_MICROSOFT, RS_MOUSE_SYS_3, RS_MOUSE_SYS_5,
	RS_MM, RS_LOGITECH
} ibm_model_t;

extern int ibm_serial_initialise(int, char **);
extern void ibm_serial_poller_entry_point(void);
extern void ibm_serial_update_period(ushort);

#endif /* __MOUSE_IBM_SERIAL_H__ */
