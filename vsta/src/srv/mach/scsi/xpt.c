/*
 * Xpt.c - the CAM XPT layer.
 */
#include <stdio.h>
#include <std.h>
#include <syslog.h>
#include "cam.h"

struct	cam_bus_entry *cam_edt;
int	cam_max_path_id = 0;

/*
 * Xpt_ccb_alloc() - allocate a CCB.
 */
CCB	*xpt_ccb_alloc(void)
{
	CCB	*ccb;

	if((ccb = cam_alloc_mem(sizeof(CCB), NULL, 0)) == NULL)
		return(NULL);
	bzero((void *)ccb, sizeof(CCB));
	return(ccb);
}

/*
 * Xpt_ccb_free() - free a CCB
 */
void	xpt_ccb_free(CCB *ccb)
{
	cam_free_mem((char *)ccb, 0);
}

/*
 * Xpt_action - dispatch to the appropriate SIM action function based on
 * the contents of the input CCB.
 */
long	xpt_action(CCB *ccb)
{
	struct	cam_bus_entry *edt;
	struct	cam_device_entry *device;
	struct	scsi_inq_data *inq_data;
	long	path_id, status;
	int	i;
	char	*myname = "xpt_action";

	CAM_DEBUG_FCN_ENTRY(myname);

	path_id = ccb->header.path_id;
	if((path_id < 0) || (path_id > cam_max_path_id)) {
		ccb->header.cam_status = CAM_PATH_INVALID;
		CAM_DEBUG_FCN_EXIT(myname, CAM_EINVAL);
		return(CAM_EINVAL);
	}

	status = CAM_SUCCESS;

	switch(ccb->header.fcn_code) {
	case XPT_GDEV_TYPE:
		edt = &cam_edt[path_id];
		device = edt->devices;
		for(i = 0; i < edt->ndevices; i++, device++) {
			if((ccb->header.target == device->target) &&
			   (ccb->header.lun == device->lun))
				break;
		}

		if(i < edt->ndevices) {
			ccb->header.cam_status = CAM_REQ_CMP;
			inq_data = (struct scsi_inq_data *)
				device->dev_edt.inq_data;
			ccb->getdev.inquiry_data = inq_data;
			ccb->getdev.dev_type = inq_data->pdev_type;
		} else
			ccb->header.cam_status = CAM_DEV_NOT_THERE;
		
		break;
	default:
		status = (*cam_edt[path_id].sim_entry->sim_action)(ccb);
	}

	CAM_DEBUG_FCN_EXIT(myname, status);
	return(status);
}

/*
 * Xpt_init - called by the peripheral driver to request that the XPT and 
 * sub-layers be initialized. Once the sub-layers are initialized any
 * subsequent calls by other peripheral drivers shall quickly return. 
 *
 * When the XPT is called it will update it's internal tables and then call
 * the sim_init(path_id) function pointed to by the CAM_SIM_ENTRY structure.
 * After the SIM has gone through the initialization process the XPT shall
 * scan the SCSI bus in order to update its internal tables containing Inquiry
 * information. 
 */
long	xpt_init(void)
{
	struct	cam_bus_entry *edt;
	struct	cam_device_entry *device;
	long	status;
	int	i, size, path_id, target, lun;
	CAM_DEV	devid;
	struct	scsi_inq_data inq_data;
	char	unsigned cam_status, scsi_status;
	char	idbuf[8 + 16 + 1 + 1];
	static	int init_done = 0;
	static	char *myname = "xpt_init";

	if(init_done)
		return(CAM_SUCCESS);

	CAM_DEBUG_FCN_ENTRY(myname);

	cam_max_path_id = 0;
	for(i = 0; i < cam_nconftbl_entries; i++) {
		status = (*cam_conftbl[i].sim_entry->sim_init)(cam_max_path_id);
		if(status != CAM_SUCCESS) {
			cam_error(0, myname, "error initializing SIM %d", i);
			continue;
		}
		path_id = cam_max_path_id++;
		size = cam_max_path_id * sizeof(struct cam_bus_entry);
		cam_edt = cam_alloc_mem(size, (void *)cam_edt, 0);
		if(cam_edt == NULL) {
			cam_error(0, myname, "EDT allocation error");
			return(CAM_ENOMEM);
			break;
		}

		cam_edt[path_id].sim_entry = cam_conftbl[i].sim_entry;
		cam_edt[path_id].devices = NULL;
		cam_edt[path_id].ndevices = 0;
	}

	edt = cam_edt;
	for(path_id = 0; path_id < cam_max_path_id; path_id++, edt++) {
	    for(target = 0; target < CAM_NTARGETS; target++) {
		for(lun = 0; lun < CAM_NLUNS; lun++) {
			devid = CAM_MKDEVID(path_id, target, lun, 0);
			if(cam_inquire(devid, &inq_data, &cam_status,
			               &scsi_status) != CAM_SUCCESS) {
				break;
			}
/*
 * If the target doesn't exist, go to the next target.
 * If the lun doesn't exist, go to the next LUN.
 */
			if(cam_status != CAM_REQ_CMP) {
				CAM_DEBUG2(CAM_DBG_DEVCONF, myname,
				           "device 0x%x cam_status = 0x%x",
				           devid, cam_status);
				break;
			}
			if(scsi_status != SCSI_GOOD) {
				CAM_DEBUG2(CAM_DBG_DEVCONF, myname,
				           "device 0x%x scsi_status = 0x%x",
				           devid, scsi_status);
				continue;
			}
/*
 * Target not capable of supporting a device on current LUN, go to next LUN.
 */
			if(*(char *)&inq_data == 0x7f) {
				CAM_DEBUG(CAM_DBG_DEVCONF, myname,
				          "device 0x%x inq_data[0] = 0x7f",
				          devid);
				continue;
			}
			size = (edt->ndevices + 1) *
			       sizeof(struct cam_device_entry);
			edt->devices = cam_alloc_mem(size, (void *)edt->devices,
			                             0);
			if(edt->devices == NULL) {
				cam_error(0, myname, "can't extend EDT table");
				continue;
			}
			device = &edt->devices[edt->ndevices++];
			device->target = target;
			device->lun = lun;
			memcpy(device->dev_edt.inq_data, (char *)&inq_data,
			       sizeof(inq_data));
/*
 * Print out the SCSI device information.
 */
			idbuf[0] = '\0';
			strncat(idbuf, (char *)inq_data.vendor_id,
			        sizeof(inq_data.vendor_id));
			strcat(idbuf, " ");
			strncat(idbuf, (char *)inq_data.prod_id,
			        sizeof(inq_data.prod_id));
			syslog(LOG_INFO, "SCSI device [%d/%d/%d] %s\n",
			       path_id, target, lun, idbuf);
		}
	    }
	}

	CAM_DEBUG_FCN_EXIT(myname, CAM_SUCCESS);
	return(CAM_SUCCESS);
}

