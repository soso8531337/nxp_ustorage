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
 
 #include "usUsb.h"
#include "usDisk.h"
#include "usProtocol.h"
#include "usSys.h"
#if defined(NXP_CHIP_18XX)
#include "fsusb_cfg.h"
#include "FreeRTOS.h"
#include "task.h"
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
#endif

/*****************************************************************************
 * Private types/enumerations/variables
 ****************************************************************************/
#if defined(DEBUG_ENABLE)
#define SDEBUGOUT(...) do {printf("[Storage Mod]");printf(__VA_ARGS__);} while(0)
#else
#define SDEBUGOUT(...)
#endif

#define USDISK_SECTOR		512
#define OP_DIV(x)			((x)/USDISK_SECTOR)
#define OP_MOD(x)			((x)%USDISK_SECTOR)

#if defined(NXP_CHIP_18XX)
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

void scu_pinmux(unsigned port, unsigned pin, unsigned mode, unsigned func)
{
	volatile unsigned int * const scu_base=(unsigned int*)(LPC_SCU_BASE);
	scu_base[(PORT_OFFSET*port+PIN_OFFSET*pin)/4]=mode+func;
} /* scu_pinmux */

#define SCU_SFSPx(port, pin) (*((volatile uint32_t *) ((LPC_SCU_BASE + PORT_OFFSET * port + PIN_OFFSET * pin))))
/**
  \fn          int32_t SCU_PinConfiguare (uint8_t port, uint8_t pin, uint32_t pin_cfg)
  \brief       Set pin function and electrical characteristics
  \param[in]   port       Port number (0..15)
  \param[in]   pin        Pin number (0..31)
  \param[in]   pin_cfg    pin_cfg configuration bit mask
   - \b  0: function succeeded
   - \b -1: function failed
*/
int32_t SCU_PinConfigure (uint8_t port, uint8_t pin, uint32_t pin_cfg) 
{
	if ((port > 15) || (pin > 31)) return -1;
	SCU_SFSPx(port, pin) = pin_cfg;
	return 0;
}

int32_t SCU_USB1_PinConfigure (uint32_t USB1_pin_cfg) 
{
	LPC_SCU->SFSUSB = USB1_pin_cfg;
	return 0;
}

#define SCU_SFSUSB_AIM                (1    <<  0)
#define SCU_SFSUSB_ESEA               (1    <<  1)
#define SCU_SFSUSB_EPD                (1    <<  2)
#define SCU_SFSUSB_EPWR               (1    <<  4)
#define SCU_SFSUSB_VBUS               (1    <<  5)

#define SCU_USB1_PIN_CFG_AIM                  (SCU_SFSUSB_AIM )
#define SCU_USB1_PIN_CFG_ESEA                 (SCU_SFSUSB_ESEA)
#define SCU_USB1_PIN_CFG_EPD                  (SCU_SFSUSB_EPD )
#define SCU_USB1_PIN_CFG_EPWR                 (SCU_SFSUSB_EPWR)
#define SCU_USB1_PIN_CFG_VBUS                 (SCU_SFSUSB_VBUS)

