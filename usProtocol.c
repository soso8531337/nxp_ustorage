/*
 * @brief U-Storage Project
 *
 * @note
 * Copyright(C) i4season, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 */
#include <string.h>
#include "usProtocol.h"
#include "usUsb.h"
#include "MassStorageHost.h"


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
/*TCP */
#define TH_FIN        0x01
#define TH_SYN        0x02
#define TH_RST        0x04
#define TH_PUSH       0x08
#define TH_ACK        0x10
#define TH_URG        0x20

/*IOS Default PORT*/
#define IOS_DEFAULT_PORT			5555
#define IOS_DEFAULT_WIN			(8*1024)
#define USB_MTU	IOS_DEFAULT_WIN
#ifndef IPPROTO_TCP
#define IPPROTO_TCP 		6
#endif

enum mux_conn_state {
	CONN_INIT,		//init
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
	PROTOCOL_REGEN,
	PROTOCOL_REINVAILD
};
enum{
	PRO_IOS,
	PRO_ANDROID
};

#if defined(DEBUG_ENABLE)
#define PRODEBUG(...) do {printf("[PRO Mod]");printf(__VA_ARGS__);} while(0)
#else
#define PRODEBUG(...)
#endif
#define min(x, y)				(((x) < (y)) ? (x) : (y))

struct tcphdr{
	uint16_t th_sport;         /* source port */
	uint16_t th_dport;         /* destination port */
	uint32_t th_seq;             /* sequence number */
	uint32_t th_ack;             /* acknowledgement number */
	uint8_t th_x2:4;           /* (unused) */
	uint8_t th_off:4;          /* data offset */
	uint8_t th_flags;
	uint16_t th_win;           /* window */
	uint16_t th_sum;           /* checksum */
	uint16_t th_urp;           /* urgent pointer */
};

struct version_header
{
	uint32_t major;
	uint32_t minor;
	uint32_t padding;
};

struct mux_header
{
	uint32_t protocol;
	uint32_t length;
	uint32_t magic;
	uint16_t tx_seq;
	uint16_t rx_seq;
};

typedef struct{
	uint8_t version; /*Protocol version*/
	uint16_t rx_seq; /*itunes protocol rx seq*/
	uint16_t tx_seq;	/*itunes protocol tx seq*/
	uint32_t protlen;	/*itnues protocol total size include itself tcpheader*/
	uint32_t prohlen;	/*itnues protocol handle size*/
	/*tcp connection information*/
	struct{
		uint16_t sport, dport;
		uint32_t tx_seq, tx_ack, tx_acked, tx_win;
		uint32_t rx_seq, rx_recvd, rx_ack, rx_win;
		int flags;
	}tcpinfo;
	uint32_t max_payload;		
	usb_device usbdev;	/*usb interface*/
	/*Buffer used for receive/send data*/
	uint8_t *ib_buf;
	uint32_t ib_used;
	uint32_t ib_capacity;
}mux_itunes;

typedef struct {
	enum mux_conn_state State;		/*U-Storage Status*/
	uint8_t usType;	/*Android or IOS*/
	uint16_t VendorID; /**< Vendor ID for the USB product. */
	uint16_t ProductID; /**< Unique product ID for the USB product. */
	mux_itunes  itunes;
	void *priv;		/*Just used in ios, represent mux_itnues structure*/
}usStorage_info;

/*****************************************************************************
 * Global Var
 ****************************************************************************/
usStorage_info uSinfo;




/*****************************************************************************
 * Private functions
 ****************************************************************************/
static uint16_t find_sport(void)
{
	static uint16_t tcport =1;
	if(tcport == 0xFFFF){
		PRODEBUG("Port Reach To Max Reset it..\r\n");
		tcport = 0;
	}
	return (++tcport);
}

