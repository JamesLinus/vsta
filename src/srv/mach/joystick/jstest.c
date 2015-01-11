#include <stdio.h>
#include <std.h>
#include "libjoystick.h"

void main(void)
{
  int i;
  uchar  btns, obtns = 0;
  ushort a = 0, oa, b = 0, ob, c = 0, oc, d = 0, od;

  if (joystick_connect() == -1) {
    fprintf(stderr,"Unable to connect to joystick\n");
    exit(1);
  }

  for (i = 0; i < 100; i++) {
    oa = a;
    ob = b;
    oc = c;
    od = d;
    obtns = btns;
    joystick_read(&a, &b, &c, &d, &btns);
    printf("%04x %04x %04x %04x %02x\n", a, b, c, d, btns);
  }

  if (joystick_disconnect() == -1) {
    fprintf(stderr,"Unable to disconnect from joystick\n");
    exit(1);
  }
}