#define USB_PORTSC1_H_PTS_POS                  (          30U)
#define USB_PORTSC1_H_PTS_MSK                  (3UL    << USB_PORTSC1_H_PTS_POS)
#define USB_PORTSC1_H_PTS(n)                   (((n)   << USB_PORTSC1_H_PTS_POS) & USB_PORTSC1_H_PTS_MSK)
static void SetUsb1ClockPinmux( void )
{
	volatile int32_t tmo = 1000;
	uint32_t  portsc;
		
#if (RTE_USB_USB1_HS_PHY_EN)// modified by Roger
	SCU_PinConfigure(RTE_USB1_ULPI_CLK_PORT, RTE_USB1_ULPI_CLK_BIT,  RTE_USB1_ULPI_CLK_FUNC | SCU_SFS_EPUN | SCU_SFS_EHS | SCU_SFS_EZI | SCU_SFS_ZIF);
	SCU_PinConfigure(RTE_USB1_ULPI_DIR_PORT, RTE_USB1_ULPI_DIR_BIT,  RTE_USB1_ULPI_DIR_FUNC | SCU_SFS_EPUN | SCU_SFS_EHS | SCU_SFS_EZI | SCU_SFS_ZIF);
	SCU_PinConfigure(RTE_USB1_ULPI_STP_PORT, RTE_USB1_ULPI_STP_BIT,  RTE_USB1_ULPI_STP_FUNC | SCU_SFS_EPUN | SCU_SFS_EHS | SCU_SFS_EZI | SCU_SFS_ZIF);
	SCU_PinConfigure(RTE_USB1_ULPI_NXT_PORT, RTE_USB1_ULPI_NXT_BIT,  RTE_USB1_ULPI_NXT_FUNC | SCU_SFS_EPUN | SCU_SFS_EHS | SCU_SFS_EZI | SCU_SFS_ZIF);
	SCU_PinConfigure(RTE_USB1_ULPI_D0_PORT,  RTE_USB1_ULPI_D0_BIT,   RTE_USB1_ULPI_D0_FUNC | SCU_SFS_EPUN | SCU_SFS_EHS | SCU_SFS_EZI | SCU_SFS_ZIF );
	SCU_PinConfigure(RTE_USB1_ULPI_D1_PORT,  RTE_USB1_ULPI_D1_BIT,   RTE_USB1_ULPI_D1_FUNC | SCU_SFS_EPUN | SCU_SFS_EHS | SCU_SFS_EZI | SCU_SFS_ZIF );
	SCU_PinConfigure(RTE_USB1_ULPI_D2_PORT,  RTE_USB1_ULPI_D2_BIT,   RTE_USB1_ULPI_D2_FUNC | SCU_SFS_EPUN | SCU_SFS_EHS | SCU_SFS_EZI | SCU_SFS_ZIF );
	SCU_PinConfigure(RTE_USB1_ULPI_D3_PORT,  RTE_USB1_ULPI_D3_BIT,   RTE_USB1_ULPI_D3_FUNC | SCU_SFS_EPUN | SCU_SFS_EHS | SCU_SFS_EZI | SCU_SFS_ZIF );
	SCU_PinConfigure(RTE_USB1_ULPI_D4_PORT,  RTE_USB1_ULPI_D4_BIT,   RTE_USB1_ULPI_D4_FUNC | SCU_SFS_EPUN | SCU_SFS_EHS | SCU_SFS_EZI | SCU_SFS_ZIF );
	SCU_PinConfigure(RTE_USB1_ULPI_D5_PORT,  RTE_USB1_ULPI_D5_BIT,   RTE_USB1_ULPI_D5_FUNC | SCU_SFS_EPUN | SCU_SFS_EHS | SCU_SFS_EZI | SCU_SFS_ZIF );
	SCU_PinConfigure(RTE_USB1_ULPI_D6_PORT,  RTE_USB1_ULPI_D6_BIT,   RTE_USB1_ULPI_D6_FUNC | SCU_SFS_EPUN | SCU_SFS_EHS | SCU_SFS_EZI | SCU_SFS_ZIF );
	SCU_PinConfigure(RTE_USB1_ULPI_D7_PORT,  RTE_USB1_ULPI_D7_BIT,   RTE_USB1_ULPI_D7_FUNC | SCU_SFS_EPUN | SCU_SFS_EHS | SCU_SFS_EZI | SCU_SFS_ZIF );
#endif

	/* switch to ulpi phy and turn on the power to phy*/
	printf("PORTSC1_H = %x\r\n",LPC_USB1->PORTSC1_H);
	portsc = LPC_USB1->PORTSC1_H & 0x00FFFFFF;
	portsc |= 0x80000000;
	LPC_USB1->PORTSC1_H = portsc;
	/* reset the controller */
	printf("bask %x \r\n", &(LPC_CCU1->CLKCCU[CLK_MX_USB1].CFG));
	LPC_CGU->BASE_CLK[CLK_BASE_USB1]     = (0x01U << 11) | (0x0CU << 24) ; 

	/* disable USB1_CLOCK */
	LPC_CCU1->CLKCCU[CLK_USB1].CFG = 0;

	/* reset the controller */
	LPC_CCU1->CLKCCU[CLK_MX_USB1].CFG |= 1U;
	while (!(LPC_CCU1->CLKCCU[CLK_MX_USB1].STAT & 1U));
	LPC_USB1->PORTSC1_H |=   USB_PORTSC1_H_PTS(2U);
	SCU_USB1_PinConfigure (SCU_USB1_PIN_CFG_AIM  |
					SCU_USB1_PIN_CFG_ESEA |
					SCU_USB1_PIN_CFG_EPD  |
					SCU_USB1_PIN_CFG_EPWR);
}


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
		SetUsb1ClockPinmux();
