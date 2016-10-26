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
			uint16_t wValue, uint16_t wIndex, uint16_t wLength, const void *data)
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
static uint8_t NXP_BlukPacketSend(nxp_bluk *cPrivate, uint8_t *buffer, uint32_t length)
{
	uint8_t  ErrorCode = PIPE_RWSTREAM_NoError;
	uint8_t ErrorCallback = 0;

	if(!cPrivate || !buffer){
		return USB_REPARA;
	}
	Pipe_SelectPipe(cPrivate->usbnum, cPrivate->endnum);
	
	ErrorCode = Pipe_Streaming(cPrivate->usbnum,buffer, length, 
				cPrivate->packetsize);
	if(ErrorCode == PIPE_RWSTREAM_IncompleteTransfer){
		USBDEBUG("PiPe Streaming Not Ready[%d]...\r\n", PIPE_RWSTREAM_IncompleteTransfer);			
	}else{
		USBDEBUG("PiPe Streaming Read[%dBytes]...\r\n", 
		PipeInfo[cPrivate->usbnum][pipeselected[cPrivate->endnum]].ByteTransfered);
	}
	while((!cPrivate->callback ||(ErrorCallback = cPrivate->callback(cPrivate->data)) == USB_TRUE)
			&& !Pipe_IsStatusOK(cPrivate->usbnum));
	
	Pipe_ClearIN(cPrivate->usbnum);
	if(cPrivate->callback && ErrorCallback != USB_TRUE){
		USBDEBUG("CallBack Excute Error Something Happen...\r\n");
		return USB_REGEN;
	}

	return USB_REOK;
}	

static uint8_t NXP_BlukPacketReceive(nxp_bluk *cPrivate, uint8_t *buffer, uint32_t length)
{
	uint8_t  ErrorCode = PIPE_RWSTREAM_NoError;
	uint8_t ErrorCallback = 0;

	if(!cPrivate || !buffer){
		return USB_REPARA;
	}
	
	Pipe_SelectPipe(cPrivate->usbnum, cPrivate->endnum);
	Pipe_Unfreeze();

	if ((ErrorCode = Pipe_Write_Stream_LE(cPrivate->usbnum,buffer, length,
	                                      NULL)) != PIPE_RWSTREAM_NoError){
		return ((ErrorCode == PIPE_RWSTREAM_NoError)?USB_REOK:USB_REGEN);
	}
	
	Pipe_ClearOUT(cPrivate->usbnum);
	Pipe_WaitUntilReady(cPrivate->usbnum);
	Pipe_Freeze();
	if(cPrivate->callback && 
			cPrivate->callback(cPrivate->data) == USB_FALSE){
		USBDEBUG("CallBack Excute Error Something Happen...\r\n");
		return USB_REGEN;
	}

	return USB_REOK;
}	

static uint8_t NXP_GetDeviceDescriptor(const uint8_t corenum, USB_StdDesDevice_t *DeviceDescriptorData)
{	
	if(!DeviceDescriptorData){
		return USB_REPARA;
	}
	if(USB_Host_GetDeviceDescriptor(corenum, (USB_Descriptor_Device_t*)DeviceDescriptorData)){
		USBDEBUG("Error Getting Device Descriptor.\r\n");
		return USB_REGEN;
	}
	return USB_REOK;
}

static uint8_t NXP_GetDeviceConfigDescriptor(nxp_desconfig *cPrivate, 
					uint8_t *ConfigDescriptorData, uint16_t ConfigDescriptorDataLen)
{	
	if(!ConfigDescriptorData || !cPrivate){
		return USB_REPARA;
	}
	if (USB_Host_GetDeviceConfigDescriptor(cPrivate->usbnum, cPrivate->cfgindex, &cPrivate->cfgnum, 
				ConfigDescriptorData, ConfigDescriptorDataLen) != HOST_GETCONFIG_Successful) {
		USBDEBUG("Error Retrieving Configuration Descriptor.\r\n");
		return USB_REGEN;
	}
	
	return USB_REOK;
}

