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
#include "usSys.h"
#include "protocol.h"
#if defined(NXP_CHIP_18XX)
#include "MassStorageHost.h"
#elif defined(LINUX)
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <getopt.h>
#include <pwd.h>
#include <grp.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/un.h>
#include <sys/select.h>
#include <linux/types.h>
#include <linux/netlink.h>
#include <sys/ioctl.h>
#include <scsi/sg.h>

#if !defined(BLKGETSIZE64)
#define BLKGETSIZE64           _IOR(0x12,114,size_t)
#endif

#endif

#define MSC_FTRANS_CLASS				0x08
#define MSC_FTRANS_SUBCLASS			0x06
#define MSC_FTRANS_PROTOCOL			0x50

#if defined(DEBUG_ENABLE)
#define DSKDEBUG(...) do {printf("[DISK Mod]");printf(__VA_ARGS__);} while(0)
#else
#define DSKDEBUG(...)
#endif

#define STOR_DFT_PRO		"U-Storage"
#define STOR_DFT_VENDOR		"i4season"

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
	int64_t disk_cap;
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
	
	return DISK_REOK;
}

#elif defined(LINUX)

usb_device disk_phone;
char dev[256];
uint8_t usDisk_DeviceDetect(void *os_priv)
{
	unsigned char sense_b[32] = {0};
	unsigned char rcap_buff[8] = {0};
	unsigned char cmd[] = {0x25, 0, 0, 0 , 0, 0};
	struct sg_io_hdr io_hdr;
	unsigned int lastblock, blocksize;
	int dev_fd;
	int64_t disk_cap = 0;

	if(os_priv == NULL){
		return DISK_REGEN;
	}
	memset(&uDinfo, 0, sizeof(uDinfo));
	strcpy(dev, os_priv);
	disk_phone.os_priv = dev;
	memcpy(&uDinfo.diskdev, &disk_phone, sizeof(usb_device));

	dev_fd= open(dev, O_RDWR | O_NONBLOCK);
	if (dev_fd < 0 && errno == EROFS)
		dev_fd = open(dev, O_RDONLY | O_NONBLOCK);
	if (dev_fd<0){
		DSKDEBUG("Open %s Failed:%s", dev, strerror(errno));
		return DISK_REGEN; 
	}

	memset(&io_hdr, 0, sizeof(struct sg_io_hdr));
	io_hdr.interface_id = 'S';
	io_hdr.cmd_len = sizeof(cmd);
	io_hdr.dxferp = rcap_buff;
	io_hdr.dxfer_len = 8;
	io_hdr.mx_sb_len = sizeof(sense_b);
	io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
	io_hdr.cmdp = cmd;
	io_hdr.sbp = sense_b;
	io_hdr.timeout = 20000;

	if(ioctl(dev_fd, SG_IO, &io_hdr)<0){
		DSKDEBUG("IOCTRL error:%s[Used BLKGETSIZE64]!", strerror(errno));
		if (ioctl(dev_fd, BLKGETSIZE64, &disk_cap) != 0) {			
			DSKDEBUG("Get Disk Capatiy Failed");
		}		
		DSKDEBUG("Disk Capacity = %lld Bytes", disk_cap);
		close(dev_fd);
		uDinfo.disk_cap = disk_cap;
		uDinfo.BlockSize = 512;
		uDinfo.Blocks = disk_cap/uDinfo.BlockSize;
		uDinfo.disknum=1;
		return DISK_REOK;
	}

	/* Address of last disk block */
	lastblock =  ((rcap_buff[0]<<24)|(rcap_buff[1]<<16)|
	(rcap_buff[2]<<8)|(rcap_buff[3]));

	/* Block size */
	blocksize =  ((rcap_buff[4]<<24)|(rcap_buff[5]<<16)|
	(rcap_buff[6]<<8)|(rcap_buff[7]));

	/* Calculate disk capacity */
	uDinfo.Blocks= (lastblock+1);
	uDinfo.BlockSize= blocksize;	
	uDinfo.disk_cap  = (lastblock+1);
	uDinfo.disk_cap *= blocksize;
	uDinfo.disknum=1;
	DSKDEBUG("Disk Blocks = %u BlockSize = %u Disk Capacity=%lld\n", 
			uDinfo.Blocks, uDinfo.BlockSize, uDinfo.disk_cap);
	close(dev_fd);

	return DISK_REOK;
}
#endif

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

uint8_t usDisk_DiskInquiry(struct scsi_inquiry_info *inquiry)
{
	if(!inquiry){
		DSKDEBUG("usDisk_DiskInquiry Parameter Error\r\n");
		return DISK_REPARA;
	}
	memset(inquiry, 0, sizeof(struct scsi_inquiry_info));
	/*Init Other Info*/
	inquiry->size = uDinfo.disk_cap;
	strcpy(inquiry->product, STOR_DFT_PRO);
	strcpy(inquiry->vendor, STOR_DFT_VENDOR);
	strcpy(inquiry->serial, "1234567890abcdef");
	strcpy(inquiry->version, "1.0");

	return DISK_REOK;	
}
