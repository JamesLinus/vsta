/*
 * Sim.c - device independent part of the CAM SIM layer.
 */
#include "cam.h"

/*
 * sim_sched()
 *	Schedule the next I/O on the input bus.
 */
int	sim_sched(bus_info)
struct	sim_bus *bus_info;
{
	struct	sim_target *target_info;
	CCB	*ccb;
	long	status;
	int	unsigned target, i;
	static	char *myname = "sim_sched";

	if(bus_info->phase != SCSI_BUS_FREE) {
		CAM_DEBUG(CAM_DBG_MSG, myname, "bus not free", 0);
		return(CAM_SUCCESS);
	}
	target = bus_info->last_target + 1;
	target_info = &bus_info->target_info[target];
	for(i = 0; i < CAM_NTARGETS; i++, target++, target_info++) {
/*
 * Need to wrap around?
 */
		if(target > CAM_MAX_TARGET) {
			target = 0;
			target_info = bus_info->target_info;
		}
/*
 * Start I/O on the first inactive target that has a non-empty CCB queue.
 */
		if((target_info->nactive == 0) &&
		   (!CAM_EMPTYQUE(&target_info->head))) {
			target_info->nactive++;
			ccb = ((struct cam_simq *)
				target_info->head.q_forw)->ccb;
			bus_info->last_target = target;
			status = (*bus_info->start)(ccb);
			return(status);
		}
	}
	bus_info->last_target = target;

	if(i >= CAM_NTARGETS) {
		CAM_DEBUG(CAM_DBG_MSG, myname, "all queues empty", 0);
	}

	return(CAM_SUCCESS);
}

/*
 * sim_complete()
 *	Per-target completion.
 */
void	sim_complete(bus_info, ccb)
struct	sim_bus *bus_info;
CCB	*ccb;
{
	struct	sim_target *target_info;
	int	iodone_flag;
	char	*myname = "sim_complete";

	target_info = &bus_info->target_info[ccb->header.target];
/*
 * Remove the CCB from the SIMQ. Decrement the active count.
 */
	CAM_REMQUE(&ccb->scsiio.simq.head);
	target_info->nactive--;
/*
 * Start the next I/O for the current CCB, if necessary.
 */
	iodone_flag = TRUE;
	if((ccb->header.fcn_code == XPT_SCSI_IO) &&
	   (ccb->header.cam_status == CAM_REQ_CMP))
		if(cam_cntuio(ccb) == CAM_SUCCESS)
			if(xpt_action(ccb) == CAM_SUCCESS)
				iodone_flag = FALSE;
/*
 * Call the peripheral driver completion function, if necessary.
 */
	if(iodone_flag && (ccb->scsiio.completion != NULL))
		(*ccb->scsiio.completion)(ccb);
/*
 * Schedule the next I/O in the current bus.
 */
	if(sim_sched(bus_info) != CAM_SUCCESS)
		cam_error(0, myname, "reschedule error");
}

/*
 * sim_action()
 *	Initiate I/O on the input CCB.
 */
long	sim_action(ccb, bus_info)
CCB	*ccb;
struct	sim_bus *bus_info;
{
	struct	sim_target *target_info;
	int	status = CAM_SUCCESS;
	char	*myname = "sim_action";

	CAM_DEBUG_FCN_ENTRY(myname);

	switch(ccb->header.fcn_code) {
	case XPT_SCSI_IO:
		target_info = &bus_info->target_info[ccb->header.target];
		ccb->scsiio.simq.ccb = ccb;
		if(ccb->header.cam_flags & CAM_SIM_QHEAD)
			CAM_INSQUE(&ccb->scsiio.simq.head,
			           target_info->head.q_forw);
		else
			CAM_INSQUE(&ccb->scsiio.simq.head,
			           target_info->head.q_back);
		status = sim_sched(bus_info);
		break;
	case XPT_NOOP:
	case XPT_GDEV_TYPE:
	case XPT_PATH_INQ:
	case XPT_REL_SIMQ:
	case XPT_SASYNC_CB:
		cam_error(0, myname, "unimplemented operation");
		status = CAM_EINVAL;
		break;
	default:
		cam_error(0, myname, "unknown operation");
		status = CAM_EINVAL;
		break;
	}

	CAM_DEBUG_FCN_EXIT(myname, status);
	return(status);
}

/*
 * sim_get_active_ccb - get the current active for the input target on
 * the input bus.
 */
CCB	*sim_get_active_ccb(bus_info, target, tag)
struct	sim_bus *bus_info;
int	target, tag;
{
	struct	sim_target *target_info;
/*
 * Get the CCB.
 */
	target_info = &bus_info->target_info[target];
	return(((struct cam_simq *)target_info->head.q_forw)->ccb);
}