static uint8_t NXP_SetDeviceConfigDescriptor(nxp_desconfig *cPrivate)
{	
	if(!cPrivate){
		return USB_REPARA;
	}
	if (USB_Host_SetDeviceConfiguration(cPrivate->usbnum, cPrivate->cfgindex) != 
				HOST_SENDCONTROL_Successful) {
		USBDEBUG("Error Setting Device Configuration.\r\n");
		return USB_REGEN;
	}
	
	return USB_REOK;
}

static uint8_t NXP_ClaimInterface(nxp_clminface *cPrivate)
{
	USB_Descriptor_Endpoint_t*  DataINEndpoint       = NULL;
	USB_Descriptor_Endpoint_t*  DataOUTEndpoint      = NULL;
	USB_Descriptor_Interface_t* MassStorageInterface = NULL;
	uint8_t portnum = 0;

	if(!cPrivate){
		return USB_REPARA;
	}
	uint16_t ConfigDescriptorSize = cPrivate->ConfigDescriptorSize;
	void* ConfigDescriptorData = cPrivate->ConfigDescriptorData;	
	USB_ClassInfo_MS_Host_t *MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)(cPrivate->InterfaceInfo);

	if(!ConfigDescriptorData || !MSInterfaceInfo||
			!cPrivate->callbackEndpoint || !cPrivate->callbackInterface){
		return USB_REPARA;
	}
	portnum = MSInterfaceInfo->Config.PortNumber;
	memset(&MSInterfaceInfo->State, 0x00, sizeof(MSInterfaceInfo->State));

	if (DESCRIPTOR_TYPE(ConfigDescriptorData) != DTYPE_Configuration){
		return USB_REGEN;
	}

	while (!(DataINEndpoint) || !(DataOUTEndpoint)){
		if (!(MassStorageInterface) ||
				USB_GetNextDescriptorComp(&ConfigDescriptorSize, &ConfigDescriptorData,
						cPrivate->callbackEndpoint) != DESCRIPTOR_SEARCH_COMP_Found){
			if (USB_GetNextDescriptorComp(&ConfigDescriptorSize, &ConfigDescriptorData,
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
uint8_t usUsb_SendControlRequest(void *cPrivate, 
			uint8_t bmRequestType, uint8_t bRequest, 
			uint16_t wValue, uint16_t wIndex, uint16_t wLength, const void *data)
{
#ifdef NXP_CHIP_18XX
	if(!cPrivate){
		return 1;
	}
	return NXP_SendControlRequest(*((uint8_t*)cPrivate), bmRequestType,
				bRequest, wValue, wIndex, wLength, data);
#endif
}

uint8_t usUsb_BlukPacketSend(void *cPrivate, uint8_t *buffer, const uint32_t length)
{
#ifdef NXP_CHIP_18XX
	return NXP_BlukPacketSend((nxp_bluk *)cPrivate, buffer, length);
#endif
}

uint8_t usUsb_BlukPacketReceive(void *cPrivate, uint8_t *buffer, uint32_t length)
{
#ifdef NXP_CHIP_18XX
	return NXP_BlukPacketReceive((nxp_bluk *)cPrivate, buffer, length);
#endif
}

uint8_t usUusb_GetDeviceDescriptor(void *cPrivate, USB_StdDesDevice_t *DeviceDescriptorData)
{
#ifdef NXP_CHIP_18XX
	return NXP_GetDeviceDescriptor(*((uint8_t*)cPrivate), DeviceDescriptorData);
#endif
}

uint8_t usUusb_GetDeviceConfigDescriptor(void *cPrivate, 
							uint8_t *ConfigDescriptorData, uint16_t ConfigDescriptorDataLen)
{
#ifdef NXP_CHIP_18XX
	return NXP_GetDeviceConfigDescriptor((nxp_desconfig*)cPrivate, 
						ConfigDescriptorData, ConfigDescriptorDataLen);
#endif
}

uint8_t usUusb_SetDeviceConfigDescriptor(void *cPrivate)
{
#ifdef NXP_CHIP_18XX
	return NXP_SetDeviceConfigDescriptor((nxp_desconfig*)cPrivate);
#endif
}

uint8_t usUusb_ClaimInterface(void *cPrivate)
{
#ifdef NXP_CHIP_18XX
	return NXP_ClaimInterface((nxp_clminface*)cPrivate);
#endif
}