static int send_packet(mux_itunes *dev, enum mux_protocol proto, void *header, const void *data, int length)
{
	uint8_t *buffer;
	int hdrlen;
	int res;

	switch(proto) {
		case MUX_PROTO_VERSION:
			hdrlen = sizeof(struct version_header);
			break;
		case MUX_PROTO_SETUP:
			hdrlen = 0;
			break;
		case MUX_PROTO_TCP:
			hdrlen = sizeof(struct tcphdr);
			break;
		default:
			PRODEBUG("Invalid protocol %d for outgoing packet (hdr %p data %p len %d)\r\n", proto, header, data, length);	
			dev->ib_used = 0;
			return -1;
	}
	PRODEBUG("send_packet(0x%x, %p, %p, %d)", proto, header, data, length);

	int mux_header_size = ((dev->version < 2) ? 8 : sizeof(struct mux_header));

	int total = mux_header_size + hdrlen + length;

	if(proto != MUX_PROTO_TCP && total > dev->ib_capacity){
		PRODEBUG("Tried to send packet larger than USB MTU (hdr %d data %d total %d) to device\r\n", 
							hdrlen, length, total);
		dev->ib_used = 0;
		return -1;
	}
	buffer = dev->ib_buf;
	struct mux_header *mhdr = (struct mux_header *)buffer;
	mhdr->protocol = htonl(proto);
	mhdr->length = htonl(total);
	if (dev->version >= 2) {
		mhdr->magic = htonl(0xfeedface);
		if (proto == MUX_PROTO_SETUP) {
			dev->tx_seq = 0;
			dev->rx_seq = 0xFFFF;
		}
		mhdr->tx_seq = htons(dev->tx_seq);
		mhdr->rx_seq = htons(dev->rx_seq);
		dev->tx_seq++;
	}
	memcpy(buffer + mux_header_size, header, hdrlen);

	dev->ib_used = mux_header_size + hdrlen;
	if(total > dev->ib_capacity){
		uint16_t sndsize = 0, packetSize;
		while(sndsize < total){
			packetSize = min(total-sndsize, dev->ib_capacity-dev->ib_used);
			memcpy((void*)buffer + dev->ib_used, data+sndsize, packetSize);
			if((res = usUsb_BlukPacketSend(&(dev->usbdev), buffer, dev->ib_used+packetSize)) < 0) {
				PRODEBUG("usb_send failed while sending packet (len %d) to device: %d\r\n",  
							dev->ib_used+packetSize, res);
				dev->ib_used = 0;
				return res;
			}
			dev->ib_used -= res;
			sndsize+= res;
			PRODEBUG("fragment sending packet ok(len %d) to device: %d\r\n", total, res);	
		}
	}else{
		if(data && length)
			memcpy(buffer + dev->ib_used, data, length);
		if((res = usUsb_BlukPacketSend(&(dev->usbdev), buffer, total)) < 0) {
			PRODEBUG("usb_send failed while sending packet (len %d) to device: %d\r\n", total, res);
			dev->ib_used = 0;
			return res;
		}
		PRODEBUG("sending packet ok(len %d) to device: %d\r\n", total, res);
	}
	dev->ib_used = 0;
	return total;
}

static int send_tcp(mux_itunes *conn, uint8_t flags, const unsigned char *data, int length)
{
	struct tcphdr th;

	if(!conn){
		return -1;
	}
	memset(&th, 0, sizeof(th));
	th.th_sport = htons(conn->tcpinfo.sport);
	th.th_dport = htons(conn->tcpinfo.dport);
	th.th_seq = htonl(conn->tcpinfo.tx_seq);
	th.th_ack = htonl(conn->tcpinfo.tx_ack);
	th.th_flags = flags;
	th.th_off = sizeof(th) / 4;
	th.th_win = htons(conn->tcpinfo.tx_win >> 8);

	PRODEBUG("[OUT]sport=%d dport=%d seq=%d ack=%d flags=0x%x window=%d[%d] len=%d\r\n",
				conn->tcpinfo.sport, conn->tcpinfo.dport, 
				conn->tcpinfo.tx_seq, conn->tcpinfo.tx_ack, flags, 
				conn->tcpinfo.tx_win, conn->tcpinfo.tx_win >> 8, length);

	int res = send_packet(conn, MUX_PROTO_TCP, &th, data, length);
	if(res >= 0) {
		conn->tcpinfo.tx_acked = conn->tcpinfo.tx_ack;
	}
	return res;
}

