/*
 * Filename:	dir.c
 * Author:	Dave Hudson <dave@humbug.demon.co.uk>
 * Started:	9th May 1994
 * Last Update: 10th May 1994
 * Implemented:	GNU GCC 1.42 (VSTa v1.3.1)
 *
 * Description: Deals with directory manipulations
 */
 
 
#include <stdio.h>
#include <sys/fs.h>
#include <stdlib.h>
#include "nvram.h"


/*
 * Structure of all "file" names supported by the server
 */
struct fentries fe[MAX_INO] = {
  {"nvram", MAX_INO - 1},
  {"all", 0},
  {"rtc", 8},
  {"cfg_fd0", 1},
  {"cfg_fd1", 1},
  {"cfg_wd0", 1},
  {"cfg_wd1", 1}
};


/*
 * nvram_readdir()
 *	Fill in buffer with list of supported names
 */
void nvram_readdir(struct msg *m, struct file *f)
{
  int x;
  char *buf;

  /*
   * Check for an EOF
   */
  if (f->f_pos >= (MAX_INO - ALL_INO)) {
    m->m_arg = m->m_arg1 = m->m_nseg = 0;
    msg_reply(m->m_sender, m);
  }

  /*
   * Get a buffer
   */
  buf = malloc(16 * MAX_INO + 1);
  if (buf == NULL) {
    msg_err(m->m_sender, ENOMEM);
    return;
  }
  buf[0] = '\0';

  /*
   * Run through our "file" list, adding them to the dir list buffer
   */
  for (x = ALL_INO; x < MAX_INO; x++) {
    strcat(buf, fe[x].fe_name);
    strcat(buf, "\n");
  }

  /*
   * Send results
   */
  m->m_buf = buf;
  m->m_arg = m->m_buflen = strlen(buf);
  m->m_nseg = 1;
  m->m_arg1 = 0;
  msg_reply(m->m_sender, m);
  free(buf);
  f->f_pos += (MAX_INO - ALL_INO);
}


/*
 * nvram_open()
 *	Move from root dir down into the "files"
 */
void nvram_open(struct msg *m, struct file *f)
{
  uint x;
  char *p = m->m_buf;

  /*
   * Can only move from root to a node
   */
  if (f->f_node != ROOT_INO) {
    msg_err(m->m_sender, EINVAL);
    return;
  }

  /*
   * Scan names for a match
   */
  for (x = ALL_INO; x < MAX_INO; x++) {
    if (!strcmp(fe[x].fe_name, p)) {
      f->f_node = x;
      m->m_nseg = m->m_arg = m->m_arg1 = 0;
      msg_reply(m->m_sender, m);
      return;
    }
   }
   msg_err(m->m_sender, ESRCH);
}
