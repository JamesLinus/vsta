/*
 * Filename:	stat.c
 * Author:	Dave Hudson <dave@humbug.demon.co.uk>
 * Started:	5th January 1994
 * Last Update: 21st March 1994
 * Implemented:	GNU GCC version 2.5.7
 *
 * Description: Deals with stat read/write requests for the joystick device
 */


#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/perm.h>
#include <sys/fs.h>
#include "joystick.h"


extern char *perm_print();
extern int do_wstat(struct msg *, struct prot *, int, char **, char **);


extern struct prot js_prot;
extern uint js_accgen;
extern uchar js_mask;


/*
 * js_stat()
 *	Do stat
 */
void js_stat(struct msg *m, struct file *f)
{
  char buf[MAXSTAT];

  if (!(f->f_flags & ACC_READ)) {
    msg_err(m->m_sender, EPERM);
    return;
  }
  sprintf(buf, "type=x\nowner=0\ninode=0\ngen=%d\nchmask=%d\n",
  	  js_accgen, js_mask);
  strcat(buf, perm_print(&js_prot));
  m->m_buf = buf;
  m->m_arg = m->m_buflen = strlen(buf);
  m->m_nseg = 1;
  m->m_arg1 = 0;
  msg_reply(m->m_sender, m);
}


/*
 * js_wstat()
 *	Allow writing of supported stat messages
 */
void js_wstat(struct msg *m, struct file *f)
{
  char *field, *val;
  int x;

  /*
   * See if common handling code can do it
   */
  if (do_wstat(m, &js_prot, f->f_flags, &field, &val) == 0) {
    return;
  }

  /*
   * Process each kind of field we can write
   */
  if (!strcmp(field, "gen")) {
    /*
     * Set access-generation field
     */
    if (val) {
      js_accgen = atoi(val);
    } else {
      js_accgen += 1;
    }
    f->f_gen = js_accgen;
  } else if (!strcmp(field, "chmask")) {
    /*
     * Set channel connection mask
     */
    if (val) {
      x = atoi(val);
      if (x < 0 || x > JS_CH_MASK) {
        msg_err(m->m_sender, EINVAL);
	return;
      }
      js_mask = x;
    }
  } else {
    /*
     * Not a field we support so flag an error!
     */
    msg_err(m->m_sender, EINVAL);
    return;
  }

  /*
   * Return success
   */
  m->m_buflen = m->m_nseg = m->m_arg = m->m_arg1 = 0;
  msg_reply(m->m_sender, m);
}
