/*
 * camdata.c - CAM configuration information.
 */
#include "cam.h"
#include "sim.h"

uint32	cam_debug_flags = 0;

/*
 * Static link to SIM HBA drivers.
 */
#ifdef	__SIM154X__
void	sim154x_dvrinit(void);
#endif

struct	sim_driver sim_dvrtbl[] = {
#ifdef	__SIM154X__
	{ sim154x_dvrinit,	0,	NULL },
#endif
};
int	sim_ndrivers = sizeof(sim_dvrtbl) / sizeof(sim_dvrtbl[0]);

/*
 * The per-driver CAM configuration table.
 */
CAM_SIM_ENTRY **cam_conftbl = NULL;
int	cam_nconftbl_entries = 0;

