/*
 * libmouse.h
 *    Function prototypes for the mouse server API.
 *
 * Copyright (C) 1993 G.T.Nicol, all rights reserved.
 *
 * All the functions here return -1 on error, or 0 on success.
 */

#ifndef __VSTA_LIBMOUSE_H__
#define __VSTA_LIBMOUSE_H__

/*
 * Masks for the mouse buttons
 */
#define MOUSE_LEFT_BUTTON    (1 << 1)
#define MOUSE_MIDDLE_BUTTON  (1 << 2)
#define MOUSE_RIGHT_BUTTON   (1 << 3)

/*
 * Open/close the mouse device
 */
extern int mouse_connect(void);
extern int mouse_disconnect(void);

/*
 * Low level I/O routines
 */
extern int mouse_read(uchar *buttons, ushort *x, ushort *y, int *freq,
		      ushort *bx1, ushort *by1, ushort *bx2, ushort *by2);
extern int mouse_write(uchar *op, uchar *buttons, ushort *x, ushort *y, 
		       int *freq, ushort *bx1, ushort *by1, ushort *bx2, 
		       ushort *by2);

/*
 * Higher level routines
 */
extern int mouse_get_buttons(uchar *buttons);
extern int mouse_get_coordinates(ushort *x, ushort *y);
extern int mouse_get_bounds(ushort *x1, ushort *y1, ushort *x2, ushort *y2);
extern int mouse_get_update_freq(int *freq);

extern int mouse_set_coordinates(ushort x, ushort y);
extern int mouse_set_bounds(ushort x1, ushort y1, ushort x2, ushort y2);
extern int mouse_set_update_freq(int freq);

#endif /* __VSTA_LIBMOUSE_H__ */
