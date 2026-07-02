#ifndef __LX_ZYNQ_QSPI_FLASH_DRIVER_H_
#define __LX_ZYNQ_QSPI_FLASH_DRIVER_H_

#include "lx_api.h"


/* 初始化QSPI-Flash */
int  lx_qspi_hardware_init(void);

/* 擦除 LevelX 管理区域（QSPI_BOOT_OFFSET 起） */
void lx_qspi_erase_levelx_region(void);

/* 获得扇区数量 */
ULONG lx_qspi_get_total_sectors(void);


UINT lx_qspi_nor_initialize(LX_NOR_FLASH *nor_flash);

#endif /* __LX_ZYNQ_QSPI_FLASH_DRIVER_H_ */