//		Chip_USB1_Init();
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

#endif //#if defined(NXP_CHIP_18XX)
/*****************************************************************************
 * Private functions
 ****************************************************************************/
static int usStorage_sendHEAD(struct scsi_head *header)
{
	uint8_t buffer[PRO_HDR] = {0};

	if(!header){
		return 1;
	}

	memcpy(buffer, header, PRO_HDR);
	/*Send To Phone*/
	if(usProtocol_SendPackage(buffer, PRO_HDR)){
		SDEBUGOUT("Send To Phone[Just Header Error]\r\n");
		return 1;
	}

	return 0;
}

static int usStorage_sendHEADBUF(struct scsi_head *header)
{
	uint8_t *buffer = NULL;
	uint32_t size = 0;

	if(!header){
		return 1;
	}

	if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
		SDEBUGOUT("usProtocol_GetAvaiableBuffer Failed\r\n");
		return 1;
	}
	memcpy(buffer, header, PRO_HDR);
	/*Send To Phone*/
	if(usProtocol_SendPackage(buffer, PRO_HDR)){
		SDEBUGOUT("Send To Phone[Just Header Error]\r\n");
		return 1;
	}

	return 0;
}

/*
*Read Multi TIme
*/
static int usStorage_diskMULREAD(struct scsi_head *header)
{
	uint8_t *buffer = NULL;
	uint32_t size = 0, rsize = 0, avsize = 0;
	int32_t addr = 0;

	if(!header){
		SDEBUGOUT("usStorage_diskREAD Parameter Failed\r\n");
		return 1;
	}
	addr = header->addr;
	
	if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
		SDEBUGOUT("usProtocol_GetAvaiableBuffer Failed\r\n");
		return 1;
	}
	memcpy(buffer, header, PRO_HDR);
	/*Send To Phone*/
	if(usProtocol_SendPackage(buffer, PRO_HDR)){
		SDEBUGOUT("Send To Phone[Just Header Error]\r\n");
		return 1;
	}
	if(!header->len){		
		SDEBUGOUT("Read Request Len is 0[MayBeError]\r\n");
		return 0;
	}
	while(rsize < header->len){
		uint32_t secCount = 0;
		if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
			SDEBUGOUT("usProtocol_GetAvaiableBuffer Failed\r\n");
			return 1;
		}

		avsize = min(USDISK_SECTOR*OP_DIV(size), header->len-rsize); /*We leave a sector for safe*/		
		secCount = OP_DIV(avsize);
		if(usDisk_DiskReadSectors(buffer, addr, secCount)){
			SDEBUGOUT("Read Sector Error[SndTotal:%d addr:%d  SectorCount:%d]\r\n",
					rsize, addr, secCount);
			return 1;
		}
		/*Send To Phone*/
		if(usProtocol_SendPackage(buffer, avsize)){
			SDEBUGOUT("Send To Phone[SndTotal:%d addr:%d  SectorCount:%d]\r\n",
					rsize, addr, secCount);
			return 1;
		}
		SDEBUGOUT("READ INFO:%p[SZ:%dBytes][DS:%d(%d-->%d) [TS:%dBytes]\r\n", 
						buffer, avsize, header->addr, addr, addr +secCount, header->len);
		addr += secCount;
		rsize += avsize;
	}

	SDEBUGOUT("REQUEST READ OK:\r\nwtag=%d\r\nctrid=%d\r\naddr=%d\r\nlen=%d\r\nwlun=%d\r\n", 
			header->wtag, header->ctrid, header->addr, header->len, header->wlun);
	
	return 0;
}

