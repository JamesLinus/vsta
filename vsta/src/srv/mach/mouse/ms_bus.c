/* 
 * ms_bus.c
 *    Interrupt driven driver for the NEC bus mouse.
 *
 * Copyright (C) 1993 by G.T.Nicol, all rights reserved.
 *
 * A lot of this code is just taken from the Linux source.
 */

#include <stdio.h>
#include <std.h>
#include <sys/mman.h>
#include "machine.h"
#include "mouse.h"

/*
 * Our function table
 */
static mouse_function_table ms_bus_functions = {
   NULL,                               /* mouse_poller_entry_point */
   ms_bus_interrupt,                   /* mouse_interrupt          */
   ms_bus_coordinates,                 /* mouse_coordinates        */
   ms_bus_bounds,                      /* mouse_bounds             */
   NULL,                               /* mouse_update_period      */
};

/*
 * ms_bus_interrupt()
 *    Handle a mouse interrupt.
 */
void
ms_bus_interrupt(void)
{
   short new_x, new_y;
   uchar dx, dy, buttons;
   
   new_x = mouse_data.pointer_data.x;
   new_y = mouse_data.pointer_data.y;

   outportb(MS_BUS_CONTROL_PORT, MS_BUS_COMMAND_MODE);
   outportb(MS_BUS_DATA_PORT, (inportb(MS_BUS_DATA_PORT) | 0x20));
   
   outportb(MS_BUS_CONTROL_PORT, MS_BUS_READ_X);
   dx = inportb(MS_BUS_DATA_PORT);
   
   outportb(MS_BUS_CONTROL_PORT, MS_BUS_READ_Y);
   dy = inportb(MS_BUS_DATA_PORT);
   
   outportb(MS_BUS_CONTROL_PORT, MS_BUS_READ_BUTTONS);
   buttons = ~(inportb(MS_BUS_DATA_PORT)) & 0x07;
   
   outportb(MS_BUS_CONTROL_PORT, MS_BUS_COMMAND_MODE);
   outportb(MS_BUS_DATA_PORT, (inportb(MS_BUS_DATA_PORT) & 0xdf));

   /*
    *  If they've changed, update  the current coordinates
    */
   if (dx != 0 || dy != 0) {
      new_x += dx;
      new_y += dy;
      /*
       *  Make sure we honour the bounding box
       */
      if (new_x < mouse_data.pointer_data.bx1)
         new_x = mouse_data.pointer_data.bx1;
      if (new_x > mouse_data.pointer_data.bx2)
         new_x = mouse_data.pointer_data.bx2;
      if (new_y < mouse_data.pointer_data.by1)
         new_y = mouse_data.pointer_data.by1;
      if (new_y > mouse_data.pointer_data.by2)
         new_y = mouse_data.pointer_data.by2;
      /*
       *  Set up the new mouse position
       */
      mouse_data.pointer_data.x = new_x;
      mouse_data.pointer_data.y = new_y;
   }

   /*
    * Not sure about this... but I assume that the MS mouse is the
    * same as the NEC one.
    */
   switch (buttons) {                 /* simulate a 3 button mouse here */
   case 4:
      mouse_data.pointer_data.buttons = (1 << 1);       /* left   */
      break;
   case 1:
      mouse_data.pointer_data.buttons = (1 << 3);       /* right  */
      break;
   case 0:
      mouse_data.pointer_data.buttons = (1 << 2);       /* middle */
      break;
   default:
      mouse_data.pointer_data.buttons = 0;              /* none   */
   };
}


/*
 * ms_bus_bounds()
 *    Change or read the mouse bounding box.
 */
void
ms_bus_bounds(ushort x1, ushort y1, ushort x2, ushort y2)
{
   mouse_data.pointer_data.bx1 = x1;
   mouse_data.pointer_data.bx2 = x2;
   mouse_data.pointer_data.by1 = y1;
   mouse_data.pointer_data.by2 = y2;
}

/*
 * ms_bus_coordinates()
 *    Change or read the mouse coordinates.
 */
void
ms_bus_coordinates(ushort x, ushort y)
{
   mouse_data.pointer_data.x = x;
   mouse_data.pointer_data.y = y;
}

/*
 * ms_bus_initialise()
 *    Initialise the mouse system.
 */
int
ms_bus_initialise(int argc, char **argv)
{
   int mouse_seen = FALSE;
   int loop,dummy;

   /*
    *  Initialise the system data.
    */
   mouse_data.functions             = ms_bus_functions;
   mouse_data.pointer_data.x        = 320;
   mouse_data.pointer_data.y        = 320;
   mouse_data.pointer_data.buttons  = 0;
   mouse_data.pointer_data.bx1      = 0;
   mouse_data.pointer_data.by1      = 0;
   mouse_data.pointer_data.bx2      = 639;
   mouse_data.pointer_data.by2      = 399;
   mouse_data.irq_number            = MS_BUS_IRQ;
   mouse_data.update_frequency      = 0;

   /*
    * Parse our args...
    */
   for(loop=1; loop<argc; loop++){
      if(strcmp(argv[loop],"-x_size") == 0){
	 if(++loop == argc){
	    fprintf(stderr,"Mouse: bad -x_size parameter.\n");
	    break;
	 }
	 mouse_data.pointer_data.x   = atoi(argv[loop])/2;
	 mouse_data.pointer_data.bx2 = atoi(argv[loop]);
      }
      if(strcmp(argv[loop],"-y_size") == 0){
	 if(++loop == argc){
	    fprintf(stderr,"Mouse: bad -y_size parameter.\n");
	    break;
	 }
	 mouse_data.pointer_data.y   = atoi(argv[loop])/2;
	 mouse_data.pointer_data.by2 = atoi(argv[loop]);
      }
   }

   /*
    * Get our hardware ports.
    */
   if (enable_io(MICROSOFT_LOW_PORT, MICROSOFT_HIGH_PORT) < 0) {
      fprintf(stderr, "Mouse: Unable to enable I/O ports for mouse.\n");
      return (-1);
   }

   /*
    * Check for the mouse.
    */
   if (inportb(MS_BUS_ID_PORT) == 0xde) {
      __msleep(100);
      dummy = inportb(MS_BUS_ID_PORT);
      for (loop = 0; loop < 4; loop++) {
	 __msleep(100);
	 if (inportb(MS_BUS_ID_PORT) == 0xde) {
	    __msleep(100);
	    if (inportb(MS_BUS_ID_PORT) == dummy) {
	       mouse_seen = TRUE;
	    } else {
	       mouse_seen = FALSE;
	    }
	 } else
	    mouse_seen = FALSE;
      }
   }

   if (mouse_seen == FALSE) {
      return(-1);
   }

   mouse_data.functions.mouse_poller_entry_point = NULL;
   mouse_data.enable_interrupts                  = TRUE;

   /*
    * Enable mouse and go!
    */
   outportb(MS_BUS_CONTROL_PORT, MS_BUS_START);
   MS_BUS_INT_ON();

   printf("Microsoft Bus mouse detected and installed.\n");
   return (0);
}




