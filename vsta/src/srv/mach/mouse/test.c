#include <stdio.h>
#include "libmouse.h"

int 
main(void)
{
   uchar  buttons, obuttons;
   ushort x, y, ox, oy;

   if (mouse_connect() == -1){
      fprintf(stderr,"Unable to connect to mouse\n");
      exit(1);
   }

   for (;;) {
      mouse_get_buttons(&buttons);
      mouse_get_coordinates(&x, &y);
      if ((x != ox) || (y != oy) || (buttons != obuttons)) {
	      printf("%d %d %d %3d %3d\n",
		     (buttons & MOUSE_LEFT_BUTTON) > 0,
		     (buttons & MOUSE_MIDDLE_BUTTON) > 0,
		     (buttons & MOUSE_RIGHT_BUTTON) > 0,
		     x, y);
     	     ox = x;
	     oy = y;
	     obuttons = buttons;
     } else {
     	__msleep(200);
     }
   }
}
