/* 
 * mouse.h
 *    Data and function prototypes for a fairly universal mouse driver.
 *
 * Copyright (C) 1993 by G.T.Nicol, all rights reserved.
 */
#ifndef __VSTA_MOUSE_H__
#define __VSTA_MOUSE_H__

#include <sys/types.h>
#include <sys/param.h>
#include <sys/perm.h>
#include <sys/fs.h>

#include "nec_bus.h"
#include "ms_bus.h"
#include "logi_bus.h"
#include "ibmrs232.h"
#include "ps2aux.h"

/*
 * Mouse button definitions
 */
#define MOUSE_LEFT_BUTTON    (1 << 0)
#define MOUSE_RIGHT_BUTTON   (1 << 1)
#define MOUSE_MIDDLE_BUTTON  (1 << 2)

/*
 * An open file
 */
struct file {
	uint f_gen;   /* Generation of access */
	uint f_flags; /* User access bits */
};

/*
 * Device specific mouse handling functions 
 */
typedef struct {
	void (*mouse_poller_entry_point)(void);
	void (*mouse_interrupt)(void);
	void (*mouse_update)(ushort);
} mouse_function_table;

/*
 * Pointer related mouse data
 */
typedef struct {
	uchar buttons;			/* current button state */
	ushort x;			/* current X coordinate */
	ushort y;			/* current Y coordinate */
	ushort bx1, by1, bx2, by2;	/* bounding box for the mouse */
} mouse_pointer_data_t;

/*
 * Overall mouse data
 */
typedef struct {
	mouse_function_table functions;
	mouse_pointer_data_t pointer_data;
	int irq_number;
	int update_frequency;
	char enable_interrupts;
	int type_id;
} mouse_data_t;

extern struct prot mouse_prot;
extern port_t mouse_port;
extern port_name mouse_name;
extern uint mouse_accgen;
extern mouse_data_t mouse_data;
extern int oldx, oldy;

extern void mouse_initialise(int, char **);
extern void mouse_read(struct msg *, struct file *);
extern void mouse_stat(struct msg *, struct file *);
extern void mouse_wstat(struct msg *, struct file *);
extern void mouse_changed(void);
extern void update_changes(void);

#endif /* __VSTA_MOUSE_H__ */