/*Read Once*/
static int usStorage_diskSIGREAD(struct scsi_head *header)
{
	uint8_t *buffer = NULL;
	uint32_t size = 0;

	if(!header){
		SDEBUGOUT("usStorage_diskREAD Parameter Failed\r\n");
		return 1;
	}
	
	if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
		SDEBUGOUT("usProtocol_GetAvaiableBuffer Failed\r\n");
		return 1;
	}
	if(size < header->len + PRO_HDR){
		SDEBUGOUT("usStorage_diskSIGREAD Space Not Enough\r\n");
		return 1;
	}
	memcpy(buffer, header, PRO_HDR);

	if(usDisk_DiskReadSectors(buffer+PRO_HDR, header->addr, OP_DIV(header->len))){
		SDEBUGOUT("Read Sector Error[SndTotal:%d addr:%d  SectorCount:%d]\r\n",
				header->len+PRO_HDR, header->addr, OP_DIV(header->len));
		/*Write to Phone*/
		header->relag = 1;
		usStorage_sendHEAD(header);	
		return 1;
	}
	/*Send To Phone*/
	if(usProtocol_SendPackage(buffer, PRO_HDR+header->len)){
		SDEBUGOUT("Send To Phone[SndTotal:%d addr:%d  SectorCount:%d]\r\n",
				header->len+PRO_HDR, header->addr, OP_DIV(header->len));
		return 1;
	}
	
	SDEBUGOUT("REQUEST READ OK:\r\nwtag=%d\r\nctrid=%d\r\naddr=%d\r\nlen=%d\r\nwlun=%d\r\n", 
			header->wtag, header->ctrid, header->addr, header->len, header->wlun);
	
	return 0;
}

static int usStorage_diskREAD(struct scsi_head *header)
{
	uint32_t size = 0;
	uint8_t *buffer = NULL;

	if(!header){
		SDEBUGOUT("usStorage_diskREAD Parameter Error\r\n");
		return 1;
	}

	if(!header->len){
		SDEBUGOUT("usStorage_diskREAD 0Bytes\r\n");
		/*Write to Phone*/
		header->relag = 1;
		usStorage_sendHEAD(header);	
		return 1;
	}
	
	if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
		SDEBUGOUT("usStorage_diskREAD Failed\r\n");
		return 1;
	}
	if(size < header->len+PRO_HDR){
		SDEBUGOUT("Use usStorage_diskMULREAD To Send[%d/%d]\r\n",
					header->len+PRO_HDR, size);
		return usStorage_diskMULREAD(header);
	}

	return usStorage_diskSIGREAD(header);
}

