/*
 * libmouse.c
 *    A simple library for dealing with the mouse driver.
 *
 * Copyright (C) 1993 G.T.Nicol, all rights reserved.
 *
 * This library provides a simple API to the mouse driver. There are 2 
 * "core" routines, mouse_read(), and mouse_write(), and some auxillary 
 * functions for reading/writing only specific parts of the mouse data.
 * These use the core functions.
 *
 * One problem with this library is that each read/write corresponds to a
 * read or write to the mouse driver. Some of this overhead could be avoided
 * by forking of a thread to read in mouse data, say 10 times a second, or
 * less, and thereby maintaining a local copy of the data. The read functions
 * would then only read local data. This may be a good thing to implement 
 * later.
 */

#include <stdio.h>
#include <pio.h>
#include <fcntl.h>
#include <std.h>

#define MOUSE_NOOP              0
#define MOUSE_SET_COORDINATES   1
#define MOUSE_SET_BOUNDS        2
#define MOUSE_SET_UPDATE_FREQ   3

/*
 * This is the file descriptor we use for mouse I/O.
 */
static int __mouse_io_file = -1;

/*
 * We use this just in case some (l)user descides to open the mouse more
 * than once.
 */
static int __mouse_open = 0;

/*
 * mouse_connect()
 *    Open the mouse device for reading and writing.
 */
int 
mouse_connect(void)
{
   __mouse_io_file = open("/dev/mouse",O_RDWR);
   __mouse_open += 1;
   return(__mouse_io_file);
}

/*
 * mouse_disconnect()
 *    If the mouse device needs closing, close the file descriptor.
 */
int 
mouse_disconnect(void)
{
   if(__mouse_open > 0) {
      close(__mouse_io_file);
      __mouse_io_file = -1;
   } else {
      __mouse_open -= 1;
   }
   return(0);
}

/*
 * mouse_read()
 *    Low-level read function for the mouse device.
 */
int 
mouse_read(uchar *buttons, ushort *x, ushort *y, int *freq,
	   ushort *bx1, ushort *by1, ushort *bx2, ushort *by2)
{
   uchar id;
   static pio_buffer_t *pio_buff = NULL;
   int size,ret = 0;

   /*
    * Make sure the device is open.
    */
   if(__mouse_io_file == -1){
      fprintf(stderr,"Mouse device not open.\n");
      return(-1);
   }

   /*
    * Create out PIO buffer if needed. The size 1024 is actually
    * much too big.
    */
   if (pio_buff == NULL)  {
     pio_buff = pio_create_buffer(NULL, 1024);
   }

   /*
    * Read the data into the PIO buffer.
    */
   size = read(__mouse_io_file, pio_buff->buffer, 1024);
   if(size == -1) {
      fprintf(stderr,"Bad read on mouse device\n");
      return(-1);
   }

   /*
    * Reset the buffer pointer and type 
    */
   pio_reset_buffer(pio_buff);
   pio_buff->buffer_type = PIO_INPUT;
   pio_buff->buffer_end = size;

   /*
    * Read in the actual data.
    */
   ret += pio_u_char(pio_buff,  &id);
   ret += pio_u_char(pio_buff,  buttons);
   ret += pio_u_short(pio_buff, x);
   ret += pio_u_short(pio_buff, y);
   ret += pio_int(pio_buff,     freq);
   ret += pio_u_short(pio_buff, bx1);
   ret += pio_u_short(pio_buff, by1);
   ret += pio_u_short(pio_buff, bx2);
   ret += pio_u_short(pio_buff, by2);

   if(id != 'M')
      ret = -1;

   return(ret);
}

/*
 * mouse_write()
 *    Low-level write function for the mouse device.
 */
