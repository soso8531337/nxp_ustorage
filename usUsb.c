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
#define USBDEBUG(...)
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
	uint8_t portnum = 0, curConfigurations = 1;
	uint16_t ConfigDescriptorSize = 0;
	uint8_t  ConfigDescriptorData[512], *PtrConfigDescriptorData = NULL;
	

	if(!cPrivate){
		return USB_REPARA;
	}
	USB_ClassInfo_MS_Host_t *MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)(usbdev->os_priv);
	if( !MSInterfaceInfo||
			!cPrivate->callbackEndpoint || !cPrivate->callbackInterface){
		return USB_REPARA;
	}

	for(curConfigurations= 1; curConfigurations <= cPrivate->bNumConfigurations; curConfigurations++){
		if (USB_Host_GetDeviceConfigDescriptor(MSInterfaceInfo->Config.PortNumber, 
								curConfigurations, &ConfigDescriptorSize, ConfigDescriptorData,
								sizeof(ConfigDescriptorData)) != HOST_GETCONFIG_Successful) {
			USBDEBUG("Error Retrieving Configuration Descriptor.\r\n");
			continue;
		}
		if (DESCRIPTOR_TYPE(ConfigDescriptorData) != DTYPE_Configuration){
			continue;
		}		
		usUsb_Print(ConfigDescriptorData, ConfigDescriptorSize); 
		/*Set array name to point var, USB_GetNextDescriptorComp will change point*/
		PtrConfigDescriptorData = ConfigDescriptorData;		
		while (!(DataINEndpoint) || !(DataOUTEndpoint)){
			if (!(MassStorageInterface) ||
					USB_GetNextDescriptorComp(&ConfigDescriptorSize, (void **)&PtrConfigDescriptorData,
							cPrivate->callbackEndpoint) != DESCRIPTOR_SEARCH_COMP_Found){
				if (USB_GetNextDescriptorComp(&ConfigDescriptorSize, (void **)&PtrConfigDescriptorData,
							cPrivate->callbackInterface) != DESCRIPTOR_SEARCH_COMP_Found){
					DataINEndpoint	= NULL;
					DataOUTEndpoint = NULL;
					MassStorageInterface = NULL;					
					USBDEBUG("No Found Vaild Interface At Configuration[%d].\r\n", curConfigurations);
					break;
				}
				MassStorageInterface = DESCRIPTOR_PCAST(PtrConfigDescriptorData, USB_Descriptor_Interface_t);

				DataINEndpoint  = NULL;
				DataOUTEndpoint = NULL;

				continue;
			}

			USB_Descriptor_Endpoint_t* EndpointData = DESCRIPTOR_PCAST(PtrConfigDescriptorData, USB_Descriptor_Endpoint_t);

			if ((EndpointData->EndpointAddress & ENDPOINT_DIR_MASK) == ENDPOINT_DIR_IN){
				DataINEndpoint  = EndpointData;				
				USBDEBUG("Found Vaild INInterface At Configuration[%d].\r\n", curConfigurations);
			}else{
				DataOUTEndpoint = EndpointData;				
				USBDEBUG("Found Vaild OUTInterface At Configuration[%d].\r\n", curConfigurations);
			}			
		}
		if(DataINEndpoint && DataOUTEndpoint){
			break;
		}
	}
	if(!(DataINEndpoint) || !(DataOUTEndpoint) ||
			(curConfigurations > cPrivate->bNumConfigurations)){
		USBDEBUG("No Found Suitable Interface.\r\n");
		return USB_REGEN;
	}
	memset(&MSInterfaceInfo->State, 0x00, sizeof(MSInterfaceInfo->State));	
	portnum = MSInterfaceInfo->Config.PortNumber;
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

	/*Set Configuration*/
	if(NXP_SetDeviceConfigDescriptor(usbdev, curConfigurations)){
		USBDEBUG("Error Setting Device Configuration.\r\n");
		return USB_REGEN;
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

uint8_t NXP_DiskReadSectors(usb_device *usbdev, 
				void *buff, uint32_t secStart, uint32_t numSec, uint16_t BlockSize)
{
	int ret;
	
	ret = MS_Host_ReadDeviceBlocks((USB_ClassInfo_MS_Host_t*)usbdev->os_priv, 0, 
						secStart, numSec, BlockSize, buff);
	if(ret) {
		USBDEBUG("Error reading device block. ret = %d\r\n", ret);
		USB_Host_SetDeviceConfiguration(usbdev->device_address, 0);
		return USB_REGEN;
	}
	return USB_REOK;
}

uint8_t NXP_DiskWriteSectors(usb_device *usbdev, 
					void *buff, uint32_t secStart, uint32_t numSec, uint16_t BlockSize)
{
	int ret;
	ret = MS_Host_WriteDeviceBlocks((USB_ClassInfo_MS_Host_t*)usbdev->os_priv, 0, 
						secStart, numSec, BlockSize, buff);
	if(ret) {
		USBDEBUG("Error writing device block. ret = %d\r\n", ret);
		return USB_REGEN;
	}
	return USB_REOK;
}
uint8_t NXP_GetMaxLUN(usb_device *usbdev, uint8_t *LunIndex)
{
	USB_ClassInfo_MS_Host_t *MSInterfaceInfo;
	/*os_priv is USB_ClassInfo_MS_Host_t *MSInterfaceInfo*/
	if(!usbdev || !LunIndex){
		return USB_REPARA;
	}
	MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)(usbdev->os_priv);

	if (MS_Host_GetMaxLUN(MSInterfaceInfo, LunIndex)) {
		USB_Host_SetDeviceConfiguration(usbdev->device_address, 0);
		return USB_REGEN;
	}

	return USB_REOK;
}