static int usStorage_diskWRITE(uint8_t *buffer, uint32_t recvSize, struct scsi_head *header)
{
	uint32_t hSize = recvSize;
	uint32_t paySize, curSize = 0, secSize = 0, sdivSize = 0;
	int32_t addr;	
	uint8_t sector[USDISK_SECTOR] = {0};

	if(!buffer || !header){
		SDEBUGOUT("usStorage_diskWRITE Parameter Error\r\n");
		return 1;
	}
	if(!header->len){
		SDEBUGOUT("usStorage_diskWRITE Length is 0\r\n");
		return 0;
	}
	addr = header->addr;
	/*Write the first payload*/
	paySize= recvSize-PRO_HDR;
	if((secSize = OP_MOD(paySize))){
		memcpy(sector, buffer+PRO_HDR+OP_DIV(paySize)*USDISK_SECTOR, 
					secSize);
		SDEBUGOUT("REQUEST WRITE: InComplete Sector Detect[BEGIN]:%d/%dBytes\r\n", 
								secSize, USDISK_SECTOR);
	}
	if((sdivSize = OP_DIV(paySize)) && 
			usDisk_DiskWriteSectors(buffer+PRO_HDR, addr, sdivSize)){
		SDEBUGOUT("REQUEST WRITE Error[addr:%d  SectorCount:%d]\r\n",
						addr, sdivSize);
		/*Write to Phone*/
		header->relag = 1;
		usStorage_sendHEAD(header);	
		return 1;
	}
	addr += sdivSize;
	curSize = paySize-secSize;
	SDEBUGOUT("REQUEST WRITE: PART INFO: addr:%d curSize:%dBytes %d/%dBytes\r\n", 
							addr, curSize, secSize, paySize);	
	while(curSize < header->len){
		uint32_t secCount = 0;
		uint8_t *ptr = NULL, *pbuffer = NULL;
		if(usProtocol_RecvPackage((void **)&pbuffer, hSize, &paySize)){
			SDEBUGOUT("usProtocol_RecvPackage Failed\r\n");
			/*Write to Phone*/
			header->relag = 1;
			usStorage_sendHEAD(header);			
			return 1;
		}
		/*add handle size*/		
		hSize+= paySize;
		ptr = pbuffer;
		if(secSize){
			SDEBUGOUT("REQUEST WIRTE: Handle InComplete Sector[%dBytes Payload:%dBytes]\r\n",
					secSize, paySize);
			if(paySize < USDISK_SECTOR-secSize){
				memcpy(sector+secSize, ptr, paySize);
				curSize += paySize;
				secSize += paySize;
				SDEBUGOUT("REQUEST WIRTE: PayLoad Not Enough Fill Sector[update to %dBytes CurSize:%dByte Payload:%dBytes]\r\n",
						secSize, curSize, paySize);
				continue;
			}
			memcpy(sector+secSize, ptr, USDISK_SECTOR-secSize);
			ptr += (USDISK_SECTOR-secSize);
			/*Write to disk*/
			if(usDisk_DiskWriteSectors(sector, addr, 1)){
				SDEBUGOUT("REQUEST WRITE Last Sector Error[addr:%d SectorCount:%d]\r\n",
							addr, secCount);
				/*Write to Phone*/
				header->relag = 1;
				usStorage_sendHEAD(header);				
				return 1;
			}
			/*add var*/
			addr++;
			curSize += USDISK_SECTOR;
			paySize -= (USDISK_SECTOR-secSize);
			secSize = 0;
			SDEBUGOUT("REQUEST WIRTE: Handle InComplete Sector OK[Payload:%dBytes]\r\n",
								paySize);			
		}
		
		secCount = OP_DIV(paySize);
		if(!secSize && (secSize = OP_MOD(paySize))){
			SDEBUGOUT("REQUEST WRITE: InComplete Sector Detect [LAST]:%d/%dBytes\r\n", 
									secSize, USDISK_SECTOR);
			memcpy(sector, ptr+secCount*USDISK_SECTOR, secSize);
		}
		/*Write to disk*/
		if(secCount && usDisk_DiskWriteSectors(ptr, addr, secCount)){
			SDEBUGOUT("REQUEST WRITE Error[addr:%d	SectorCount:%d]\r\n",
							addr, sdivSize);
			/*Write to Phone*/
			header->relag = 1;
			usStorage_sendHEAD(header);				
			return 1;
		}
		/*Add var*/
		addr += secCount;
		curSize += secCount*USDISK_SECTOR;
	}

	/*Write to Phone*/
	if(usStorage_sendHEAD(header)){
		SDEBUGOUT("Error Send Header\r\n");
		return 1;
	}

	SDEBUGOUT("REQUEST WRITE FINISH:\r\nwtag=%d\r\nctrid=%d\r\naddr=%d\r\nlen=%d\r\nwlun=%d\r\n", 
			header->wtag, header->ctrid, header->addr, header->len, header->wlun);
	return 0;
}

