/* 
 * nec_bus.c
 *    Combined polling/interrupt driven driver for the NEC bus mouse.
 *
 * Copyright (C) 1993 by G.T.Nicol, all rights reserved.
 *
 * This driver does not currently support high-resolution mode.
 */

#include <stdio.h>
#include <std.h>
#include <sys/mman.h>
#include "machine.h"
#include "mouse.h"

/*
 * Our function table
 */
static mouse_function_table pc98_bus_functions = {
   pc98_bus_poller_entry_point,          /* mouse_poller_entry_point */
   pc98_bus_interrupt,                   /* mouse_interrupt          */
   pc98_bus_coordinates,                 /* mouse_coordinates        */
   pc98_bus_bounds,                      /* mouse_bounds */
   pc98_bus_update_period,               /* mouse_update_period      */
};

/*
 * General data used by the driver
 */
static ushort pc98_bus_delay_period   = 100;
static uint   pc98_update_allowed     = TRUE;
static int    pc98_interrupt_driven   = FALSE;
static int    pc98_polling_driver     = FALSE;
static int    pc98_irq_number         = 0x6;
static int    pc98_delay_period       = 100;

/*
 * pc98_bus_check_status()
 *    Read in the mouse coordinates.
 */
static inline void
pc98_bus_check_status(void)
{
   short new_x, new_y;
   char x_off = 0;
   char y_off = 0;
   uchar mask = inportb(PC98_MOUSE_RPORT_C) & 0xf0;

   if (!pc98_update_allowed)
      return;

   set_semaphore(&pc98_update_allowed, FALSE);
   new_x = mouse_data.pointer_data.x;
   new_y = mouse_data.pointer_data.y;

   /*
    *  First we read in the X offset
    */
   mask = mask & 0xa0;
   outportb(PC98_MOUSE_WPORT_C, mask);
   x_off = inportb(PC98_MOUSE_RPORT_A) & 0x0f;
   outportb(PC98_MOUSE_WPORT_C, mask | 0x20);
   x_off |= (inportb(PC98_MOUSE_RPORT_A) & 0x0f) << 4;

   /*
    *  Now we read in the Y offset
    */
   outportb(PC98_MOUSE_WPORT_C, mask | 0x40);
   y_off = inportb(PC98_MOUSE_RPORT_A) & 0x0f;
   outportb(PC98_MOUSE_WPORT_C, mask | 0x60);
   y_off |= (inportb(PC98_MOUSE_RPORT_A) & 0x0f) << 4;

   /*
    *  If they've changed, update  the current coordinates
    */
   if (x_off != 0 || y_off != 0) {
      outportb(PC98_MOUSE_WPORT_C, 0x10);
      outportb(PC98_MOUSE_WPORT_C, 0x90);
      outportb(PC98_MOUSE_WPORT_C, 0x10);
      new_x += x_off;
      new_y += y_off;
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
   mask = (inportb(PC98_MOUSE_RPORT_A) >> 5) & 0x5;
   switch (mask) {                       /* simulate a 3 button mouse here */
   case 4:
      mouse_data.pointer_data.buttons = (1 << 3);       /* left   */
      break;
   case 1:
      mouse_data.pointer_data.buttons = (1 << 1);       /* right  */
      break;
   case 0:
      mouse_data.pointer_data.buttons = (1 << 2);       /* middle */
      break;
   default:
      mouse_data.pointer_data.buttons = 0;              /* none   */
   };
   set_semaphore(&pc98_update_allowed, TRUE);
}

/*
 * pc98_bus_interrupt()
 *    Handle a mouse interrupt.
 */
void
pc98_bus_interrupt(void)
{
   pc98_bus_check_status();
}


/*
 * pc98_bus_poller_entry_point()
 *    Main loop of the polling thread.
 */
void
pc98_bus_poller_entry_point(void)
{
   for (;;) {
      pc98_bus_check_status();
      if (pc98_bus_delay_period) {
         __msleep(pc98_bus_delay_period);
      }
   }
}

/*
 * pc98_bus_bounds()
 *    Change or read the mouse bounding box.
 */
void
pc98_bus_bounds(ushort x1, ushort y1, ushort x2, ushort y2)
{
   while (!pc98_update_allowed);
   set_semaphore(&pc98_update_allowed, FALSE);
   mouse_data.pointer_data.bx1 = x1;
   mouse_data.pointer_data.bx2 = x2;
   mouse_data.pointer_data.by1 = y1;
   mouse_data.pointer_data.by2 = y2;
   set_semaphore(&pc98_update_allowed, TRUE);
}

/*
 * pc98_bus_coordinates()
 *    Change or read the mouse coordinates.
 */
void
pc98_bus_coordinates(ushort x, ushort y)
{
   while (!pc98_update_allowed);
   set_semaphore(&pc98_update_allowed, FALSE);
   mouse_data.pointer_data.x = x;
   mouse_data.pointer_data.y = y;
   set_semaphore(&pc98_update_allowed, TRUE);
}

/*
 * pc98_bus_update_period()
 *    Read the mouse button status
 */
void
pc98_bus_update_period(ushort period)
{
   if (period == 0)
      period = 5;
   set_semaphore(&pc98_update_allowed, FALSE);
   pc98_bus_delay_period = 1000 / period;
   mouse_data.update_frequency = period;
   set_semaphore(&pc98_update_allowed, TRUE);
}

/*
 * pc98_usage()
 *    Infor the user how to use the pc98 bus driver
 */
static void
pc98_usage(void)
{
   fprintf(stderr,
     "Usage: mouse -type pc98bus [-interrupt|-poll] [-irq #] [-delay #]\n");
   exit(1);
}

/*
 * pc98_parse_args()
 *    Parse the command line...
 */
static void
pc98_parse_args(int argc, char **argv)
{
   int arg;

   for (arg = 1; arg < argc; arg++)
      if (argv[arg][0] == '-') {
         if (strcmp(&argv[arg][1], "type") == 0) {
            if (++arg == argc)
               pc98_usage();
         } else if (strcmp(&argv[arg][1], "interrupt") == 0)
            pc98_interrupt_driven = 1;
         else if (strcmp(&argv[arg][1], "poll") == 0)
            pc98_polling_driver = 1;
         else if (strcmp(&argv[arg][1], "irq") == 0) {
            if (++arg == argc)
               pc98_usage();
            pc98_irq_number = atoi(argv[arg]);
         } else if (strcmp(&argv[arg][1], "delay") == 0) {
            if (++arg == argc)
               pc98_usage();
            pc98_delay_period = atoi(argv[arg]);
         } else
            pc98_usage();
      } else
         break;

   if (pc98_interrupt_driven + pc98_polling_driver > 1)
      pc98_usage();

   if (argc - arg != 0)
      pc98_usage();
}


/*
 * pc98_bus_initialise()
 *    Initialise the mouse system.
 */
int
pc98_bus_initialise(int argc, char **argv)
{
   pc98_parse_args(argc, argv);

   /*
    *  Initialise the system data.
    */
   mouse_data.functions             = pc98_bus_functions;
   mouse_data.pointer_data.x        = 320;
   mouse_data.pointer_data.y        = 320;
   mouse_data.pointer_data.buttons  = 0;
   mouse_data.pointer_data.bx1      = 0;
   mouse_data.pointer_data.by1      = 0;
   mouse_data.pointer_data.bx2      = 639;
   mouse_data.pointer_data.by2      = 399;
   mouse_data.irq_number            = pc98_irq_number;
   mouse_data.update_frequency      = pc98_delay_period;


   /*
    * Perform general hardware initialisations. We try to detect
    * when it's there or not, but it is often insuccessful....
    */
   if (enable_io(PC98_MOUSE_LOW_PORT, PC98_MOUSE_HIGH_PORT) < 0) {
      fprintf(stderr, "Mouse: Unable to enable I/O ports for mouse.\n");
      return (-1);
   }
   outportb(PC98_MOUSE_MODE_PORT, 0x93); /* initialise */
   outportb(PC98_MOUSE_MODE_PORT, 0x9);  /* disable interrupts */
   outportb(PC98_MOUSE_WPORT_C, 0x10);   /* HC = 0 */
   if (inportb(PC98_MOUSE_RPORT_C) & 0x80 != 0) {
      fprintf(stderr, "pc98_bus: mouse not connected\n");
      return (-1);
   }
   outportb(PC98_MOUSE_WPORT_C, 0x90);   /* HC = 1 */
   if (inportb(PC98_MOUSE_RPORT_C) & 0x80 == 0) {
      fprintf(stderr, "pc98_bus: mouse not connected\n");
      return (-1);
   }
   outportb(PC98_MOUSE_WPORT_C, 0x10);

   /*
    *  Taylor our driver to be either interrupt driven, or polling
    */
   if (pc98_interrupt_driven) {
      mouse_data.functions.mouse_poller_entry_point = NULL;
      mouse_data.enable_interrupts = TRUE;
      outportb(PC98_MOUSE_MODE_PORT, 0x93);     /* initialise */
      outportb(PC98_MOUSE_MODE_PORT, 0x8);      /* enable interrupts */
   } else {
      mouse_data.functions.mouse_interrupt = NULL;
      mouse_data.enable_interrupts = FALSE;
      pc98_bus_update_period(mouse_data.update_frequency);
   }
   return (0);
}




