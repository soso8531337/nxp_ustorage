/*
 * @brief U-Storage Project
 *
 * @note
 * Copyright(C) i4season, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 */

#include "usProtocol.h"
#include "usUsb.h"


/*****************************************************************************
 * Private types/enumerations/variables
 ****************************************************************************/
struct accessory_t {
	uint32_t aoa_version;
	uint16_t vid;
	uint16_t pid;
	char *device;
	char *manufacturer;
	char *model;
	char *description;
	char *version;
	char *url;
	char *serial;
};
static struct accessory_t acc_default = {
	.manufacturer = "i4season",
	.model = "U-Storage",
	.description = "U-Storage",
	.version = "1.0",
	.url = "https://www.simicloud.com/download/index.html",
	.serial = "0000000012345678",
};


typedef struct {
	uint8_t State;		/*U-Storage Status*/
	uint8_t usType;	/*Android or IOS*/
	uint16_t VendorID; /**< Vendor ID for the USB product. */
	uint16_t ProductID; /**< Unique product ID for the USB product. */
	
}usStorage_info;

/*****************************************************************************
 * Public types/enumerations/variables
 ****************************************************************************/
/**********IOS Itunes***********/
#define VID_APPLE 0x5ac
#define PID_RANGE_LOW 0x1290
#define PID_RANGE_MAX 0x12af
/**********Android AOA***********/
/* Product IDs / Vendor IDs */
#define AOA_ACCESSORY_VID		0x18D1	/* Google */
#define AOA_ACCESSORY_PID		0x2D00	/* accessory */
#define AOA_ACCESSORY_ADB_PID		0x2D01	/* accessory + adb */
#define AOA_AUDIO_PID			0x2D02	/* audio */
#define AOA_AUDIO_ADB_PID		0x2D03	/* audio + adb */
#define AOA_ACCESSORY_AUDIO_PID		0x2D04	/* accessory + audio */
#define AOA_ACCESSORY_AUDIO_ADB_PID	0x2D05	/* accessory + audio + adb */
#define INTERFACE_CLASS_AOA 255 // Referrance http://www.usb.org/developers/defined_class/#BaseClassFFh
#define INTERFACE_SUBCLASS_AOA 255
/* Android Open Accessory protocol defines */
#define AOA_GET_PROTOCOL		51
#define AOA_SEND_IDENT			52
#define AOA_START_ACCESSORY		53
#define AOA_REGISTER_HID		54
#define AOA_UNREGISTER_HID		55
#define AOA_SET_HID_REPORT_DESC		56
#define AOA_SEND_HID_EVENT		57
#define AOA_AUDIO_SUPPORT		58
/* String IDs */
#define AOA_STRING_MAN_ID		0
#define AOA_STRING_MOD_ID		1
#define AOA_STRING_DSC_ID		2
#define AOA_STRING_VER_ID		3
#define AOA_STRING_URL_ID		4
#define AOA_STRING_SER_ID		5
/* Android Open Accessory protocol Filter Condition */
#define AOA_FTRANS_CLASS					0xff
#define AOA_FTRANS_SUBCLASS			0xff
#define AOA_FTRANS_PROTOCOL			0x00
/* IOS Itunes protocol Filter Condition */
#define IOS_FTRANS_CLASS					0xff
#define IOS_FTRANS_SUBCLASS			0xfe
#define IOS_FTRANS_PROTOCOL			0x02

enum USCONN_STATE {
	CONN_CONNECTING,	// SYN
	CONN_CONNECTED,		// SYN/SYNACK/ACK -> active
	CONN_REFUSED,		// RST received during SYN
	CONN_DYING,			// RST received
	CONN_DEAD			// being freed; used to prevent infinite recursion between client<->device freeing
};

enum mux_protocol {
	MUX_PROTO_VERSION = 0,
	MUX_PROTO_CONTROL = 1,
	MUX_PROTO_SETUP = 2,
	MUX_PROTO_TCP = IPPROTO_TCP,
};

enum{
	PROTOCOL_REOK=0,
	PROTOCOL_REPARA,
	PROTOCOL_REGEN
};

#if defined(DEBUG_ENABLE)
#define PRODEBUG(...) do {printf("[PRO Mod]");printf(__VA_ARGS__);} while(0)
#endif


struct version_header
{
	uint32_t major;
	uint32_t minor;
	uint32_t padding;
};

/*****************************************************************************
 * Private functions
 ****************************************************************************/
static uint8_t NXP_COMPFUNC_AOA_CLASS(void* const CurrentDescriptor)
{
	USB_Descriptor_Header_t* Header = DESCRIPTOR_PCAST(CurrentDescriptor, USB_Descriptor_Header_t);

	if (Header->Type == DTYPE_Interface){
		USB_Descriptor_Interface_t* Interface = DESCRIPTOR_PCAST(CurrentDescriptor, USB_Descriptor_Interface_t);

		if (Interface->Class  == AOA_FTRANS_CLASS){
			return DESCRIPTOR_SEARCH_Found;
		}
	}

	return DESCRIPTOR_SEARCH_NotFound;
}


