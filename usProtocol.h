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

#ifdef __cplusplus
extern "C" {
#endif

typedef struct{
	uint8_t usbnum; /*USB Number*/
	void *InterfaceInfo;
	void *ConfigDescriptorData;
	uint16_t ConfigDescriptorSize;
}nxp_aoa;

#ifdef __cplusplus
}
#endif

#endif /* __USPROTOCOL_H_ */
