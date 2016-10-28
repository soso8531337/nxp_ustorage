/*
 * @brief U-Storage Project
 *
 * @note
 * Copyright(C) i4season, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 *
 * @par
 * Software that is described herein is for illustrative purposes only
 * which provides customers with programming information regarding the
 * LPC products.  This software is supplied "AS IS" without any warranties of
 * any kind, and NXP Semiconductors and its licensor disclaim any and
 * all warranties, express or implied, including all implied warranties of
 * merchantability, fitness for a particular purpose and non-infringement of
 * intellectual property rights.  NXP Semiconductors assumes no responsibility
 * or liability for the use of the software, conveys no license or rights under any
 * patent, copyright, mask work right, or any other intellectual property rights in
 * or to any products. NXP Semiconductors reserves the right to make changes
 * in the software without notification. NXP Semiconductors also makes no
 * representation or warranty that such application will be suitable for the
 * specified use without further testing or modification.
 *
 * @par
 * Permission to use, copy, modify, and distribute this software and its
 * documentation is hereby granted, under NXP Semiconductors' and its
 * licensor's relevant copyrights in the software, without fee, provided that it
 * is used in conjunction with NXP Semiconductors microcontrollers.  This
 * copyright, permission, and disclaimer notice must appear in all copies of
 * this code.
 */

#include "MassStorageHost.h"
#include "fsusb_cfg.h"
#include "FreeRTOS.h"
#include "task.h"
#include "usUsb.h"
#include "usDisk.h"
#include "usProtocol.h"
/*****************************************************************************
 * Private types/enumerations/variables
 ****************************************************************************/
#if defined(DEBUG_ENABLE)
#define DEBUGOUT(...) do {printf("[Storage Mod]");printf(__VA_ARGS__);} while(0)
#else
#define DEBUGOUT(...)
#endif



/** LPCUSBlib Mass Storage Class driver interface configuration and state information. This structure is
 *  passed to all Mass Storage Class driver functions, so that multiple instances of the same class
 *  within a device can be differentiated from one another.
 */
 /** Use USB0 for Phone
 *    Use USB1 for Mass Storage
*/
static USB_ClassInfo_MS_Host_t UStorage_Interface[]	= {
	{
		.Config = {
			.DataINPipeNumber       = 1,
			.DataINPipeDoubleBank   = false,

			.DataOUTPipeNumber      = 2,
			.DataOUTPipeDoubleBank  = false,
			.PortNumber = 0,
		},
	},
	{
			.Config = {
			.DataINPipeNumber       = 1,
			.DataINPipeDoubleBank   = false,

			.DataOUTPipeNumber      = 2,
			.DataOUTPipeDoubleBank  = false,
			.PortNumber = 1,
		},
	},
	
};

/*****************************************************************************
 * Public types/enumerations/variables
 ****************************************************************************/
static void usSys_init(int port_num)
{
	if(port_num >= 2){
		die(1);
		return;
	}
#if (defined(CHIP_LPC43XX) || defined(CHIP_LPC18XX))
	if (port_num== 0){
		Chip_USB0_Init();
	} else {
		Chip_USB1_Init();
	}
#endif
	USB_Init(UStorage_Interface[port_num].Config.PortNumber, USB_MODE_Host);
}

/** Configures the board hardware and chip peripherals for the demo's functionality. */
static void SetupHardware(void)
{
#if (defined(CHIP_LPC43XX_NOSYSTEM))
	SystemCoreClockUpdate();
	Board_Init();
#endif
	usSys_init(0);
	usSys_init(1);
#if (defined(CHIP_LPC43XX_NOSYSTEM))
	/* Hardware Initialization */
	Board_Debug_Init();
#endif
}

/*****************************************************************************
 * Private functions
 ****************************************************************************/
static int usStorage_diskREAD(struct scsi_head *header)
{
	return 0;
}

static int usStorage_diskWRITE(const uint32_t handleSzie, struct scsi_head *header)
{
	return 0;
}

static int usStorage_diskLUN(struct scsi_head *header)
{
	uint8_t num = usDisk_DiskNum();
	uint8_t *buffer = NULL;
	uint32_t size = 0, total = 0;

	if(usProtocol_GetAvaiableBuffer(&buffer, &size)){
		DEBUGOUT("usProtocol_GetAvaiableBuffer Failed\r\n");
		return 1;
	}	
	DEBUGOUT("AvaiableBuffer 0x%p[%dBytes][Disk:%d]\r\n", 
				buffer, size, num);
	
	total = sizeof(struct scsi_head);
	memcpy(buffer, header, total);
	memcpy(buffer+total, &num, 1);
	total += 1;
	
	if(usProtocol_SendPackage(buffer, total)){
		DEBUGOUT("usProtocol_SendPackage Failed\r\n");
		return 1;
	}
	DEBUGOUT("usStorage_diskLUN Successful[DiskNumber %d]\r\n", num);
	return 0;
}

