/* 
 * mouse.c
 *    A fairly universal mouse driver.
 *
 * Copyright (C) 1993 by G.T.Nicol, all rights reserved.
 */

#include <stdio.h>
#include <std.h>
#include <syslog.h>
#include <string.h>
#include <pio.h>
#include "mouse.h"

/*
 * We use the following table to bootstrap the system. The command
 * line is searched for the -type argument, and that is used to index
 * into this table. The initialisation code is called, and that
 * sets uo the rest of the system parameters.
 */
struct {
   char *driver_name;
   int (*driver_initialise) (int argc, char **argv);
} mouse_drivers[] = {
   { "pc98_bus",         pc98_bus_initialise,     },
   { "microsoft_bus",    ms_bus_initialise,       },
   { "logitech_bus",     logitech_bus_initialise, },
   { "serial",		 ibm_serial_initialise,	  },
   { "ps2aux",		 ps2aux_initialise,	  },
   { NULL,               NULL,                    },  /* last entry */
};

/*
 * We use the following table to hold various bits and peices of data
 * about the mouse.
 */
mouse_data_t mouse_data = {
   {
      NULL,                              /* mouse_poller_entry_point      */
      NULL,                              /* mouse_interrupt               */
      NULL,                              /* mouse_coordinates             */
      NULL,                              /* mouse_bounds                  */
      NULL,                              /* mouse_update_period           */
   },
   {
      0,                                 /* X coordinate                  */
      0,                                 /* Y coordinate                  */
      0,                                 /* Pressed buttons               */
      0,                                 /* Bounding box X1               */
      0,                                 /* "     "   Y1               */
      0,                                 /* Bounding box X2               */
      0,                                 /* "     "   Y2               */
   },
   -1,                                   /* irq_number                    */
   -1,                                   /* update_frequency              */
   FALSE,                                /* enable_interrupts             */
   -1,                                    /* type_id                       */
};

/*
 * mouse_read()
 *    Handle a read request.
 */
void
mouse_read(struct msg * m, struct file * f)
{
   static uchar id = 'M';
   static pio_buffer_t *buffer = NULL;

   if(buffer == NULL)
     buffer = pio_create_buffer(NULL,0);

   /*
    * Use pio to give us a nicely formatted buffer
    */
   pio_reset_buffer(buffer);
   pio_u_char(buffer, &id);
   pio_u_char(buffer, &mouse_data.pointer_data.buttons);
   pio_u_short(buffer,&mouse_data.pointer_data.x);
   pio_u_short(buffer,&mouse_data.pointer_data.y);
   pio_int(buffer,    &mouse_data.update_frequency);
   pio_u_short(buffer,&mouse_data.pointer_data.bx1);
   pio_u_short(buffer,&mouse_data.pointer_data.by1);
   pio_u_short(buffer,&mouse_data.pointer_data.bx2);
   pio_u_short(buffer,&mouse_data.pointer_data.by2);

   m->m_buf    = buffer->buffer;
   m->m_buflen = buffer->buffer_pos;
   m->m_nseg   = 1;
   m->m_arg    = m->m_arg1 = 0;
   msg_reply(m->m_sender, m);
}

/*
 * mouse_write()
 *    Handle a write request.
 */
void
mouse_write(struct msg * m, struct file * f)
{
   uchar id,buttons,op;
   ushort x,y,bx1,bx2,by1,by2;
   int freq,ret = 0;
   pio_buffer_t temp,*buffer = &temp; 

   temp.buffer      = m->m_buf;
   temp.buffer_size = m->m_buflen;
   temp.buffer_pos  = 0;
   temp.buffer_type = PIO_INPUT;

   /*
    * Use pio to read in the data
    */
   ret += pio_u_char(buffer, &id);
   ret += pio_u_char(buffer, &op);
   ret += pio_u_char(buffer, &buttons);
   ret += pio_int(buffer,    &freq);
   ret += pio_u_short(buffer,&x);
   ret += pio_u_short(buffer,&y);
   ret += pio_u_short(buffer,&bx1);
   ret += pio_u_short(buffer,&by1);
   ret += pio_u_short(buffer,&bx2);
   ret += pio_u_short(buffer,&by2);

   if(ret >= 0 && id == 'M'){
      switch(op) {
      case 0: 
	 break;
      case 1:
	 if(mouse_data.functions.mouse_coordinates != NULL){
	    (*mouse_data.functions.mouse_coordinates)(x,y);
	 }
	 break;
      case 2:
	 if(mouse_data.functions.mouse_bounds != NULL){
	    (*mouse_data.functions.mouse_bounds)(bx1,by1,bx2,by2);
	 }
	 break;
      case 3:
	 if(mouse_data.functions.mouse_update_period != NULL){
	    (*mouse_data.functions.mouse_update_period)((ushort)freq);
	 }
	 break;
      default:
	 syslog(LOG_ERR,"%s bad operation specified", mouse_sysmsg);
	 msg_err(m->m_sender, EINVAL);
	 return;
      };
      m->m_buflen = m->m_arg1 = m->m_nseg = 0;
      m->m_arg = temp.buffer_size;
      msg_reply(m->m_sender, m);
   } else {
      syslog(LOG_ERR, "%s got a bad data set", mouse_sysmsg);
      msg_err(m->m_sender, EINVAL);
   }
}

/*
 * mouse_initialise()
 *    Parse the command line and initialise the mouse driver
 */
void
mouse_initialise(int argc, char **argv) {
   int loop, param = -1;

   /*
    *  Look for a -type parameter. If none is found, die.
    */
   for (loop = 1; loop < argc; loop++) {
      if (strcmp(argv[loop], "-type") == 0 && loop++ < argc) {
         param = loop;
      }
   }
   if (param == -1) {
      syslog(LOG_ERR, "%s no mouse type specified - exiting", mouse_sysmsg);
      exit(1);
   }
   /*
    *  Use the parameter to look up the driver in the table. Die if not found.
    */
   for (loop = 0; mouse_drivers[loop].driver_name != NULL; loop++) {
      if (strcmp(mouse_drivers[loop].driver_name, argv[param]) == 0)
         break;
   }
   if (mouse_drivers[loop].driver_initialise == NULL) {
      syslog(LOG_ERR, "%s no driver for \"%s\" type mouse - exiting",
             mouse_sysmsg, argv[param]);
      exit(1);
   }
   mouse_data.type_id = loop;

   /* Initialise the driver */
   if ((*mouse_drivers[loop].driver_initialise) (argc, argv) == -1) {
      syslog(LOG_ERR, "%s unable to initialise \"%s\" driver - exiting\n",
             mouse_sysmsg, argv[param]);
      exit(1);
   }
}
