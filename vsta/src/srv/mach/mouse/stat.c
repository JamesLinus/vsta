/* 
 * stat.c 
 *    Handle the stat() and wstat() calls for the mouse driver.
 *
 * Copyright (C) 1993 by G.T.Nicol, all rights reserved.
 *
 * This code is based on the code for cons, which is based on the code
 * for the original cons driver.
 */

#include <sys/assert.h>
#include <string.h>
#include "mouse.h"

extern char *perm_print();

/*
 * mouse_stat()
 *     Handle the mouse stat() call.
 *
 * Simply build a message and return it to the sender.
 */
void
mouse_stat(struct msg * m, struct file * f)
{
   char buf[MAXSTAT];

   sprintf(buf, "type=x\nowner=0\ninode=0\ngen=%d\n", mouse_accgen);
   strcat(buf, perm_print(&mouse_prot));

   m->m_buf = buf;
   m->m_buflen = strlen(buf);
   m->m_nseg = 1;
   m->m_arg = m->m_arg1 = 0;

   msg_reply(m->m_sender, m);
}

/*
 * mouse_wstat()
 *     Handle the wstat() call for the mouse driver.
 */
void
mouse_wstat(struct msg * m, struct file * f)
{
   char *field, *val;

   /*
    *  See if common handling code can do it
    */
   if (do_wstat(m, &mouse_prot, f->f_flags, &field, &val) == 0)
      return;

   /*
    *  Process each kind of field we can write
    */
   if (!strcmp(field, "gen")) {
      /*
         Set access-generation field
      */
      if (val) {
         mouse_accgen = atoi(val);
      } else {
         mouse_accgen += 1;
      }
      f->f_gen = mouse_accgen;
   } else {
      /*
       *  Not a field we support...
       */
      msg_err(m->m_sender, EINVAL);
      return;
   }

   /*
    *  Return success
    */
   m->m_buflen = m->m_nseg = m->m_arg = m->m_arg1 = 0;
   msg_reply(m->m_sender, m);
}
