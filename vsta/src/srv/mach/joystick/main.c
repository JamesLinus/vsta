/*
 * Filename:	main.c
 * Author:	Dave Hudson <dave@humbug.demon.co.uk>
 * Started:	5th January 1994
 * Last Update: 21st March 1994
 * Implemented:	GNU GCC version 2.5.7
 *
 * Description: Main message handling for the game port/joystick device.  Also
 *		handles the initialisation of the device server and namer
 *		registration
 */

 
#include <fdl.h>
#include <hash.h>
#include <stdlib.h>
#include <syslog.h>
#include <mach/pit.h>
#include <sys/assert.h>
#include <sys/fs.h>
#include <sys/namer.h>
#include <sys/perm.h>
#include <sys/ports.h>
#include <sys/syscall.h>
#include "joystick.h"


static struct hash *filehash;	/* Map session->context structure */

port_t js_port;			/* Port we receive contacts through */
port_name js_name;		/* And it's name */
uint js_accgen = 0;		/* Generation counter for access */
uchar js_mask = 0;		/* Channel availability mask */
int js_channels = 0;		/* Number of channels masked in */

struct prot js_prot = {		/* Protection for the joystick starts */
  1,				/* as access for all.  Sys can change */
  ACC_READ | ACC_WRITE,		/* this however */
  {1},
  {ACC_CHMOD}
};


/*
 * js_new_client()
 *	Create new per-connect structure
 */
static void js_new_client(struct msg *m)
{
  struct file *f;
  struct perm *perms;
  int uperms, nperms, desired;

  /*
   * See if they're OK to access
   */
  perms = (struct perm *)m->m_buf;
  nperms = (m->m_buflen) / sizeof(struct perm);
  uperms = perm_calc(perms, nperms, &js_prot);
  desired = m->m_arg & (ACC_WRITE | ACC_READ | ACC_CHMOD);
  if ((uperms & desired) != desired) {
    msg_err(m->m_sender, EPERM);
    return;
  }

  /*
   * Get data structure
   */
  if ((f = malloc(sizeof(struct file))) == 0) {
    msg_err(m->m_sender, strerror());
    return;
  }

  /*
   * Fill in fields.
   */
  f->f_gen = js_accgen;
  f->f_flags = uperms;

  /*
   * Hash under the sender's handle
   */
  if (hash_insert(filehash, m->m_sender, f)) {
    free(f);
    msg_err(m->m_sender, ENOMEM);
    return;
  }

  /*
   * Return acceptance
   */
  msg_accept(m->m_sender);
}


/*
 * js_dup_client()
 *	Duplicate current file access onto new session
 */
static void js_dup_client(struct msg *m, struct file *fold)
{
  struct file *f;

  /*
   * Get data structure
   */
  if ((f = malloc(sizeof(struct file))) == 0) {
    msg_err(m->m_sender, strerror());
    return;
  }

  /*
   * Fill in fields.  Simply duplicate old file.
   */
  *f = *fold;

  /*
   * Hash under the sender's handle
   */
  if (hash_insert(filehash, m->m_arg, f)) {
    free(f);
    msg_err(m->m_sender, ENOMEM);
    return;
  }

  /*
   * Return acceptance
   */
  m->m_arg = m->m_arg1 = m->m_buflen = m->m_nseg = 0;
  msg_reply(m->m_sender, m);
}


/*
 * js_dead_client()
 *	Someone has gone away.  Free their info.
 */
static void js_dead_client(struct msg *m, struct file *f)
{
  (void)hash_delete(filehash, m->m_sender);
  free(f);
}


/*
 * js_check_gen()
 *	Check access generation
 */
static int js_check_gen(struct msg *m, struct file *f)
{
  if (f->f_gen != js_accgen) {
    msg_err(m->m_sender, EIO);
    return 1;
  }
  return 0;
}


/*
 * js_main()
 *	Endless loop to receive and serve requests
 */
static void js_main(void)
{
  struct msg msg;
  int x;
  struct file *f;

loop:
  /*
   * Receive a message, log an error and then keep going
   */
  x = msg_receive(js_port, &msg);
  if (x < 0) {
    syslog(LOG_ERR, "joystick: msg_receive");
    goto loop;
  }

  /*
   * All incoming data should fit in one buffer
   */
  if (msg.m_nseg > 1) {
    msg_err(msg.m_sender, EINVAL);
    goto loop;
  }

  /*
   * Categorize by basic message operation
   */
  f = hash_lookup(filehash, msg.m_sender);
  switch (msg.m_op) {
  case M_CONNECT :		/* New client */
    js_new_client(&msg);
    break;

  case M_DISCONNECT :		/* Client done */
    js_dead_client(&msg, f);
    break;

  case M_DUP :			/* File handle dup during exec() */
    js_dup_client(&msg, f);
    break;

  case M_ABORT :		/* Aborted operation */
    /*
     * We're synchronous, so presumably the operation is
     * done and this abort is old news
     */
    msg_reply(msg.m_sender, &msg);
    break;

  case FS_READ :		/* Read file */
    if (js_check_gen(&msg, f)) {
      break;
    }
    js_read(&msg, f);
    break;

  case FS_STAT :		/* Get stat of file */
    if (js_check_gen(&msg, f)) {
      break;
    }
    js_stat(&msg, f);
    break;

  case FS_WSTAT :		/* Writes stats */
    if (js_check_gen(&msg, f)) {
      break;
    }
    js_wstat(&msg, f);
    break;

  default :			/* Unknown */
    msg_err(msg.m_sender, EINVAL);
    break;
  }

  goto loop;
}


/*
 * main()
 *	Startup of the joystick server
 */
void main(void)
{
  /*
   * Allocate handle->file hash table.  16 is just a guess
   * as to what we'll have to handle.
   */
  filehash = hash_alloc(16);
  if (filehash == 0) {
    syslog(LOG_ERR, "joystick: file hash");
    exit(1);
  }

  /*
   * Enable I/O for the joystick driver port
   */
  if (enable_io(JS_DATA, JS_DATA) < 0) {
    syslog(LOG_ERR, "joystick: unable to get I/O permissions");
    exit(1);
  }

  /*
   * Enable I/O for the high resolution timer
   */
  if (enable_io(PIT_CH0, PIT_CTRL) < 0) {
    syslog(LOG_ERR, "joystick: unable to get timer I/O permissions");
    exit(1);
  }

  /*
   * Init our data structures and check that there are any joysticks
   * to support
   */
  js_init();

  /*
   * Get a port for the joystick server
   */
  js_port = msg_port((port_name)0, &js_name);

  /*
   * Register the device name with the namer
   */
  if (namer_register("srv/joystick", js_name) < 0) {
    syslog(LOG_ERR, "joystick: can't register name 'srv/joystick'\n");
    exit(1);
  }

  /*
   * Start serving requests for the filesystem
   */
  js_main();
}
