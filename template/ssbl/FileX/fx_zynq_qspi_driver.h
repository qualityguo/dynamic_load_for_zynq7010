#ifndef __FX_ZYNQ_QSPI_DRIVER_H_
#define __FX_ZYNQ_QSPI_DRIVER_H_

#include "fx_api.h"

#define FX_QSPI_META_SECTORS            8				// 保留前8个扇区作为元数据扇区		FileX的扇区0在LevelX的扇区8

VOID fx_zynq_qspi_driver(FX_MEDIA *media_ptr);

#endif /* __FX_ZYNQ_QSPI_DRIVER_H_ */
