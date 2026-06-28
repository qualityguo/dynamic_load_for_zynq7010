#ifndef  __SSBL_H__
#define  __SSBL_H__

/*
*********************************************************************************************************
*                                         标准库
*********************************************************************************************************
*/
#include  <stdarg.h>
#include  <stdio.h>
#include  <stdlib.h>
#include  <math.h>

/*
*********************************************************************************************************
*                                           OS
*********************************************************************************************************
*/
#include "tx_api.h"
#include "fx_api.h"

/*
*********************************************************************************************************
*                                        APP / BSP
*********************************************************************************************************
*/
#include "ssbl_debug.h"
#include "bsp.h"

#include "bitstream_loader.h"
#include "app_loader.h"
#include "boot_config.h"
#include "boot_run.h"
#include "handoff.h"

#include "fx_zynq_sdio_driver.h"
#include "fx_zynq_qspi_driver.h"

#include "cb.h"
#include "key_driver.h"
#include "led_driver.h"
#include "uart_driver.h"

#include "shell.h"

#include "xparameters.h"
#include "xscugic.h"
#include "xscutimer.h"
#include "xgpiops.h"
#include "xuartps.h"
#include "xparameters_ps.h"
#include "xil_cache.h"
#include "xil_io.h"
#include "xdevcfg.h"
#include "xdevcfg_hw.h"



/*
*********************************************************************************************************
*                                          变量和函数
*********************************************************************************************************
*/


/*
*******************************************************************************************************
*                               			宏
*******************************************************************************************************
*/
#define BOOT_MODE_REG			(XPS_SYS_CTRL_BASEADDR + 0x25C)
#define BOOT_MODES_MASK			0x00000007 /**< FLASH types */
#define QSPI_MODE			0x00000001 /**< QSPI Boot Mode */
#define SD_MODE				0x00000005 /**< SD Boot Mode */

#define	TRIGGER_FLAG_KEY0			(1u << 0)			// 按键0按下事件-触发trigger任务
#define	TRIGGER_FLAG_UART1			(1u << 1)			// 串口1收数事件-触发trigger任务
#define	TRIGGER_FLAG_NET			(1u << 2)
#define	TRIGGER_FLAG_PROCESSED		(1u << 3)			// trigger验证通过-通知Start任务
#define	TRIGGER_FLAG_PROCESSED		(1u << 3)			// trigger任务验证通过-触发shell任务



#endif /* __SSBL_H__ */
