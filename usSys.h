#ifndef __USSYS_H_
#define __USSYS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


//#define NXP_CHIP_18XX 1
#define LINUX   1
#ifndef DEBUG_ENABLE
#define DEBUG_ENABLE    1
#endif

void die(uint8_t rc);

#ifdef __cplusplus
}
#endif
#endif

