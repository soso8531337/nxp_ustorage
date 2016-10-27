/*
 * @note
 * Copyright(C) i4season U-Storage, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 *
 */
 
#include <ctype.h>
#include <stdio.h>
#include "usUsb.h"
#include "usSys.h"

#ifdef NXP_CHIP_18XX
#include "MassStorageHost.h"
#include "fsusb_cfg.h"
#endif

enum{
	USB_REOK=0,
	USB_REPARA,
	USB_REGEN
};

#if defined(DEBUG_ENABLE)
#define USBDEBUG(...) do {printf("[USB Mod]");printf(__VA_ARGS__);} while(0)
#else
#define PRODEBUG(...)
#endif

#define USB_TRUE		1
#define USB_FALSE		0


/***********************CHIP RELETIVE USB****************************/
/*****************************************************************************
 * Private functions
 ****************************************************************************/
#ifdef NXP_CHIP_18XX
/*Special USB Command*/
static uint8_t NXP_SendControlRequest(const uint8_t corenum, 
			uint8_t bmRequestType, uint8_t bRequest, 
			uint16_t wValue, uint16_t wIndex, uint16_t wLength, void * const data)
{
	USB_ControlRequest = (USB_Request_Header_t)
		{
		.bmRequestType = (bmRequestType),
		.bRequest      = bRequest,
		.wValue        = wValue,
		.wIndex        = wIndex,
		.wLength       = wLength,
		};

	Pipe_SelectPipe(corenum, PIPE_CONTROLPIPE);

	return USB_Host_SendControlRequest(corenum,data);
}
static uint8_t NXP_BlukPacketReceive(usb_device *usbdev, uint8_t *buffer, uint32_t length)
{
	uint8_t  ErrorCode = PIPE_RWSTREAM_NoError;
	uint8_t ErrorCallback = 0;
	const USB_ClassInfo_MS_Host_t *MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)usbdev->os_priv;

	if(!usbdev || !buffer){
		return USB_REPARA;
	}
	Pipe_SelectPipe(MSInterfaceInfo->Config.PortNumber, 
			MSInterfaceInfo->Config.DataINPipeNumber);
	
	ErrorCode = Pipe_Streaming(MSInterfaceInfo->Config.PortNumber,buffer, length, 
				usbdev->wMaxPacketSize);
	if(ErrorCode == PIPE_RWSTREAM_IncompleteTransfer){
		USBDEBUG("PiPe Streaming Not Ready[%d]...\r\n", PIPE_RWSTREAM_IncompleteTransfer);			
	}else{
		USBDEBUG("PiPe Streaming Read[%dBytes]...\r\n", 
		PipeInfo[MSInterfaceInfo->Config.PortNumber][pipeselected[MSInterfaceInfo->Config.PortNumber]].ByteTransfered);
	}
	while(((ErrorCallback = MSInterfaceInfo->State.IsActive) == USB_TRUE)
			&& !Pipe_IsStatusOK(MSInterfaceInfo->Config.PortNumber));
	
	Pipe_ClearIN(MSInterfaceInfo->Config.PortNumber);
	if(ErrorCallback != USB_TRUE){
		USBDEBUG("CallBack Excute Error Something Happen...\r\n");
		return USB_REGEN;
	}

	return USB_REOK;
}	

static uint8_t NXP_BlukPacketSend(usb_device *usbdev, uint8_t *buffer, uint32_t length)
{
	uint8_t  ErrorCode = PIPE_RWSTREAM_NoError;
	const USB_ClassInfo_MS_Host_t *MSInterfaceInfo;

	if(!usbdev || !buffer){
		return USB_REPARA;
	}
	MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)usbdev->os_priv;
	Pipe_SelectPipe(MSInterfaceInfo->Config.PortNumber, 
				MSInterfaceInfo->Config.DataOUTPipeNumber);
	Pipe_Unfreeze();

	if ((ErrorCode = Pipe_Write_Stream_LE(MSInterfaceInfo->Config.PortNumber, 
					buffer, length,  NULL)) != PIPE_RWSTREAM_NoError){
		return ((ErrorCode == PIPE_RWSTREAM_NoError)?USB_REOK:USB_REGEN);
	}
	
	Pipe_ClearOUT(MSInterfaceInfo->Config.PortNumber);
	Pipe_WaitUntilReady(MSInterfaceInfo->Config.PortNumber);
	Pipe_Freeze();

	return USB_REOK;
}	

static uint8_t NXP_GetDeviceDescriptor(usb_device *usbdev, USB_StdDesDevice_t *DeviceDescriptorData)
{	
	if(!DeviceDescriptorData || !usbdev){
		return USB_REPARA;
	}
	if(USB_Host_GetDeviceDescriptor(usbdev->device_address, (USB_Descriptor_Device_t*)DeviceDescriptorData)){
		USBDEBUG("Error Getting Device Descriptor.\r\n");
		return USB_REGEN;
	}
	return USB_REOK;
}

