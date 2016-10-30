/*
 * @brief U-Storage Project
 *
 * @note
 * Copyright(C) i4season, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 */

#include <stdint.h>
#include "usDisk.h"
#include "usUsb.h"
#include "MassStorageHost.h"


#define MSC_FTRANS_CLASS				0x08
#define MSC_FTRANS_SUBCLASS			0x06
#define MSC_FTRANS_PROTOCOL			0x50

#if defined(DEBUG_ENABLE)
#define DSKDEBUG(...) do {printf("[DISK Mod]");printf(__VA_ARGS__);} while(0)
#else
#define DSKDEBUG(...)
#endif

enum{
	DISK_REOK=0,
	DISK_REPARA,
	DISK_REGEN,
	DISK_REINVAILD
};

typedef struct {
	uint8_t disknum;
	uint32_t Blocks; /**< Number of blocks in the addressed LUN of the device. */
	uint32_t BlockSize; /**< Number of bytes in each block in the addressed LUN. */
	usb_device diskdev;
}usDisk_info;

usDisk_info uDinfo;

#if defined(NXP_CHIP_18XX)
extern uint8_t DCOMP_MS_Host_NextMSInterfaceEndpoint(void* const CurrentDescriptor);
/*****************************************************************************
 * Private functions
 ****************************************************************************/

static uint8_t NXP_COMPFUNC_MSC_CLASS(void* const CurrentDescriptor)
{
	USB_Descriptor_Header_t* Header = DESCRIPTOR_PCAST(CurrentDescriptor, USB_Descriptor_Header_t);

	if (Header->Type == DTYPE_Interface){
		USB_Descriptor_Interface_t* Interface = DESCRIPTOR_PCAST(CurrentDescriptor, USB_Descriptor_Interface_t);
		if (Interface->Class  == MSC_FTRANS_CLASS&&
				(Interface->SubClass == MSC_FTRANS_SUBCLASS) &&
		   		(Interface->Protocol ==MSC_FTRANS_PROTOCOL)){
			return DESCRIPTOR_SEARCH_Found;
		}
	}

	return DESCRIPTOR_SEARCH_NotFound;
}
#endif

uint8_t usDisk_DeviceDetect(void *os_priv)
{	
	USB_StdDesDevice_t DeviceDescriptorData;
	uint8_t MaxLUNIndex;
	usb_device *usbdev = &(uDinfo.diskdev);
	
	memset(&uDinfo, 0, sizeof(uDinfo));
	/*set os_priv*/
	usUsb_Init(usbdev, os_priv);
	/*GEt device description*/
	memset(&DeviceDescriptorData, 0, sizeof(USB_StdDesDevice_t));
	if(usUsb_GetDeviceDescriptor(usbdev, &DeviceDescriptorData)){
		DSKDEBUG("usUusb_GetDeviceDescriptor Failed\r\n");
		return DISK_REGEN;
	}
#if defined(NXP_CHIP_18XX)	
	/*Set callback*/	
	nxp_clminface nxpcall;	
	nxpcall.callbackInterface = NXP_COMPFUNC_MSC_CLASS;
	nxpcall.callbackEndpoint= DCOMP_MS_Host_NextMSInterfaceEndpoint;
	/*Claim Interface*/
	nxpcall.bNumConfigurations = DeviceDescriptorData.bNumConfigurations;
	if(usUsb_ClaimInterface(usbdev, &nxpcall)){
		DSKDEBUG("Attached Device Not a Valid DiskDevice.\r\n");
		return DISK_REINVAILD;
	}
#elif defined(LINUX)
	if(usUsb_ClaimInterface(usbdev, NULL)){
		DSKDEBUG("Attached Device Not a Valid DiskDevice.\r\n");
		return DISK_REINVAILD;
	}
#endif

	if(usUsb_GetMaxLUN(usbdev, &MaxLUNIndex)){		
		DSKDEBUG("Get LUN Failed\r\n");
		return DISK_REINVAILD;
	}
	DSKDEBUG(("Total LUNs: %d - Using first LUN in device.\r\n"), (MaxLUNIndex + 1));
	if(usUsb_ResetMSInterface(usbdev)){		
		DSKDEBUG("ResetMSInterface Failed\r\n");
		return DISK_REINVAILD;
	}	
	SCSI_Sense_Response_t SenseData;
	if(usUsb_RequestSense(usbdev, 0, &SenseData)){
		DSKDEBUG("RequestSense Failed\r\n");
		return DISK_REINVAILD;
	}

	SCSI_Inquiry_t InquiryData;
	if(usUsb_GetInquiryData(usbdev, 0, &InquiryData)){
		DSKDEBUG("GetInquiryData Failed\r\n");
		return DISK_REINVAILD;
	}

	if(usUsb_ReadDeviceCapacity(usbdev, &uDinfo.Blocks, &uDinfo.BlockSize)){
		DSKDEBUG("ReadDeviceCapacity Failed\r\n");
		return DISK_REINVAILD;
	}
	DSKDEBUG("Mass Storage Device Enumerated.\r\n");
	uDinfo.disknum=1;
	
	return DISK_REGEN;
}

uint8_t usDisk_DeviceDisConnect(void)
{
	memset(&uDinfo, 0, sizeof(uDinfo));
	return DISK_REOK;
}
uint8_t usDisk_DiskReadSectors(void *buff, uint32_t secStart, uint32_t numSec)
{
	if(!buff || !uDinfo.disknum){
		DSKDEBUG("DiskReadSectors Failed[DiskNum:%d].\r\n", uDinfo.disknum);
		return DISK_REPARA;
	}
	if(usUsb_DiskReadSectors(&uDinfo.diskdev, 
			buff, secStart,numSec, uDinfo.BlockSize)){
		DSKDEBUG("DiskReadSectors Failed[DiskNum:%d secStart:%d numSec:%d].\r\n", 
						uDinfo.disknum, secStart, numSec);
		return DISK_REGEN;
	}
	
	DSKDEBUG("DiskReadSectors Successful[DiskNum:%d secStart:%d numSec:%d].\r\n", 
					uDinfo.disknum, secStart, numSec);
	return DISK_REOK;
}

uint8_t usDisk_DiskWriteSectors(void *buff, uint32_t secStart, uint32_t numSec)
{
	if(!buff || !uDinfo.disknum){
		DSKDEBUG("DiskWriteSectors Failed[DiskNum:%d].\r\n", uDinfo.disknum);
		return DISK_REPARA;
	}
	
	if(usUsb_DiskWriteSectors(&uDinfo.diskdev, 
				buff, secStart,numSec, uDinfo.BlockSize)){
		DSKDEBUG("DiskWriteSectors Failed[DiskNum:%d secStart:%d numSec:%d].\r\n", 
						uDinfo.disknum, secStart, numSec);
		return DISK_REGEN;
	}
	
	DSKDEBUG("DiskWriteSectors Successful[DiskNum:%d secStart:%d numSec:%d].\r\n", 
					uDinfo.disknum, secStart, numSec);
	return DISK_REOK;	
}

uint8_t usDisk_DiskNum(void)
{
	return uDinfo.disknum;
}
