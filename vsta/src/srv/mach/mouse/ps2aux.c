/* 
 * ms_bus.c
 *    Interrupt driven driver for the NEC bus mouse.
 *
 * Copyright (C) 1994 by David J. Hudson, all rights reserved.
 *
 * A lot of the code in this file is extracted from the other mouse drivers
 * written by Gavin Nicol and from the Linux ps2 mouse driver.  I hope I've
 * managed to keep the coding style consistent with the rest of the server!
 */

#include <stdio.h>
#include <std.h>
#include <syslog.h>
#include <time.h>
#include <mach/io.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include "mouse.h"

/*
 * Handle the queueing of commands
 */
struct ps2aux_rbuffer {
   uint head;
   uint tail;
   uchar data[PS2AUX_BUFFER_SIZE];
};

/*
 * Driver variables
 */
struct ps2aux_rbuffer *rbuffer = NULL;

/*
 * Our function table
 */
static mouse_function_table ps2aux_functions = {
   NULL,                               /* mouse_poller_entry_point */
   ps2aux_interrupt,                   /* mouse_interrupt          */
   ps2aux_coordinates,                 /* mouse_coordinates        */
   ps2aux_bounds,                      /* mouse_bounds             */
   NULL,                               /* mouse_update_period      */
};

/*
 * ps2aux_poll_status()
 *    Check whether we have any problems with full input/output buffers
 *
 * We return zero if there's a problem, otherwise non-zero
 */
static int
ps2aux_poll_status(void)
{
   int retries = 0;

   while ((inportb(PS2AUX_STATUS_PORT) 
	   & (PS2AUX_OUTBUF_FULL | PS2AUX_INBUF_FULL))
	  && retries < PS2AUX_MAX_RETRIES) {
      if (inportb(PS2AUX_STATUS_PORT) & PS2AUX_OUTBUF_FULL
          == PS2AUX_OUTBUF_FULL) {
         inportb(PS2AUX_INPUT_PORT);
      }
      __msleep(1);
      retries++;
   }
   
   return !(retries == PS2AUX_MAX_RETRIES);
}

/*
 * ps2aux_write_device()
 *    Write to the aux device
 */
static void
ps2aux_write_device(int val)
{
   ps2aux_poll_status();
   outportb(PS2AUX_COMMAND_PORT, PS2AUX_WRITE_MAGIC);
   					/* Write the magic number! */
   ps2aux_poll_status();
   outportb(PS2AUX_OUTPUT_PORT, val);	/* And then the data */
}

/*
 * ps2aux_write_command()
 *    Write a command to the aux port
 */
static void
ps2aux_write_command(int cmd)
{
   ps2aux_poll_status();
   outportb(PS2AUX_COMMAND_PORT, PS2AUX_COMMAND_WRITE);
   					/* Write the command follows ID */
   ps2aux_poll_status();
   outportb(PS2AUX_OUTPUT_PORT, cmd);	/* And then the command */
}

/*
 * ps2aux_read_device()
 *    Read data back from the mouse port
 *
 * If we timeout we return zero, otherwise non-zero
 */
static int
ps2aux_read_device(uchar *val)
{
   int retries = 0;
   
   while ((inportb(PS2AUX_STATUS_PORT) & PS2AUX_OUTBUF_FULL)
           != PS2AUX_OUTBUF_FULL
	  && retries < PS2AUX_MAX_RETRIES) {
      __msleep(1);
      retries++;
   }

   if ((inportb(PS2AUX_STATUS_PORT) & PS2AUX_OUTBUF_FULL)
       == PS2AUX_OUTBUF_FULL) {
      *val = inportb(PS2AUX_INPUT_PORT); 
   }
   
   return !(retries == PS2AUX_MAX_RETRIES);
}

/*
 * ps2aux_probe()
 *    Look to see if we can find a mouse on the aux port
 */
static int
ps2aux_probe(void)
{
   uchar ack;

   /*
    * Send a reset and wait for an ack response!
    */
   ps2aux_write_device(PS2AUX_RESET);
   if (!ps2aux_read_device(&ack)) {
      return (0);
   }
   if (ack != PS2AUX_ACK) {
      return (0);
   }

   /*
    * Ensure that we see a bat (basic assurance test) response
    */
   if (!ps2aux_read_device(&ack)) {
      return (0);
   }
   if (ack != PS2AUX_BAT) {
      return (0);
   }

   /*
    * Check that we get a pointing device ID back
    */
   if (!ps2aux_read_device(&ack)) {
      return (0);
   }
   if (ack != 0) {
      return (0);
   }

   return (1);   
}

/*
 * ps2aux_interrupt()
 *    Handle a mouse interrupt.
 */