static uint8_t NXP_SwitchAOAMode(void *cPrivate)
{
	nxp_aoa *aoa = (nxp_aoa *)cPrivate;
	USB_ClassInfo_MS_Host_t *MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)(aoa->InterfaceInfo);
	uint8_t version[2] = {0};
	uint16_t ConfigDescriptorSize = aoa->ConfigDescriptorSize;
	void* ConfigDescriptorData = aoa->ConfigDescriptorData;
	
	if(!aoa || ){
		PRODEBUG("Parameter Empty..\r\n");
		return PROTOCOL_REPARA;
	}
	memset(&MSInterfaceInfo->State, 0x00, sizeof(MSInterfaceInfo->State));

	if (DESCRIPTOR_TYPE(aoa->ConfigDescriptorData) != DTYPE_Configuration){
		return PROTOCOL_REGEN;
	}
	/*Found Interface Class */
	if (USB_GetNextDescriptorComp(&ConfigDescriptorSize, &ConfigDescriptorData,
				NXP_COMPFUNC_AOA_CLASS) != DESCRIPTOR_SEARCH_COMP_Found){		
		PRODEBUG("Attached Device Not a Valid AOA Device[NO 0xff Interface].\r\n");
		return PROTOCOL_REGEN;
	}
	/*Set AOA*/
	if(usUsb_SendControlRequest((void*)(&aoa->usbnum), REQDIR_DEVICETOHOST|REQTYPE_VENDOR, 
					AOA_GET_PROTOCOL, 0, 0, 2, version)){
		PRODEBUG("Get AOA Protocol Version Failed.\r\n");
		return PROTOCOL_REGEN;
	}
	acc_default.aoa_version = ((version[1] << 8) | version[0]);
	PRODEBUG("Found Device supports AOA %d.0!\r\n", acc_default.aoa_version);
	/* In case of a no_app accessory, the version must be >= 2 */
	if((acc_default.aoa_version < 2) && !acc_default.manufacturer) {
		PRODEBUG("Connecting without an Android App only for AOA 2.0.\r\n");
		return PROTOCOL_REGEN;
	}
	if(acc_default.manufacturer) {
		PRODEBUG("sending manufacturer: %s\r\n", acc_default.manufacturer);
		if(usUsb_SendControlRequest((void*)(&aoa->usbnum), REQDIR_HOSTTODEVICE|REQTYPE_VENDOR,
						AOA_SEND_IDENT, 0, AOA_STRING_MAN_ID, 
					strlen(acc_default.manufacturer) + 1, (uint8_t *)acc_default.manufacturer)){
			PRODEBUG("Get AOA Protocol Version Failed.\r\n");
			return PROTOCOL_REGEN;
		}
	}
	if(acc_default.model) {
		PRODEBUG("sending model: %s\r\n", acc_default.model);
		if(usUsb_SendControlRequest((void*)(&aoa->usbnum), REQDIR_HOSTTODEVICE|REQTYPE_VENDOR,
								AOA_SEND_IDENT, 0, AOA_STRING_MOD_ID, 
							strlen(acc_default.model) + 1, (uint8_t *)acc_default.model)){
			PRODEBUG("Could not Set AOA model.\r\n");
			return PROTOCOL_REGEN;
		}
	}

	PRODEBUG("sending description: %s\r\n", acc_default.description);
	if(usUsb_SendControlRequest((void*)(&aoa->usbnum), REQDIR_HOSTTODEVICE|REQTYPE_VENDOR,
						AOA_SEND_IDENT, 0, AOA_STRING_DSC_ID, 
						strlen(acc_default.description) + 1, (uint8_t *)acc_default.description)){
		PRODEBUG("Could not Set AOA description.\r\n");
		return PROTOCOL_REGEN;
	}

	PRODEBUG("sending version string: %s\r\n", acc_default.version);
	if(usUsb_SendControlRequest((void*)(&aoa->usbnum), REQDIR_HOSTTODEVICE|REQTYPE_VENDOR,
						AOA_SEND_IDENT, 0, AOA_STRING_VER_ID, 
						strlen(acc_default.version) + 1, (uint8_t *)acc_default.version)){
		PRODEBUG("Could not Set AOA version.\r\n");
		return PROTOCOL_REGEN;
	}
	PRODEBUG("sending url string: %s\r\n", acc_default.url);
	if(usUsb_SendControlRequest((void*)(&aoa->usbnum), REQDIR_HOSTTODEVICE|REQTYPE_VENDOR,
						AOA_SEND_IDENT, 0, AOA_STRING_URL_ID, 
						strlen(acc_default.url) + 1, (uint8_t *)acc_default.url)){
		PRODEBUG("Could not Set AOA url.\r\n");
		return PROTOCOL_REGEN;
	}
	PRODEBUG("sending serial number: %s\r\n", acc_default.serial);
	if(usUsb_SendControlRequest((void*)(&aoa->usbnum), REQDIR_HOSTTODEVICE|REQTYPE_VENDOR,
						AOA_SEND_IDENT, 0, AOA_STRING_SER_ID, 
						strlen(acc_default.serial) + 1, (uint8_t *)acc_default.serial)){
		PRODEBUG("Could not Set AOA serial.\r\n");
		return PROTOCOL_REGEN;
	}

	if(usUsb_SendControlRequest((void*)(&aoa->usbnum), REQDIR_HOSTTODEVICE|REQTYPE_VENDOR,
						AOA_START_ACCESSORY, 0, 0, 0, NULL)){
		PRODEBUG("Could not Start AOA.\r\n");
		return PROTOCOL_REGEN;
	}

	PRODEBUG("Start AOA Successful  Android Will Reconnect\r\n");
	return PROTOCOL_REOK;
}

/*****************************************************************************
 * Public functions
 ****************************************************************************/
uint8_t usProtocol_SwitchAOAMode(void *cPrivate)
{
	return NXP_SwitchAOAMode(cPrivate);
}

uint8_t usProtocol_ConnectIOSPhone(void *cPrivate)
{
	return NXP_SwitchAOAMode(cPrivate);
}

uint8_t usProtocol_DeviceDetect()
{

}