static int usStorage_diskINQUIRY(struct scsi_head *header)
{
	uint8_t *buffer = NULL;
	uint32_t size = 0, total = 0;
	struct scsi_inquiry_info dinfo;
	
	if(!header){
		SDEBUGOUT("usStorage_diskINQUIRY Parameter Failed\r\n");
		return 1;
	}
	if(header->len != sizeof(struct scsi_inquiry_info)){
		SDEBUGOUT("usStorage_diskINQUIRY Parameter Error:[%d/%d]\r\n",
					header->len, sizeof(struct scsi_inquiry_info));
		return 1;
	}
	if(usDisk_DiskInquiry(&dinfo)){
		SDEBUGOUT("usDisk_DiskInquiry  Error\r\n");
		return 1;
	}
	if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
		SDEBUGOUT("usProtocol_GetAvaiableBuffer Failed\r\n");
		return 1;
	}
	memcpy(buffer, header, PRO_HDR);
	memcpy(buffer+PRO_HDR, &dinfo,  sizeof(struct scsi_inquiry_info));
	total = PRO_HDR+sizeof(struct scsi_inquiry_info);
	
	if(usProtocol_SendPackage(buffer, total)){
		SDEBUGOUT("usStorage_diskINQUIRY Failed\r\n");
		return 1;
	}
	
	SDEBUGOUT("usStorage_diskINQUIRY Successful\r\nDisk INQUIRY\r\nSize:%lld\r\nVendor:%s\r\nProduct:%s\r\nVersion:%s\r\nSerical:%s\r\n", 
				dinfo.size, dinfo.vendor, dinfo.product, dinfo.version, dinfo.serial);
	
	return 0;
}

static int usStorage_diskLUN(struct scsi_head *header)
{
	uint8_t num = usDisk_DiskNum();
	uint8_t *buffer = NULL;
	uint32_t size = 0, total = 0;

	if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
		SDEBUGOUT("usProtocol_GetAvaiableBuffer Failed\r\n");
		return 1;
	}	
	SDEBUGOUT("AvaiableBuffer 0x%p[%dBytes][Disk:%d]\r\n", 
				buffer, size, num);
	
	total = sizeof(struct scsi_head);
	memcpy(buffer, header, total);
	memcpy(buffer+total, &num, 1);
	total += 1;
	
	if(usProtocol_SendPackage(buffer, total)){
		SDEBUGOUT("usProtocol_SendPackage Failed\r\n");
		return 1;
	}
	SDEBUGOUT("usStorage_diskLUN Successful[DiskNumber %d]\r\n", num);
	return 0;
}

static int usStorage_firmwareINFO(struct scsi_head *header)
{
	vs_acessory_parameter dinfo;	
	int flen = sizeof(vs_acessory_parameter);
	uint8_t *buffer = NULL;
	uint32_t size = 0, total = 0;

	if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
		SDEBUGOUT("usProtocol_GetAvaiableBuffer Failed\r\n");
		return 1;
	}	
	SDEBUGOUT("AvaiableBuffer 0x%p[%dBytes]\r\n", buffer, size);
	
	total = sizeof(struct scsi_head);
	memcpy(buffer, header, total);
	memset(&dinfo, 0, sizeof(vs_acessory_parameter));
	strcpy(dinfo.fw_version, "1.0");
	strcpy(dinfo.hw_version, "1.0");
	strcpy(dinfo.manufacture, "szitman");
	strcpy(dinfo.model_name, "nxp");
	strcpy(dinfo.sn, "1234567890");
	strcpy(dinfo.license, "1234567890");
	memcpy(buffer+total, &dinfo, flen);
	total += flen;
	
	if(usProtocol_SendPackage(buffer, total)){
		SDEBUGOUT("usProtocol_SendPackage Failed\r\n");
		return 1;
	}
	SDEBUGOUT("usStorage_firmwareINFO Successful Firmware Info:\r\nVendor:%s\r\nProduct:%s\r\nVersion:%s\r\nSerical:%s\r\nLicense:%s\r\n", 
					dinfo.manufacture, dinfo.model_name, dinfo.fw_version, dinfo.sn, dinfo.license);
	
	return 0;
}