static int send_tcp_ack(mux_itunes *conn)
{
	if(send_tcp(conn, TH_ACK, NULL, 0) < 0) {
		PRODEBUG("Error sending TCP ACK (%d->%d)", 
				conn->tcpinfo.sport, conn->tcpinfo.dport);
		return -1;
	}
	return 0;
}

/*****************************************************************************
 * Private functions[Chip]
 ****************************************************************************/
static uint8_t NXP_FILTERFUNC_AOA_CLASS(void* const CurrentDescriptor)
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

static uint8_t  NXP_COMPFUNC_AOA_CLASS(void* const CurrentDescriptor)
{
	USB_Descriptor_Header_t* Header = DESCRIPTOR_PCAST(CurrentDescriptor, USB_Descriptor_Header_t);

	if (Header->Type == DTYPE_Interface){
		USB_Descriptor_Interface_t* Interface = DESCRIPTOR_PCAST(CurrentDescriptor, USB_Descriptor_Interface_t);
		if (Interface->Class  == AOA_FTRANS_CLASS&&
				(Interface->SubClass == AOA_FTRANS_SUBCLASS) &&
		   		(Interface->Protocol ==AOA_FTRANS_PROTOCOL)){
			return DESCRIPTOR_SEARCH_Found;
		}
	}

	return DESCRIPTOR_SEARCH_NotFound;
}

static uint8_t NXP_COMPFUNC_IOS_CLASS(void* const CurrentDescriptor)
{
	USB_Descriptor_Header_t* Header = DESCRIPTOR_PCAST(CurrentDescriptor, USB_Descriptor_Header_t);

	if (Header->Type == DTYPE_Interface){
		USB_Descriptor_Interface_t* Interface = DESCRIPTOR_PCAST(CurrentDescriptor, USB_Descriptor_Interface_t);

		if (Interface->Class  == IOS_FTRANS_CLASS&&
				(Interface->SubClass == IOS_FTRANS_SUBCLASS) &&
		   		(Interface->Protocol ==IOS_FTRANS_PROTOCOL)){
			return DESCRIPTOR_SEARCH_Found;
		}
	}

	return DESCRIPTOR_SEARCH_NotFound;
}


static uint8_t DCOMP_MS_Host_NextMSInterfaceEndpoint(void* const CurrentDescriptor)
{
	USB_Descriptor_Header_t* Header = DESCRIPTOR_PCAST(CurrentDescriptor, USB_Descriptor_Header_t);

	if (Header->Type == DTYPE_Endpoint){
		USB_Descriptor_Endpoint_t* Endpoint = DESCRIPTOR_PCAST(CurrentDescriptor, USB_Descriptor_Endpoint_t);

		uint8_t EndpointType = (Endpoint->Attributes & EP_TYPE_MASK);

		if ((EndpointType == EP_TYPE_BULK) && (!(Pipe_IsEndpointBound(Endpoint->EndpointAddress)))){
			return DESCRIPTOR_SEARCH_Found;
		}
	}else if (Header->Type == DTYPE_Interface){
		return DESCRIPTOR_SEARCH_Fail;
	}

	return DESCRIPTOR_SEARCH_NotFound;
}

