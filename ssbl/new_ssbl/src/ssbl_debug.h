#ifndef __SSBL_DEBUG_H__
#define __SSBL_DEBUG_H__

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/
#include "xparameters.h"
#include "xil_printf.h"

/************************** Constant Definitions *****************************/
/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/
/*
 * LOG_ERR	总是输出
 * LOG_INFO	只有在FSBL_PRINT_INFO被定义时才输出
 */
#define LOG_ERR		0x00000001
#define LOG_INFO	0x00000002

//#define ssbl_dbg_current_types 0
//#define ssbl_dbg_current_types (LOG_ERR)
#define ssbl_dbg_current_types (LOG_ERR | LOG_INFO)


#ifdef STDOUT_BASEADDRESS
#define ssbl_printf(type,...) \
        if (((type) & ssbl_dbg_current_types))  {xil_printf("[SSBL] " __VA_ARGS__); }
#else
#define ssbl_printf(type, ...)
#endif

#ifdef __cplusplus
}
#endif





#endif /* __FSBL_DEBUG_H__ */
