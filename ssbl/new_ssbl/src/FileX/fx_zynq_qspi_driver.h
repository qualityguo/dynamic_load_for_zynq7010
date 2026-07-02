#ifndef __FX_ZYNQ_QSPI_DRIVER_H_
#define __FX_ZYNQ_QSPI_DRIVER_H_

#include "fx_api.h"

/*
 * LevelX 逻辑扇区前保留的扇区数，FileX 的 boot sector 存放在 LevelX 逻辑扇区
 * FX_QSPI_META_SECTORS 处。fx_media_format 与 fx_media_open 的 hidden_sectors
 * 必须保持一致，否则 format 写入与 open 读取的扇区地址会错位。
 */
#define FX_QSPI_META_SECTORS            8

VOID fx_zynq_qspi_driver(FX_MEDIA *media_ptr);

#endif /* __FX_ZYNQ_QSPI_DRIVER_H_ */
