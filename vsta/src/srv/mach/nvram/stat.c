/*
 * Filename:	stat.c
 * Author:	Dave Hudson <dave@humbug.demon.co.uk>
 * Started:	12th March 1994
 * Last Update: 9th May 1994
 * Implemented:	GNU GCC 1.42 (VSTa v1.3.1)
 *
 * Description: Deals with stat read/write requests for the "nvram" device
 */


#include <stdio.h>
#include <stdlib.h>
#include <sys/perm.h>
#include <sys/fs.h>
#include "nvram.h"


extern char *perm_print();
extern do_wstat(struct msg *, struct prot *, int, char **, char **);

extern struct prot nvram_prot;
extern uint nvram_accgen;
extern int nvram_bytes;
extern struct fentries fe[MAX_INO];
extern int bcd_count;


/*
 * nvram_stat()
 *	Do stat
 */
void nvram_stat(struct msg *m, struct file *f)
{
  char buf[MAXSTAT];

  if (!(f->f_flags & ACC_READ)) {
    msg_err(m->m_sender, EPERM);
    return;
  }
  sprintf(buf, "size=%d\ntype=%c\nowner=0\ninode=%d\ngen=%d\n", 
          (f->f_node == ALL_INO) ? nvram_bytes : fe[f->f_node].fe_size,
          (f->f_node == ROOT_INO) ? 'd' : 's', 
          f->f_node, nvram_accgen);
  strcat(buf, perm_print(&nvram_prot));
  if (f->f_node == RTC_INO) {
    /*
     * Is this the rtc node?  If it is we add extra stat fields to identify
     * whether or not we're using BCD counting, whether or not we're
     * using 12 or 24 hour clocks and whether or not we're using daylight
     * saving mode.
     */
    sprintf(&buf[strlen(buf)], "count_mode=%d\n", nvram_get_count_mode());
    sprintf(&buf[strlen(buf)], "clock_mode=%d\n", nvram_get_clock_mode());
    sprintf(&buf[strlen(buf)], "daysave_mode=%d\n", nvram_get_daysave_mode());
  }
  m->m_buf = buf;
  m->m_arg = m->m_buflen = strlen(buf);
  m->m_nseg = 1;
  m->m_arg1 = 0;
  msg_reply(m->m_sender, m);
}


/*
 * nvram_wstat()
 *	Allow writing of supported stat messages
 */
void nvram_wstat(struct msg *m, struct file *f)
{
  char *field, *val;
  int sz = 0;

  /*
   * See if common handling code can do it
   */
  if (do_wstat(m, &nvram_prot, f->f_flags, &field, &val) == 0) {
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
      nvram_accgen = atoi(val);
    } else {
      nvram_accgen += 1;
    }
    f->f_gen = nvram_accgen;
  } else if (!strcmp(field, "size")) {
    /*
     * Are we addressing the "all" node?  If not reject the request
     */
    if (f->f_node != ALL_INO) {
      msg_err(m->m_sender, EINVAL);
      return;
    }

    /*
     * Set usable NVRAM size
     */
    if (val) {
      sz = atoi(val);
    }
    if ((sz < MIN_NVRAM_BYTES) || (sz > MAX_NVRAM_BYTES)) {
      msg_err(m->m_sender, EINVAL);
    }
    nvram_bytes = sz;
  } else {
    /*
     * Not a field we support...
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
