#ifndef __FSBL_DEBUG_H__
#define __FSBL_DEBUG_H__

#include "xparameters.h"

#ifdef __cplusplus
extern "C" {
#endif


/* 打印DEBUG+INFO */
// #define FSBL_PRINT_DEBUG
/* 打印INFO */
// #define FSBL_PRINT_INFO
/* 都不定义则什么都不打印 */

#define LOG_DEBUG   0x00000001
#define LOG_INFO    0x00000002

#if defined (FSBL_PRINT_DEBUG)
#define fsbl_dbg_current_types ((LOG_INFO) | (LOG_DEBUG))
#elif defined (FSBL_PRINT_INFO)
#define fsbl_dbg_current_types (LOG_INFO)
#else
#define fsbl_dbg_current_types 0x00000000
#endif

#ifdef STDOUT_BASEADDRESS
#define fsbl_printf(type,...) \
        if (((type) & fsbl_dbg_current_types))  {xil_printf (__VA_ARGS__); }    
#else
#define fsbl_printf(type, ...)
#endif

#ifdef __cplusplus
}
#endif





#endif /* __FSBL_DEBUG_H__ */