int 
mouse_write(uchar *op, uchar *buttons, ushort *x, ushort *y, int *freq,
	    ushort *bx1, ushort *by1, ushort *bx2, ushort *by2)
{
   uchar id = 'M';
   static pio_buffer_t *pio_buff = NULL;
   int ret = 0;

   /*
    * Make sure the file is open.
    */
   if(__mouse_io_file == -1){
      fprintf(stderr,"Mouse device not open.\n");
      return(-1);
   }

   /*
    * Create out PIO buffer if needed.
    */
   if(pio_buff == NULL) 
     pio_buff = pio_create_buffer(NULL,1024);

   /*
    * Reset the buffer
    */
   pio_buff->buffer_type = PIO_OUTPUT;
   pio_buff->buffer_pos  = 0;

   /*
    * Write out the request
    */
   ret += pio_u_char(pio_buff,  &id);
   ret += pio_u_char(pio_buff,  op);
   ret += pio_u_char(pio_buff,  buttons);
   ret += pio_u_short(pio_buff, x);
   ret += pio_u_short(pio_buff, y);
   ret += pio_int(pio_buff,     freq);
   ret += pio_u_short(pio_buff, bx1);
   ret += pio_u_short(pio_buff, by1);
   ret += pio_u_short(pio_buff, bx2);
   ret += pio_u_short(pio_buff, by2);
   if(ret == 0){
      ret = write(__mouse_io_file,pio_buff->buffer,pio_buff->buffer_pos);
      if(ret != pio_buff->buffer_pos)
	 return(-1);
      ret = 0;
   }
   return(ret);
}

/*
 * mouse_set_coordinates()
 *    Change the location of the mouse point.
 */
int
mouse_set_coordinates(ushort x, ushort y)
{
   uchar  op     = MOUSE_SET_COORDINATES;
   uchar  dummy0 = 0;
   ushort dummy1 = 0;
   int    dummy2 = 0;

   return(mouse_write(&op,&dummy0,&x,&y,&dummy2,
		      &dummy1,&dummy1,&dummy1,&dummy1));
}

/*
 * mouse_set_bounds()
 *    Change the bounding box for the mouse.
 */
int
mouse_set_bounds(ushort x1, ushort y1, ushort x2, ushort y2)
{
   uchar  op     = MOUSE_SET_BOUNDS;
   uchar  dummy0 = 0;
   ushort dummy1 = 0;
   int    dummy2 = 0;

   return(mouse_write(&op,&dummy0,&dummy1,&dummy1,&dummy2,&x1,&y1,&x2,&y2));
}

/*
 * mouse_set_update_freq()
 *    Change the number of times per second the mouse data is updated.
 */
int
mouse_set_update_freq(int freq)
{
   uchar  op     = MOUSE_SET_UPDATE_FREQ;
   uchar  dummy0 = 0;
   ushort dummy1 = 0;

   return(mouse_write(&op,&dummy0,&dummy1,&dummy1,&freq,
		      &dummy1,&dummy1,&dummy1,&dummy1));
}

/*
 * mouse_get_coordinates()
 *    Get the current location of the mouse.
 */
int
mouse_get_coordinates(ushort *x, ushort *y)
{
   uchar  dummy0 = 0;
   ushort dummy1 = 0;
   int    dummy2 = 0;

   return(mouse_read(&dummy0,x,y,&dummy2, &dummy1,&dummy1,&dummy1,&dummy1));
}

/*
 * mouse_get_buttons()
 *    Get the current status of the buttons on the mouse.
 */
int
mouse_get_buttons(uchar *buttons)
{
   ushort dummy1 = 0;
   int    dummy2 = 0;

   return(mouse_read(buttons,&dummy1,&dummy1,&dummy2, 
		     &dummy1,&dummy1,&dummy1,&dummy1));
}

/*
 * mouse_get_bounds()
 *    Get the current bounding box of the mouse.
 */
int
mouse_get_bounds(ushort *x1, ushort *y1, ushort *x2, ushort *y2)
{
   uchar  dummy0 = 0;
   ushort dummy1 = 0;
   int    dummy2 = 0;

   return(mouse_read(&dummy0,&dummy1,&dummy1,&dummy2,x1,y1,x2,y2));
}

/*
 * mouse_get_update_freq()
 *    Get the current update frequency of the mouse.
 */
int
mouse_get_update_freq(int *freq)
{
   uchar  dummy0 = 0;
   ushort dummy1 = 0;

   return(mouse_read(&dummy0,&dummy1,&dummy1,freq,
		     &dummy1,&dummy1,&dummy1,&dummy1));
}



