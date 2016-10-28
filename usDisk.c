#include <stdint.h>
#include "usDisk.h"
#include "usUsb.h"


#define MSC_FTRANS_CLASS				0x08
#define MSC_FTRANS_SUBCLASS			0x06
#define MSC_FTRANS_PROTOCOL			0x50



typedef struct {
	uint32_t Blocks; /**< Number of blocks in the addressed LUN of the device. */
	uint32_t BlockSize; /**< Number of bytes in each block in the addressed LUN. */
	usb_device diskdev;
}usDisk_info;

usDisk_info uDinfo;

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

uint8_t usDisk_DeviceDetect(void *os_priv)
{	
	USB_StdDesDevice_t DeviceDescriptorData;
	nxp_clminface nxpcall;	
	uint8_t MaxLUNIndex;
	usb_device *usbdev = &(uDinfo.diskdev);
	
	memset(&uDinfo, 0, sizeof(uDinfo));
	/*set os_priv*/
	usUsb_Init(usbdev, os_priv);
	/*GEt device description*/
	memset(&DeviceDescriptorData, 0, sizeof(USB_StdDesDevice_t));
	if(usUsb_GetDeviceDescriptor(usbdev, &DeviceDescriptorData)){
		PRODEBUG("usUusb_GetDeviceDescriptor Failed\r\n");
		return PROTOCOL_REGEN;
	}
	/*Set callback*/
	nxpcall.callbackInterface = NXP_COMPFUNC_MSC_CLASS;
	nxpcall.callbackEndpoint= DCOMP_MS_Host_NextMSInterfaceEndpoint;
	/*Claim Interface*/
	nxpcall.bNumConfigurations = DeviceDescriptorData.bNumConfigurations;
	if(usUsb_ClaimInterface(usbdev, &nxpcall)){
		PRODEBUG("Attached Device Not a Valid DiskDevice.\r\n");
		return PROTOCOL_REINVAILD;
	}

	if(usUsb_GetMaxLUN(usbdev, &MaxLUNIndex)){		
		PRODEBUG("Get LUN Failed\r\n");
		return PROTOCOL_REINVAILD;
	}
	PRODEBUG(("Total LUNs: %d - Using first LUN in device.\r\n"), (MaxLUNIndex + 1));
	if(usUsb_ResetMSInterface(usbdev)){		
		PRODEBUG("ResetMSInterface Failed\r\n");
		return PROTOCOL_REINVAILD;
	}	
	SCSI_Sense_Response_t SenseData;
	if(usUsb_RequestSense(usbdev, 0, &SenseData)){
		PRODEBUG("RequestSense Failed\r\n");
		return PROTOCOL_REINVAILD;
	}

	SCSI_Inquiry_t InquiryData;
	if(usUsb_GetInquiryData(usbdev, 0, &InquiryData)){
		PRODEBUG("GetInquiryData Failed\r\n");
		return PROTOCOL_REINVAILD;
	}

	if(usUsb_ReadDeviceCapacity(usbdev, &uDinfo.Blocks, &uDinfo.BlockSize)){
		PRODEBUG("ReadDeviceCapacity Failed\r\n");
		return PROTOCOL_REINVAILD;
	}
	PRODEBUG("Mass Storage Device Enumerated.\r\n");
	
	return PROTOCOL_REGEN;
}