static uint8_t NXP_SwitchAOAMode(usb_device *usbdev)
{
	USB_ClassInfo_MS_Host_t *MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)(usbdev->os_priv);
	uint8_t version[2] = {0};
	uint16_t ConfigDescriptorSize = 0;
	uint8_t  ConfigDescriptorData[512];
	
	if(!usbdev){
		PRODEBUG("Parameter Empty..\r\n");
		return PROTOCOL_REPARA;
	}
	memset(&MSInterfaceInfo->State, 0x00, sizeof(MSInterfaceInfo->State));

	if(usUusb_GetDeviceConfigDescriptor(usbdev, 1, &ConfigDescriptorSize, 
						ConfigDescriptorData, sizeof(ConfigDescriptorData))){
		PRODEBUG("Get Device ConfigDescriptor Error..\r\n");
		return PROTOCOL_REGEN;
	}

	if (DESCRIPTOR_TYPE(ConfigDescriptorData) != DTYPE_Configuration){
		return PROTOCOL_REGEN;
	}
	/*Found Interface Class */
	if (USB_GetNextDescriptorComp(&ConfigDescriptorSize, &ConfigDescriptorData,
				NXP_FILTERFUNC_AOA_CLASS) != DESCRIPTOR_SEARCH_COMP_Found){		
		PRODEBUG("Attached Device Not a Valid AOA Device[NO 0xff Interface].\r\n");
		return PROTOCOL_REGEN;
	}
	/*Set AOA*/
	if(usUsb_SendControlRequest(usbdev, REQDIR_DEVICETOHOST|REQTYPE_VENDOR, 
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
		if(usUsb_SendControlRequest(usbdev, REQDIR_HOSTTODEVICE|REQTYPE_VENDOR,
						AOA_SEND_IDENT, 0, AOA_STRING_MAN_ID, 
					strlen(acc_default.manufacturer) + 1, (uint8_t *)acc_default.manufacturer)){
			PRODEBUG("Get AOA Protocol Version Failed.\r\n");
			return PROTOCOL_REGEN;
		}
	}
	if(acc_default.model) {
		PRODEBUG("sending model: %s\r\n", acc_default.model);
		if(usUsb_SendControlRequest(usbdev, REQDIR_HOSTTODEVICE|REQTYPE_VENDOR,
								AOA_SEND_IDENT, 0, AOA_STRING_MOD_ID, 
							strlen(acc_default.model) + 1, (uint8_t *)acc_default.model)){
			PRODEBUG("Could not Set AOA model.\r\n");
			return PROTOCOL_REGEN;
		}
	}

	PRODEBUG("sending description: %s\r\n", acc_default.description);
	if(usUsb_SendControlRequest(usbdev, REQDIR_HOSTTODEVICE|REQTYPE_VENDOR,
						AOA_SEND_IDENT, 0, AOA_STRING_DSC_ID, 
						strlen(acc_default.description) + 1, (uint8_t *)acc_default.description)){
		PRODEBUG("Could not Set AOA description.\r\n");
		return PROTOCOL_REGEN;
	}

	PRODEBUG("sending version string: %s\r\n", acc_default.version);
	if(usUsb_SendControlRequest(usbdev, REQDIR_HOSTTODEVICE|REQTYPE_VENDOR,
						AOA_SEND_IDENT, 0, AOA_STRING_VER_ID, 
						strlen(acc_default.version) + 1, (uint8_t *)acc_default.version)){
		PRODEBUG("Could not Set AOA version.\r\n");
		return PROTOCOL_REGEN;
	}
	PRODEBUG("sending url string: %s\r\n", acc_default.url);
	if(usUsb_SendControlRequest(usbdev, REQDIR_HOSTTODEVICE|REQTYPE_VENDOR,
						AOA_SEND_IDENT, 0, AOA_STRING_URL_ID, 
						strlen(acc_default.url) + 1, (uint8_t *)acc_default.url)){
		PRODEBUG("Could not Set AOA url.\r\n");
		return PROTOCOL_REGEN;
	}
	PRODEBUG("sending serial number: %s\r\n", acc_default.serial);
	if(usUsb_SendControlRequest(usbdev, REQDIR_HOSTTODEVICE|REQTYPE_VENDOR,
						AOA_SEND_IDENT, 0, AOA_STRING_SER_ID, 
						strlen(acc_default.serial) + 1, (uint8_t *)acc_default.serial)){
		PRODEBUG("Could not Set AOA serial.\r\n");
		return PROTOCOL_REGEN;
	}

	if(usUsb_SendControlRequest(usbdev, REQDIR_HOSTTODEVICE|REQTYPE_VENDOR,
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

uint8_t usProtocol_SwitchAOAMode(usb_device *usbdev)
{
	return NXP_SwitchAOAMode(usbdev);
}

uint8_t usProtocol_ConnectIOSPhone(mux_itunes *uSdev)
{
	uint8_t mux_header_size, mux_total_size; 
	struct version_header rvh, *vh;

	if(!uSdev){
		return PROTOCOL_REPARA;
	}
	uSdev->version = 0;
	uSdev->tx_seq = uSdev->rx_seq = uSdev->protlen = 0;
	/*confirm source dest port*/
	memset(&(uSdev->tcpinfo), 0, sizeof(uSdev->tcpinfo));
	uSdev->tcpinfo.sport = find_sport();
	uSdev->tcpinfo.dport= IOS_DEFAULT_PORT;
	uSdev->tcpinfo.tx_win = IOS_DEFAULT_WIN;
	
	/*Begin to conncet to iPhone*/
	/*1.request PROTOCOL_VERSION*/
	rvh.major = htonl(2);
	rvh.minor = htonl(0);
	rvh.padding = 0;
	if(send_packet(uSdev, MUX_PROTO_VERSION, &rvh, NULL, 0) < 0) {
		PRODEBUG("Error sending version request packet to device\r\n");
		return PROTOCOL_REINVAILD;
	}
	/*Send Successful receive response*/
	mux_header_size = ((uSdev->version < 2) ? 8 : sizeof(struct mux_header));
	mux_total_size = mux_header_size + sizeof(struct version_header);
	if(usUsb_BlukPacketReceive(&(uSdev->usbdev), uSdev->ib_buf, mux_total_size)){
		PRODEBUG("Error receive version request packet from phone\r\n");
		return PROTOCOL_REINVAILD;
	}
	vh = (struct version_header *)(uSdev->ib_buf+mux_header_size);
	vh->major = ntohl(vh->major);
	vh->minor = ntohl(vh->minor);
	if(vh->major != 2 && vh->major != 1) {
		PRODEBUG("Device has unknown version %d.%d\r\n", vh->major, vh->minor);
		return PROTOCOL_REINVAILD;
	}
	uSdev->version = vh->major;

	if (uSdev->version >= 2) {
		send_packet(uSdev, MUX_PROTO_SETUP, NULL, "\x07", 1);
	}

	PRODEBUG("Connected to v%d.%d device\r\n", uSdev->version, vh->minor);
	/*Send TH_SYNC*/
	if(send_tcp(uSdev, TH_SYN, NULL, 0) < 0){
		PRODEBUG("Error sending TCP SYN to device (%d->%d)\r\n", 
				uSdev->tcpinfo.sport, uSdev->tcpinfo.dport);
		return PROTOCOL_REINVAILD; //bleh
	}
	/*Wait TH_ACK*/

	return PROTOCOL_REOK;
}

uint8_t usProtocol_ConnectPhone(uint8_t *buffer, uint32_t size)
{
	if(uSinfo.State  == CONN_INIT){
		PRODEBUG("Error Connect State[INIT]\r\n");
		return PROTOCOL_REINVAILD;
	}
	if(uSinfo.usType == PRO_ANDROID && 
			uSinfo.State == CONN_CONNECTING){
		uSinfo.State = CONN_CONNECTED;
		PRODEBUG("Androd Device[v/p=%d:%d] Connected\r\n", 
					uSinfo.VendorID, uSinfo.ProductID);
		uSinfo.itunes.ib_buf = buffer;
		uSinfo.itunes.ib_capacity= size;
		uSinfo.itunes.ib_used = 0;
		return PROTOCOL_REOK;
	}
	/*Connect iPhone, we need to the large buffer to send packet*/
	uSinfo.itunes.ib_buf = buffer;
	uSinfo.itunes.ib_capacity= size;
	uSinfo.itunes.ib_used = 0;	
	if(usProtocol_ConnectIOSPhone(&(uSinfo.itunes))){
		PRODEBUG("iPhone Device[v/p=%d:%d] Connect Failed\r\n", 
					uSinfo.VendorID, uSinfo.ProductID);
		return PROTOCOL_REINVAILD;
	}
	uSinfo.State = CONN_CONNECTED;
	PRODEBUG("iPhone Device[v/p=%d:%d] Connected\r\n", 
				uSinfo.VendorID, uSinfo.ProductID);
	return PROTOCOL_REOK;	
}
uint8_t usProtocol_DeviceDetect(void *os_priv)
{
	USB_StdDesDevice_t DeviceDescriptorData;
	int8_t PhoneType = -1;
	nxp_clminface nxpcall;
	
	usb_device *usbdev = &(uSinfo.itunes.usbdev);
	/*We need to memset*/
	memset(&uSinfo, 0, sizeof(uSinfo));
	/*set os_priv*/
	usUusb_Init(usbdev, os_priv);
	/*GEt device description*/
	memset(&DeviceDescriptorData, 0, sizeof(USB_StdDesDevice_t));
	if(usUusb_GetDeviceDescriptor(usbdev, &DeviceDescriptorData)){
		PRODEBUG("usUusb_GetDeviceDescriptor Failed\r\n");
		return PROTOCOL_REGEN;
	}
	if(DeviceDescriptorData.idVendor == VID_APPLE &&
		(DeviceDescriptorData.idProduct>= PID_RANGE_LOW 
			&& DeviceDescriptorData.idProduct <= PID_RANGE_MAX)){
		/*iPhone Device*/
		PRODEBUG("Found iPhone Device[v/p=%d:%d].\r\n", 
				DeviceDescriptorData.idVendor, DeviceDescriptorData.idProduct);
		/*Set Type*/
		PhoneType = PRO_IOS;
		/*Set callback*/
		nxpcall.callbackInterface = NXP_COMPFUNC_IOS_CLASS;
		nxpcall.callbackEndpoint= DCOMP_MS_Host_NextMSInterfaceEndpoint;
		
	}else if(DeviceDescriptorData.idVendor == AOA_ACCESSORY_VID &&
		(DeviceDescriptorData.idProduct >= AOA_ACCESSORY_PID 
			&& DeviceDescriptorData.idProduct <= AOA_ACCESSORY_AUDIO_ADB_PID)){
		/*Android Device*/
		PRODEBUG("Found Android Device[v/p=%d:%d].\r\n", 
				DeviceDescriptorData.idVendor, DeviceDescriptorData.idProduct);
		/*Set Type*/
		PhoneType = PRO_ANDROID;		
		/*Set callback*/
		nxpcall.callbackInterface = NXP_COMPFUNC_AOA_CLASS;
		nxpcall.callbackEndpoint= DCOMP_MS_Host_NextMSInterfaceEndpoint;
	}else{
		/*Switch to AOA Mode*/
		return usProtocol_SwitchAOAMode(usbdev);
	}
	/*Claim Interface*/
	if(usUusb_ClaimInterface(usbdev, &nxpcall)){
		PRODEBUG("Attached Device Not a Valid Android AOA Device.\r\n");
		return PROTOCOL_REINVAILD;
	}
	/*Set Configuration*/
	if(usUusb_SetDeviceConfigDescriptor(usbdev, 1)){
		PRODEBUG("Error Setting Device Configuration.\r\n");
		return PROTOCOL_REINVAILD;
	}
	/*Set Global var*/
	uSinfo.usType = PhoneType;
	uSinfo.VendorID = DeviceDescriptorData.idVendor;
	uSinfo.ProductID = DeviceDescriptorData.idProduct;
	uSinfo.State = CONN_CONNECTING;
	
	PRODEBUG("Phone Change to CONNCETING State.\r\n");
	
	return PROTOCOL_REOK;
}