void
ps2aux_interrupt(void)
{
   int head, maxhead, diff;
   short new_x, dx, new_y, dy;

   /*
    * Arrange to read the pending information
    */
   maxhead = (rbuffer->tail - 1) & (PS2AUX_BUFFER_SIZE - 1);
   head = rbuffer->head;
   rbuffer->data[head] = inportb(PS2AUX_INPUT_PORT);
   if (head != maxhead) {
      rbuffer->head = (rbuffer->head + 1) & (PS2AUX_BUFFER_SIZE - 1);
   }

   /*
    * Have we got a full 3 bytes of position data?  If so then start updating
    * all of the positional stats
    */
   diff = rbuffer->head - rbuffer->tail;
   if (diff < 0) {
      diff += PS2AUX_BUFFER_SIZE;
   }

   if (diff < 3) {
      return;
   }

   new_x = mouse_data.pointer_data.x;
   new_y = mouse_data.pointer_data.y;

   while (diff >= 3) {
      mouse_data.pointer_data.buttons = rbuffer->data[rbuffer->tail] & 0x07;
      if (!(rbuffer->data[rbuffer->tail] & PS2AUX_X_OVERFLOW)) {
         dx = rbuffer->data[(rbuffer->tail + 1) & (PS2AUX_BUFFER_SIZE - 1)];
         if (rbuffer->data[rbuffer->tail] & PS2AUX_X_SIGN) {
  	   dx = dx - 256;
         }
      } else {
	 dx = 255;
         if (rbuffer->data[rbuffer->tail] & PS2AUX_X_SIGN) {
  	   dx = -256;
         }
      }
      if (!(rbuffer->data[rbuffer->tail] & PS2AUX_Y_OVERFLOW)) {
         dy = rbuffer->data[(rbuffer->tail + 2) & (PS2AUX_BUFFER_SIZE - 1)];
         if (rbuffer->data[rbuffer->tail] & PS2AUX_Y_SIGN) {
  	   dy = dy - 256;
         }
      } else {
	 dy = 255;
         if (rbuffer->data[rbuffer->tail] & PS2AUX_Y_SIGN) {
  	   dy = -256;
         }
      }
      diff -= 3;
      rbuffer->tail = (rbuffer->tail + 3) & (PS2AUX_BUFFER_SIZE - 1);
      new_x += dx;
      new_y += dy;
   }

   /*
    *  Make sure we honour the bounding box
    */
   if (new_x < mouse_data.pointer_data.bx1) {
      new_x = mouse_data.pointer_data.bx1;
   }
   if (new_x > mouse_data.pointer_data.bx2) {
      new_x = mouse_data.pointer_data.bx2;
   }
   if (new_y < mouse_data.pointer_data.by1) {
      new_y = mouse_data.pointer_data.by1;
   }
   if (new_y > mouse_data.pointer_data.by2) {
      new_y = mouse_data.pointer_data.by2;
   }

   /*
    *  Set up the new mouse position
    */
   mouse_data.pointer_data.x = new_x;
   mouse_data.pointer_data.y = new_y;
}


/*
 * ps2aux_bounds()
 *    Change or read the mouse bounding box.
 */
void
ps2aux_bounds(ushort x1, ushort y1, ushort x2, ushort y2)
{
   mouse_data.pointer_data.bx1 = x1;
   mouse_data.pointer_data.bx2 = x2;
   mouse_data.pointer_data.by1 = y1;
   mouse_data.pointer_data.by2 = y2;
}

/*
 * ps2aux_coordinates()
 *    Change or read the mouse coordinates.
 */
void
ps2aux_coordinates(ushort x, ushort y)
{
   mouse_data.pointer_data.x = x;
   mouse_data.pointer_data.y = y;
}

/*
 * ps2aux_initialise()
 *    Initialise the mouse system.
 */
int
ps2aux_initialise(int argc, char **argv)
{
   int loop;

   /*
    *  Initialise the system data.
    */
   mouse_data.functions             = ps2aux_functions;
   mouse_data.pointer_data.x        = 320;
   mouse_data.pointer_data.y        = 320;
   mouse_data.pointer_data.buttons  = 0;
   mouse_data.pointer_data.bx1      = 0;
   mouse_data.pointer_data.by1      = 0;
   mouse_data.pointer_data.bx2      = 639;
   mouse_data.pointer_data.by2      = 399;
   mouse_data.irq_number            = PS2AUX_IRQ;
   mouse_data.update_frequency      = 0;

   /*
    * Parse our args...
    */
   for (loop = 1; loop < argc; loop++ ) {
      if (strcmp(argv[loop], "-x_size") == 0) {
	 if (++loop == argc) {
	    syslog(LOG_ERR, "%s bad -x_size parameter", mouse_sysmsg);
	    break;
	 }
	 mouse_data.pointer_data.x = atoi(argv[loop]) / 2;
	 mouse_data.pointer_data.bx2 = atoi(argv[loop]);
      }
      if (strcmp(argv[loop], "-y_size") == 0) {
	 if (++loop == argc) {
	    syslog(LOG_ERR, "%s bad -y_size parameter", mouse_sysmsg);
	    break;
	 }
	 mouse_data.pointer_data.y = atoi(argv[loop]) / 2;
	 mouse_data.pointer_data.by2 = atoi(argv[loop]);
      }
   }

   /*
    * Establish the receive ring buffer details
    */
   rbuffer = (struct ps2aux_rbuffer *)malloc(sizeof(struct ps2aux_rbuffer));
   if (!rbuffer) {
      syslog(LOG_ERR, "%s unable to allocate ring buffer", mouse_sysmsg);
      return(-1);
   }

   /*
    * Get our hardware ports
    */
   if (enable_io(PS2AUX_LOW_PORT, PS2AUX_HIGH_PORT) < 0) {
      syslog(LOG_ERR, "%s unable to enable I/O ports for mouse", mouse_sysmsg);
      return(-1);
   }

   /*
    * Check for the mouse
    */
   if (!ps2aux_probe()) {
      return(-1);
   }

   /*
    * We've found our pointing device, so now let's complete the
    * initialisation of the driver
    */
   rbuffer->head = 0;
   rbuffer->tail = 0;
   mouse_data.functions.mouse_poller_entry_point = NULL;
   mouse_data.enable_interrupts = TRUE;

   /*
    * Enable mouse and go!
    */
   outportb(PS2AUX_COMMAND_PORT, PS2AUX_ENABLE_CONTROLLER);
   ps2aux_write_device(PS2AUX_ENABLE_DEVICE);
   ps2aux_write_command(PS2AUX_ENABLE_INTERRUPTS);

   syslog(LOG_INFO, "%s PS/2 mouse detected and installed", mouse_sysmsg);
   return (0);
}
