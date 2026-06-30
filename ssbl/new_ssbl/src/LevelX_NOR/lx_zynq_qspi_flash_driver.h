#ifndef __LX_ZYNQ_QSPI_FLASH_DRIVER_H_
#define __LX_ZYNQ_QSPI_FLASH_DRIVER_H_

#include "lx_api.h"

/*
 * LevelX NOR 驱动对接 Zynq QSPI Flash 的硬件相关胶水层。
 * 本文件随应用工程编译（依赖 XQspiPs），不属于通用 LevelX 静态库。
 */

/* 初始化 QSPI 控制器硬件（XQspiPs）。成功返回 0，失败返回 -1。 */
int  lx_qspi_hardware_init(void);

/*
 * LevelX NOR 实例初始化：设置 Flash 几何参数、注册驱动回调、提供扇区缓冲。
 * 由 lx_nor_flash_open() 回调，应用无需直接调用。
 */
UINT lx_qspi_nor_initialize(LX_NOR_FLASH *nor_flash);

#endif /* __LX_ZYNQ_QSPI_FLASH_DRIVER_H_ */