static int usStorage_Handle(void)
{	
	uint8_t *buffer;
	uint32_t size=0;
	struct scsi_head header;

	if(usProtocol_RecvPackage(&buffer, hsize, &size)){
		DEBUGOUT("usProtocol_RecvPackage Failed\r\n");
		return 1;
	}
	memcpy(&header, buffer, sizeof(header));
	DEBUGOUT("usProtocol_RecvPackage [%d/%d]Bytes\r\n", 
				header.len, size);
	/*Handle Package*/
	if(header.len+sizeof(header) == size){
		DEBUGOUT("usProtocol_RecvPackage Finish\r\n");
	}
	
	switch(header->ctrid){
		case SCSI_READ:
			usStorage_diskREAD(&header);
			break;
		case SCSI_WRITE:		
			usStorage_diskWRITE(sizeof(header), header);
			break;
		case SCSI_GET_LUN:
			usStorage_diskLUN(header);
			break;
		default:
			DEBUGOUT("Unhandle Command\r\n");
	}

	return 0;
}


/*****************************************************************************
 * Public functions
 ****************************************************************************/

/** Main program entry point. This routine configures the hardware required by the application, then
 *  calls the filesystem function to read files from USB Disk
 *Ustorage Project by Szitman 20161022
 */
void vs_main(void *pvParameters)
{
	SetupHardware();

	DEBUGOUT("U-Storage Running.\r\n");
	while(1){
		if (USB_HostState[0] != HOST_STATE_Configured) {
			USB_USBTask(0, USB_MODE_Host);
			continue;
		}
		/*Connect Phone Device*/
		if(usProtocol_ConnectPhone()){
			/*Connect to Phone Failed*/
			continue;
		}
		usStorage_Handle();
	}
}


/** Event handler for the USB_DeviceAttached event. This indicates that a device has been attached to the host, and
 *  starts the library USB task to begin the enumeration and USB management process.
 */
void EVENT_USB_Host_DeviceAttached(const uint8_t corenum)
{
	if(corenum == 1){
		DEBUGOUT(("Disk Attached on port %d\r\n"), corenum);	
	}else{
		DEBUGOUT(("Phone Attached on port %d\r\n"), corenum);
		
	}
}

/** Event handler for the USB_DeviceUnattached event. This indicates that a device has been removed from the host, and
 *  stops the library USB task management process.
 */
void EVENT_USB_Host_DeviceUnattached(const uint8_t corenum)
{
	DEBUGOUT(("\r\nDevice Unattached on port %d\r\n"), corenum);
	memset(&(UStorage_Interface[corenum].State), 0x00, sizeof(UStorage_Interface[corenum].State));
	if(corenum == 1){
		usDisk_DeviceDisConnect();
	}else{
		usProtocol_DeviceDisConnect();
	}
}

/** Event handler for the USB_DeviceEnumerationComplete event. This indicates that a device has been successfully
 *  enumerated by the host and is now ready to be used by the application.
 */
void EVENT_USB_Host_DeviceEnumerationComplete(const uint8_t corenum)
{
	if(corenum == 1){
		DEBUGOUT(("Disk Enumeration on port %d\r\n"), corenum);
		usDisk_DeviceDetect(&UStorage_Interface[corenum]);
	}else if(corenum == 0){
		DEBUGOUT(("Phone Enumeration on port %d\r\n"), corenum);
		usProtocol_DeviceDetect(&UStorage_Interface[corenum]);	
	}else{
		DEBUGOUT("Unknown USB Port %d.\r\n", corenum);
	}
}
/** Event handler for the USB_HostError event. This indicates that a hardware error occurred while in host mode. */
void EVENT_USB_Host_HostError(const uint8_t corenum, const uint8_t ErrorCode)
{
	USB_Disable(corenum, USB_MODE_Host);

	DEBUGOUT(("Host Mode Error\r\n"
			  " -- Error port %d\r\n"
			  " -- Error Code %d\r\n" ), corenum, ErrorCode);

	die(0);
}

/** Event handler for the USB_DeviceEnumerationFailed event. This indicates that a problem occurred while
 *  enumerating an attached USB device.
 */
void EVENT_USB_Host_DeviceEnumerationFailed(const uint8_t corenum,
											const uint8_t ErrorCode,
											const uint8_t SubErrorCode)
{
	DEBUGOUT(("Dev Enum Error\r\n"
			  " -- Error port %d\r\n"
			  " -- Error Code %d\r\n"
			  " -- Sub Error Code %d\r\n"
			  " -- In State %d\r\n" ),
			 corenum, ErrorCode, SubErrorCode, USB_HostState[corenum]);

}
