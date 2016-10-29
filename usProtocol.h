/*
 * @note
 * Copyright(C) i4season U-Storage, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 *
 */

#ifndef __USPROTOCOL_H_
#define __USPROTOCOL_H_

#include <ctype.h>
#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
#define min(x, y)				(((x) < (y)) ? (x) : (y))

#define htons(A)        ((((uint16_t)(A) & 0xff00) >> 8) | \
                                                   (((uint16_t)(A) & 0x00ff) << 8))

#define htonl(A)        ((((uint32_t)(A) & 0xff000000) >> 24) | \
                                                   (((uint32_t)(A) & 0x00ff0000) >> 8) | \
                                                   (((uint32_t)(A) & 0x0000ff00) << 8) | \
                                                   (((uint32_t)(A) & 0x000000ff) << 24))
#define ntohs(A)		htons(A)
#define ntohl(A)		htonl(A)

struct scsi_head{
	int32_t head;	/*Receive OR Send*/
	int32_t wtag; /*Task ID*/
	int32_t ctrid; /*Command ID*/
	int32_t addr; /*Offset addr*512   represent sectors */
	int32_t len;
	int16_t wlun;
	int16_t relag; /*Response Code*/
};

#define SCSI_HEAD_SIZE			sizeof(struct scsi_head)
#define SCSI_PHONE_MAGIC		 0xccddeeff
#define SCSI_DEVICE_MAGIC		 0xaabbccdd
#define SCSI_WFLAG  1 << 7

enum {
  SCSI_TEST = 0,
  SCSI_READ  = 1,//28
  SCSI_WRITE = 2 | SCSI_WFLAG,//2a
  SCSI_INQUIRY = 3,//12
  SCSI_READ_CAPACITY =4,//25
  SCSI_GET_LUN = 5,
  SCSI_INPUT = 6,
  SCSI_OUTPUT = 7,
};


uint8_t usProtocol_DeviceDetect(void *os_priv);
uint8_t usProtocol_ConnectPhone(void);
uint8_t usProtocol_DeviceDisConnect(void);
uint8_t usProtocol_SendPackage(void *buffer, uint32_t size);
uint8_t usProtocol_RecvPackage(void **buffer, uint32_t tsize, uint32_t *rsize);
uint8_t usProtocol_GetAvaiableBuffer(void **buffer, uint32_t *size);

#ifdef __cplusplus
}
#endif

#endif /* __USPROTOCOL_H_ */