static int usStorage_Handle(void)
{	
	uint8_t *buffer;
	uint32_t size=0;
	struct scsi_head header;

	if(usProtocol_RecvPackage((void **)&buffer, 0, &size)){
		SDEBUGOUT("usProtocol_RecvPackage Failed\r\n");
		return 1;
	}
	if(size < PRO_HDR){
		SDEBUGOUT("usProtocol_RecvPackage Too Small [%d]Bytes\r\n", size);
		return 1;
	}
	/*Must save the header, it will be erase*/
	memcpy(&header, buffer, PRO_HDR);
	SDEBUGOUT("usProtocol_RecvPackage [%d/%d]Bytes\r\n", 
				header.len, size);
	/*Handle Package*/
	SDEBUGOUT("RQUEST:\r\nwtag=%d\r\nctrid=%d\r\naddr=%d\r\nlen=%d\r\nwlun=%d\r\n", 
			header.wtag, header.ctrid, header.addr, header.len, header.wlun);

	switch(header.ctrid){
		case SCSI_READ:
			usStorage_diskREAD(&header);
			break;
		case SCSI_WRITE:		
			usStorage_diskWRITE(buffer, size, &header);
			break;
		case SCSI_INQUIRY:
			usStorage_diskINQUIRY(&header);
			break;
		case SCSI_GET_LUN:
			usStorage_diskLUN(&header);
			break;
		case SCSI_FIRMWARE_INFO:
			usStorage_firmwareINFO(&header);
			break;
		default:
			SDEBUGOUT("Unhandle Command\r\n");
	}

	return 0;
}


/*****************************************************************************
 * Public functions
 ****************************************************************************/

#if defined(NXP_CHIP_18XX)
/** Main program entry point. This routine configures the hardware required by the application, then
 *  calls the filesystem function to read files from USB Disk
 *Ustorage Project by Szitman 20161022
 */
void vs_main(void *pvParameters)
{
	SetupHardware();

	SDEBUGOUT("U-Storage Running.\r\n");
	while (USB_HostState[1] != HOST_STATE_Configured) {
		USB_USBTask(1, USB_MODE_Host);
		continue;
	}	
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
		SDEBUGOUT(("Disk Attached on port %d\r\n"), corenum);	
	}else{
		SDEBUGOUT(("Phone Attached on port %d\r\n"), corenum);	
	}
}

/** Event handler for the USB_DeviceUnattached event. This indicates that a device has been removed from the host, and
 *  stops the library USB task management process.
 */
void EVENT_USB_Host_DeviceUnattached(const uint8_t corenum)
{
	SDEBUGOUT(("\r\nDevice Unattached on port %d\r\n"), corenum);
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
		SDEBUGOUT(("Disk Enumeration on port %d\r\n"), corenum);
		usDisk_DeviceDetect(&UStorage_Interface[corenum]);
	}else if(corenum == 0){
		SDEBUGOUT(("Phone Enumeration on port %d\r\n"), corenum);
		usProtocol_DeviceDetect(&UStorage_Interface[corenum]);	
	}else{
		SDEBUGOUT("Unknown USB Port %d.\r\n", corenum);
	}
}
/** Event handler for the USB_HostError event. This indicates that a hardware error occurred while in host mode. */
void EVENT_USB_Host_HostError(const uint8_t corenum, const uint8_t ErrorCode)
{
	USB_Disable(corenum, USB_MODE_Host);

	SDEBUGOUT(("Host Mode Error\r\n"
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
	SDEBUGOUT(("Dev Enum Error\r\n"
			  " -- Error port %d\r\n"
			  " -- Error Code %d\r\n"
			  " -- Sub Error Code %d\r\n"
			  " -- In State %d\r\n" ),
			 corenum, ErrorCode, SubErrorCode, USB_HostState[corenum]);

}
#elif defined(LINUX)
int main(int argc, char **argv)
{
	static int	initd = 0;
	
	SDEBUGOUT("U-Storage Running.\r\n");
	while(1){
		if (initd == 0 && (usProtocol_DeviceDetect(NULL) ||
					usDisk_DeviceDetect("/dev/sda"))) {
			SDEBUGOUT("Wait Phone OK.\r\n");
			usleep(500000);
			continue;
		}else{
			initd = 1;
		}
		
		/*Connect Phone Device*/
		if(usProtocol_ConnectPhone()){
			/*Connect to Phone Failed*/
			sleep(5);
			continue;
		}
		usStorage_Handle();
	}
}
#endif //#if defined(NXP_CHIP_18XX)

