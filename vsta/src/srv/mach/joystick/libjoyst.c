/*
 * libjoystick.c
 *    A simple library for dealing with the joystick device server.
 *
 * This library provides a simple API to the joystick device server.  The
 * core function is joystick_read() which allows the position and button
 * status for a "joystick" type device to be determined.  Additional functions
 * are provided to control calibration and scaling of the results, and "raw"
 * position data may also be read if needed.
 *
 * Some of the code for this library was borrowed from Gavin Thomas Nicol's
 * libmouse.c library for VSTa.
 */

#include "pio.h"
#include <stdio.h>
#include <fcntl.h>
#include <std.h>
#include "libjoystick.h"

/*
 * This is the file descriptor we use for joystick I/O.
 */
static int __joystick_io_file = -1;

/*
 * We use this just in case some (l)user descides to open the joystick more
 * than once.
 */
static int __joystick_open = 0;

/*
 * joystick_connect()
 *	Open the joystick device for reading and writing.
 */
int  joystick_connect(void)
{
  __joystick_io_file = open("/dev/joystick", O_RDWR);
  __joystick_open += 1;
  return __joystick_io_file;
}

/*
 * joystick_disconnect()
 *	If the joystick device needs closing, close the file descriptor.
 */
int joystick_disconnect(void)
{
  if(__joystick_open > 0) {
    close(__joystick_io_file);
    __joystick_io_file = -1;
  } else {
    __joystick_open -= 1;
  }

  return 0;
}

/*
 * joystick_read()
 *	Low-level read function for the joystick device.
 *
 * The aim here is that no matter what has happened before, and what other
 * parameters are set in this library, we issue a hardware read request
 */
int joystick_read(ushort *ch_a, ushort *ch_b, ushort *ch_c, 
		  ushort *ch_d, uchar *btns)
{
  uchar id;
  static pio_buffer_t *pio_buff = NULL;

  int size, ret = 0;

  /*
   * Make sure the device is open.
   */
  if (__joystick_io_file == -1) {
    fprintf(stderr,"joystick device not open.\n");
    return -1;
  }

  /*
   * Create our PIO buffer if needed. The size 1024 is probably
   * too large.
   */
  if (pio_buff == NULL) {
    pio_buff = pio_create_buffer(NULL, 1024);
  }

  /*
   * Read the data into the PIO buffer.
   */
  size = read(__joystick_io_file, pio_buff->buffer, 1024);
  if (size == -1) {
    fprintf(stderr,"Bad read on joystick device\n");
    return -1;
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
  ret += pio_u_char(pio_buff, &id);
  ret += pio_u_char(pio_buff, btns);
  ret += pio_u_short(pio_buff, ch_a);
  ret += pio_u_short(pio_buff, ch_b);
  ret += pio_u_short(pio_buff, ch_c);
  ret += pio_u_short(pio_buff, ch_d);

  if (id != 'J') {
    ret = -1;
  }

  return ret;
}