static uint8_t NXP_GetDeviceConfigDescriptor(usb_device *usbdev, uint8_t index, uint16_t *cfgsize,
					uint8_t *ConfigDescriptorData, uint16_t ConfigDescriptorDataLen)
{	
	if(!ConfigDescriptorData || !usbdev || !cfgsize){
		return USB_REPARA;
	}
	if (USB_Host_GetDeviceConfigDescriptor(usbdev->device_address, index, cfgsize, 
				ConfigDescriptorData, ConfigDescriptorDataLen) != HOST_GETCONFIG_Successful) {
		USBDEBUG("Error Retrieving Configuration Descriptor.\r\n");
		return USB_REGEN;
	}

	return USB_REOK;
}

static uint8_t NXP_SetDeviceConfigDescriptor(usb_device *usbdev, uint8_t cfgindex)
{	
	if(!usbdev){
		return USB_REPARA;
	}
	if (USB_Host_SetDeviceConfiguration(usbdev->device_address, cfgindex) != 
				HOST_SENDCONTROL_Successful) {
		USBDEBUG("Error Setting Device Configuration.\r\n");
		return USB_REGEN;
	}
	
	return USB_REOK;
}

static uint8_t NXP_ClaimInterface(usb_device *usbdev, nxp_clminface*cPrivate)
{
	USB_Descriptor_Endpoint_t*  DataINEndpoint       = NULL;
	USB_Descriptor_Endpoint_t*  DataOUTEndpoint      = NULL;
	USB_Descriptor_Interface_t* MassStorageInterface = NULL;
	uint8_t portnum = 0;
	uint16_t ConfigDescriptorSize = 0;
	uint8_t  ConfigDescriptorData[512];
	

	if(!cPrivate){
		return USB_REPARA;
	}
	USB_ClassInfo_MS_Host_t *MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)(usbdev->os_priv);
	if( !MSInterfaceInfo||
			!cPrivate->callbackEndpoint || !cPrivate->callbackInterface){
		return USB_REPARA;
	}
	
	if (USB_Host_GetDeviceConfigDescriptor(MSInterfaceInfo->Config.PortNumber, 
							1, &ConfigDescriptorSize, ConfigDescriptorData,
							sizeof(ConfigDescriptorData)) != HOST_GETCONFIG_Successful) {
		USBDEBUG("Error Retrieving Configuration Descriptor.\r\n");
		return USB_REGEN;
	}


	portnum = MSInterfaceInfo->Config.PortNumber;
	memset(&MSInterfaceInfo->State, 0x00, sizeof(MSInterfaceInfo->State));

	if (DESCRIPTOR_TYPE(ConfigDescriptorData) != DTYPE_Configuration){
		return USB_REGEN;
	}

	while (!(DataINEndpoint) || !(DataOUTEndpoint)){
		if (!(MassStorageInterface) ||
				USB_GetNextDescriptorComp(&ConfigDescriptorSize, (void **)&ConfigDescriptorData,
						cPrivate->callbackEndpoint) != DESCRIPTOR_SEARCH_COMP_Found){
			if (USB_GetNextDescriptorComp(&ConfigDescriptorSize, (void **)&ConfigDescriptorData,
						cPrivate->callbackInterface) != DESCRIPTOR_SEARCH_COMP_Found){
				return USB_REGEN;
			}

			MassStorageInterface = DESCRIPTOR_PCAST(ConfigDescriptorData, USB_Descriptor_Interface_t);

			DataINEndpoint  = NULL;
			DataOUTEndpoint = NULL;

			continue;
		}

		USB_Descriptor_Endpoint_t* EndpointData = DESCRIPTOR_PCAST(ConfigDescriptorData, USB_Descriptor_Endpoint_t);

		if ((EndpointData->EndpointAddress & ENDPOINT_DIR_MASK) == ENDPOINT_DIR_IN){
			DataINEndpoint  = EndpointData;
		}else{
			DataOUTEndpoint = EndpointData;
		}
	}

	for (uint8_t PipeNum = 1; PipeNum < PIPE_TOTAL_PIPES; PipeNum++){
		uint16_t Size;
		uint8_t  Type;
		uint8_t  Token;
		uint8_t  EndpointAddress;
		bool     DoubleBanked;

		if (PipeNum == MSInterfaceInfo->Config.DataINPipeNumber){
			Size            = le16_to_cpu(DataINEndpoint->EndpointSize);
			EndpointAddress = DataINEndpoint->EndpointAddress;
			Token           = PIPE_TOKEN_IN;
			Type            = EP_TYPE_BULK;
			DoubleBanked    = MSInterfaceInfo->Config.DataINPipeDoubleBank;

			MSInterfaceInfo->State.DataINPipeSize = DataINEndpoint->EndpointSize;
			
		}else if (PipeNum == MSInterfaceInfo->Config.DataOUTPipeNumber){
			Size            = le16_to_cpu(DataOUTEndpoint->EndpointSize);
			EndpointAddress = DataOUTEndpoint->EndpointAddress;
			Token           = PIPE_TOKEN_OUT;
			Type            = EP_TYPE_BULK;
			DoubleBanked    = MSInterfaceInfo->Config.DataOUTPipeDoubleBank;
			
			MSInterfaceInfo->State.DataOUTPipeSize = DataOUTEndpoint->EndpointSize;
		}else{
			continue;
		}

		if (!(Pipe_ConfigurePipe(portnum,PipeNum, Type, Token, EndpointAddress, Size,
		                         DoubleBanked ? PIPE_BANK_DOUBLE : PIPE_BANK_SINGLE))){
			return USB_REGEN;
		}
	}

	MSInterfaceInfo->State.InterfaceNumber = MassStorageInterface->InterfaceNumber;
	MSInterfaceInfo->State.IsActive = true;
	/*Set the usbdev struct*/
	usbdev->device_address= MSInterfaceInfo->Config.PortNumber;
	usbdev->wMaxPacketSize = MSInterfaceInfo->State.DataOUTPipeSize;
	usbdev->ep_in = MSInterfaceInfo->Config.DataINPipeNumber;
	usbdev->ep_out = MSInterfaceInfo->Config.DataOUTPipeNumber;
	return USB_REOK;
}
static uint8_t NXP_Init(usb_device *usbdev, void *os_priv)
{
	USB_ClassInfo_MS_Host_t *MSInterfaceInfo;
	/*os_priv is USB_ClassInfo_MS_Host_t *MSInterfaceInfo*/
	if(!usbdev || !os_priv){
		return USB_REPARA;
	}
	usbdev->os_priv = os_priv;	
	MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)(os_priv);

	/*Just set device_address == PortNumber*/
	usbdev->device_address  = MSInterfaceInfo->Config.PortNumber;

	return USB_REOK;
}





