/*
 * @brief U-Storage Project
 *
 * @note
 * Copyright(C) i4season, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 */
#include <stdint.h>
#include <stdio.h>
#if defined(NXP_CHIP_18XX)
#include "MassStorageHost.h"
#endif
/* Function to spin forever when there is an error */
void die(uint8_t rc)
{
	printf("*******DIE %d*******\r\n", rc);
	while (1) {}/* Spin for ever */
}