uint8_t NXP_ResetMSInterface(usb_device *usbdev)
{
	USB_ClassInfo_MS_Host_t *MSInterfaceInfo;
	/*os_priv is USB_ClassInfo_MS_Host_t *MSInterfaceInfo*/
	if(!usbdev){
		return USB_REPARA;
	}
	
	MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)(usbdev->os_priv);

	if (MS_Host_ResetMSInterface(MSInterfaceInfo)) {
		USBDEBUG("Error resetting Mass Storage interface.\r\n");
		USB_Host_SetDeviceConfiguration(usbdev->device_address, 0);
		return USB_REGEN;
	}
	
	return USB_REOK;	
}

uint8_t NXP_RequestSense(usb_device *usbdev, 
				uint8_t index, SCSI_Sense_Response_t *SenseData)
{
	USB_ClassInfo_MS_Host_t *MSInterfaceInfo;
	/*os_priv is USB_ClassInfo_MS_Host_t *MSInterfaceInfo*/
	if(!usbdev || !SenseData){
		return USB_REPARA;
	}
	
	MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)(usbdev->os_priv);

	if (MS_Host_RequestSense(MSInterfaceInfo, index, 
					(SCSI_Request_Sense_Response_t*)SenseData) != 0) {
		USBDEBUG("Error retrieving device sense.\r\n");
		USB_Host_SetDeviceConfiguration(usbdev->device_address, 0);
		return USB_REGEN;
	}
	
	return USB_REOK;	
}

uint8_t NXP_GetInquiryData(usb_device *usbdev,
						uint8_t index, SCSI_Inquiry_t *InquiryData)
{
	USB_ClassInfo_MS_Host_t *MSInterfaceInfo;
	/*os_priv is USB_ClassInfo_MS_Host_t *MSInterfaceInfo*/
	if(!usbdev || !InquiryData){
		return USB_REPARA;
	}
	
	MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)(usbdev->os_priv);

	if (MS_Host_GetInquiryData(MSInterfaceInfo, index, (SCSI_Inquiry_Response_t*)InquiryData) != 0) {
		USBDEBUG("Error retrieving device Inquiry.\r\n");
		USB_Host_SetDeviceConfiguration(usbdev->device_address, 0);
		return USB_REGEN;
	}
	
	return USB_REOK;
}

uint8_t NXP_ReadDeviceCapacity(usb_device *usbdev, uint32_t *Blocks, uint32_t *BlockSize)
{
	SCSI_Capacity_t DiskCapacity;
	USB_ClassInfo_MS_Host_t *MSInterfaceInfo;
	/*os_priv is USB_ClassInfo_MS_Host_t *MSInterfaceInfo*/
	if(!usbdev || !Blocks|| !BlockSize){
		return USB_REPARA;
	}
	
	MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)(usbdev->os_priv);
	for (;; ) {
		uint8_t ErrorCode = MS_Host_TestUnitReady(MSInterfaceInfo, 0);
		if (!(ErrorCode)) {
			break;
		}
		/* Check if an error other than a logical command error (device busy) received */
		if (ErrorCode != MS_ERROR_LOGICAL_CMD_FAILED) {
			USBDEBUG("Failed\r\n");
			USB_Host_SetDeviceConfiguration(usbdev->device_address, 0);
			return USB_REGEN;
		}
	}
	if (MS_Host_ReadDeviceCapacity(MSInterfaceInfo, 0, &DiskCapacity)) {
		USBDEBUG("Error retrieving device capacity.\r\n");
		USB_Host_SetDeviceConfiguration(usbdev->device_address, 0);
		return USB_REGEN;
	}
	*Blocks = DiskCapacity.Blocks;
	*BlockSize = DiskCapacity.BlockSize;
	
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
		return USB_REPARA;
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

