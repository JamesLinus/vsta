/*
 * camdata.c - CAM configuration information.
 */
#include "cam.h"

uint32	cam_debug_flags = 0;

long	sim154x_init(), sim154x_action();
CAM_SIM_ENTRY sim154x_entry = { sim154x_init, sim154x_action };

struct	cam_conftbl cam_conftbl[] = {
	{ &sim154x_entry },
};
long	cam_nconftbl_entries = sizeof(cam_conftbl) / sizeof(cam_conftbl[0]);