#endif

/*****************************************************************************
 * Public functions
 ****************************************************************************/

/************************PUBLIC USB INTERFACE***************************/
/*Special USB Command*/
/*
*cPrivate is about chip vendor, is specially
*return Value:
*0: Successful
*>0: Failed
*/
uint8_t usUsb_SendControlRequest(usb_device *usbdev, 
			uint8_t bmRequestType, uint8_t bRequest, 
			uint16_t wValue, uint16_t wIndex, uint16_t wLength,  void * data)
{
#ifdef NXP_CHIP_18XX
	if(!usbdev){
		return 1;
	}
	return NXP_SendControlRequest(usbdev->device_address, bmRequestType,
				bRequest, wValue, wIndex, wLength, data);
#endif
}

uint8_t usUsb_BlukPacketSend(usb_device *usbdev, uint8_t *buffer, const uint32_t length)
{
#ifdef NXP_CHIP_18XX
	return NXP_BlukPacketSend(usbdev, buffer, length);
#endif
}

uint8_t usUsb_BlukPacketReceive(usb_device *usbdev, uint8_t *buffer, uint32_t length)
{
#ifdef NXP_CHIP_18XX
	return NXP_BlukPacketReceive(usbdev, buffer, length);
#endif
}

uint8_t usUusb_GetDeviceDescriptor(usb_device *usbdev, USB_StdDesDevice_t *DeviceDescriptorData)
{
#ifdef NXP_CHIP_18XX
	return NXP_GetDeviceDescriptor(usbdev, DeviceDescriptorData);
#endif
}

uint8_t usUusb_GetDeviceConfigDescriptor(usb_device *usbdev, uint8_t index, uint16_t *cfgsize,
					uint8_t *ConfigDescriptorData, uint16_t ConfigDescriptorDataLen)
{
#ifdef NXP_CHIP_18XX
	return NXP_GetDeviceConfigDescriptor(usbdev, index, cfgsize, 
						ConfigDescriptorData, ConfigDescriptorDataLen);
#endif
}

uint8_t usUusb_SetDeviceConfigDescriptor(usb_device *usbdev, uint8_t cfgindex)
{
#ifdef NXP_CHIP_18XX
	return NXP_SetDeviceConfigDescriptor(usbdev, cfgindex);
#endif
}

uint8_t usUusb_ClaimInterface(usb_device *usbdev, void *cPrivate)
{
#ifdef NXP_CHIP_18XX
	return NXP_ClaimInterface(usbdev, (nxp_clminface*)cPrivate);
#endif
}

uint8_t usUusb_Init(usb_device *usbdev, void *os_priv)
{
#ifdef NXP_CHIP_18XX
	return NXP_Init(usbdev, os_priv);
#else
	return USB_REOK;
#endif
}