uint8_t usUsb_GetDeviceDescriptor(usb_device *usbdev, USB_StdDesDevice_t *DeviceDescriptorData)
{
#ifdef NXP_CHIP_18XX
	return NXP_GetDeviceDescriptor(usbdev, DeviceDescriptorData);
#endif
}

uint8_t usUsb_GetDeviceConfigDescriptor(usb_device *usbdev, uint8_t index, uint16_t *cfgsize,
					uint8_t *ConfigDescriptorData, uint16_t ConfigDescriptorDataLen)
{
#ifdef NXP_CHIP_18XX
	return NXP_GetDeviceConfigDescriptor(usbdev, index, cfgsize, 
						ConfigDescriptorData, ConfigDescriptorDataLen);
#endif
}

uint8_t usUsb_SetDeviceConfigDescriptor(usb_device *usbdev, uint8_t cfgindex)
{
#ifdef NXP_CHIP_18XX
	return NXP_SetDeviceConfigDescriptor(usbdev, cfgindex);
#endif
}

uint8_t usUsb_ClaimInterface(usb_device *usbdev, void *cPrivate)
{
#ifdef NXP_CHIP_18XX
	return NXP_ClaimInterface(usbdev, (nxp_clminface*)cPrivate);
#endif
}

uint8_t usUsb_GetMaxLUN(usb_device *usbdev, uint8_t *LunIndex)
{
#ifdef NXP_CHIP_18XX
	return NXP_GetMaxLUN(usbdev, LunIndex);
#endif
}

uint8_t usUsb_ResetMSInterface(usb_device *usbdev)
{
#ifdef NXP_CHIP_18XX
		return NXP_ResetMSInterface(usbdev);
#endif
}

uint8_t usUsb_RequestSense(usb_device *usbdev,
						uint8_t index, SCSI_Sense_Response_t *SenseData)
{
#ifdef NXP_CHIP_18XX
		return NXP_RequestSense(usbdev, index, SenseData);
#endif
}

uint8_t usUsb_GetInquiryData(usb_device *usbdev,
						uint8_t index, SCSI_Inquiry_t *InquiryData)
{
#ifdef NXP_CHIP_18XX
	return NXP_GetInquiryData(usbdev, index, InquiryData);
#endif
}

uint8_t usUsb_ReadDeviceCapacity(usb_device *usbdev, uint32_t *Blocks, uint32_t *BlockSize)
{
#ifdef NXP_CHIP_18XX
	return NXP_ReadDeviceCapacity(usbdev, Blocks, BlockSize);
#endif
}






uint8_t usUsb_Init(usb_device *usbdev, void *os_priv)
{
#ifdef NXP_CHIP_18XX
	return NXP_Init(usbdev, os_priv);
#else
	return USB_REOK;
#endif
}

uint8_t usUsb_DiskReadSectors(usb_device *usbdev, 
				void *buff, uint32_t secStart, uint32_t numSec, uint16_t BlockSize)
{
#ifdef NXP_CHIP_18XX
		return NXP_DiskReadSectors(usbdev, buff, secStart, numSec, BlockSize);
#endif
}

uint8_t usUsb_DiskWriteSectors(usb_device *usbdev, 
				void *buff, uint32_t secStart, uint32_t numSec, uint16_t BlockSize)
{
#ifdef NXP_CHIP_18XX
		return NXP_DiskWriteSectors(usbdev, buff, secStart, numSec, BlockSize);
#endif
}

void usUsb_Print(uint8_t *buffer, int length)
{
	int cur = 0;
	
	if(!buffer){
		return;
	}

	for(; cur< length; cur++){
		if(cur % 16 == 0){
			printf("\r\n");
		}
		printf("0x%02x ", buffer[cur]);

	}
	
	printf("\r\n");
}


