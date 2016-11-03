/*
 * @note
 * Copyright(C) i4season U-Storage, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 *
 */


#ifndef __USDISK_H_
#define __USDISK_H_

#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t usDisk_DeviceDetect(void *os_priv);
uint8_t usDisk_DeviceDisConnect(void);
uint8_t usDisk_DiskReadSectors(void *buff, uint32_t secStart, uint32_t numSec);
uint8_t usDisk_DiskWriteSectors(void *buff, uint32_t secStart, uint32_t numSec);
uint8_t usDisk_DiskNum(void);
uint8_t usDisk_DiskInquiry(struct scsi_inquiry_info *inquiry);

#ifdef __cplusplus
}
#endif

#endif /* __USBDISK_H_ */

