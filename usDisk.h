/*
 * @note
 * Copyright(C) i4season U-Storage, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 *
 */


#ifndef __USDISK_H_
#define __USUSB_H_

#include <ctype.h>
#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t usDisk_DeviceDetect(void *os_priv);
uint8_t usDisk_DeviceDisConnect(void);
uint8_t usDisk_DiskReadSectors(void *buff, uint32_t secStart, uint32_t numSec);
uint8_t usDisk_DiskWriteSectors(void *buff, uint32_t secStart, uint32_t numSec);
uint8_t usDisk_DiskNum(void);

#ifdef __cplusplus
}
#endif

#endif /* __USBDISK_H_ */

