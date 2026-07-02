/***************************** Include Files *********************************/
#include "fx_zynq_qspi_driver.h"
#include "lx_api.h"
#include "lx_zynq_qspi_flash_driver.h"
#include "xil_printf.h"
#include <string.h>

/************************** Constant Definitions *****************************/

/*
 * FileX 扇区大小 = 512 字节，与 LevelX 逻辑扇区（LX_NOR_SECTOR_SIZE * 4）一致，
 * 因此 FileX 逻辑扇区号与 LevelX 逻辑扇区号一一对应，无需换算。
 */
#define FX_SECTOR_SIZE                  512

/************************** Variable Definitions *****************************/

/* LevelX NOR 实例（磨损均衡由其在内部完成） */
static LX_NOR_FLASH  g_lx_nor_flash;

/*
 * LevelX 实例是否已打开。fx_media_open 失败后调用 fx_media_format、再 fx_media_open
 * 会让 FileX 多次下发 FX_DRIVER_INIT；若每次都 lx_nor_flash_open 同一实例而不 close，
 * 内部互斥锁/状态会残留脏数据，导致后续 open 返回 LX_ERROR。这里做成幂等：仅首次 INIT
 * 真正打开，UNINIT 时关闭并清标志。
 */
static int g_lx_opened = 0;


/* =========================== FileX 驱动入口 ================================ */

VOID fx_zynq_qspi_driver(FX_MEDIA *media_ptr)
{
	ULONG sector;
	ULONG count;
	ULONG i;
	UCHAR *buf;

	switch (media_ptr->fx_media_driver_request)
	{
	/*
	 * 初始化：先初始化 QSPI 硬件，再打开 LevelX NOR 实例
	 * （lx_nor_flash_open 内部会完成首次格式化 / 磨损均衡状态恢复）
	 */
	case FX_DRIVER_INIT:
	{
		int  hwrc;
		UINT lxrc;

		/* 已打开则直接复用，避免对同一实例重复 open 而不 close */
		if (g_lx_opened) {
			media_ptr->fx_media_driver_status = FX_SUCCESS;
			break;
		}

		hwrc = lx_qspi_hardware_init();
		xil_printf("[LX] lx_qspi_hardware_init = %d\r\n", hwrc);
		if (hwrc != 0) {
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		lxrc = lx_nor_flash_open(&g_lx_nor_flash, "qspi_nor",
				lx_qspi_nor_initialize);
		xil_printf("[LX] lx_nor_flash_open = 0x%x\r\n", (unsigned)lxrc);
		if (lxrc != LX_SUCCESS) {
			xil_printf("[LX] open failed, erasing LevelX region...\r\n");
			lx_qspi_erase_levelx_region();
			/*
			 * 首次失败的 open 可能已在该实例上创建了互斥锁 / 写入了 state 字段，
			 * 直接重试 open 会被 LevelX 判为"实例已打开"或互斥锁创建失败而返回
			 * LX_ERROR。清零整个实例，使重试看到全新对象（id=0/state=0）。
			 * lx_qspi_nor_initialize 回调会在 open 中重新写入几何参数与缓冲指针。
			 */
			memset(&g_lx_nor_flash, 0, sizeof(g_lx_nor_flash));
			xil_printf("[LX] retry open...\r\n");
			lxrc = lx_nor_flash_open(&g_lx_nor_flash, "qspi_nor",
					lx_qspi_nor_initialize);
			xil_printf("[LX] lx_nor_flash_open = 0x%x\r\n", (unsigned)lxrc);
		}
		if (lxrc != LX_SUCCESS) {
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		g_lx_opened = 1;
		xil_printf("[LX] free=%lu mapped=%lu total=%lu\r\n",
				(unsigned long)g_lx_nor_flash.lx_nor_flash_free_physical_sectors,
				(unsigned long)g_lx_nor_flash.lx_nor_flash_mapped_physical_sectors,
				(unsigned long)g_lx_nor_flash.lx_nor_flash_total_physical_sectors);
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;
	}

	/*
	 * 读 boot sector（跳过 LevelX 元数据区，从逻辑扇区 FX_QSPI_META_SECTORS 开始）
	 */
	case FX_DRIVER_BOOT_READ:
		if (lx_nor_flash_sector_read(&g_lx_nor_flash, FX_QSPI_META_SECTORS,
				media_ptr->fx_media_driver_buffer) != LX_SUCCESS) {
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		media_ptr->fx_media_hidden_sectors = FX_QSPI_META_SECTORS;
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;

	/*
	 * 写 boot sector（跳过 LevelX 元数据区，从逻辑扇区 FX_QSPI_META_SECTORS 开始）
	 */
	case FX_DRIVER_BOOT_WRITE:
	{
		UINT lxrc = lx_nor_flash_sector_write(&g_lx_nor_flash, FX_QSPI_META_SECTORS,
				media_ptr->fx_media_driver_buffer);
		if (lxrc != LX_SUCCESS) {
			xil_printf("[LX] BOOT_WRITE fail rc=%u free=%lu mapped=%lu\r\n",
					(unsigned)lxrc,
					(unsigned long)g_lx_nor_flash.lx_nor_flash_free_physical_sectors,
					(unsigned long)g_lx_nor_flash.lx_nor_flash_mapped_physical_sectors);
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;
	}

	/*
	 * 读扇区：逐个调用 LevelX 读
	 */
	case FX_DRIVER_READ:
		sector = media_ptr->fx_media_driver_logical_sector
				+ media_ptr->fx_media_hidden_sectors;
		count  = media_ptr->fx_media_driver_sectors;
		buf    = media_ptr->fx_media_driver_buffer;

		media_ptr->fx_media_driver_status = FX_SUCCESS;
		for (i = 0; i < count; i++) {
			if (lx_nor_flash_sector_read(&g_lx_nor_flash, sector + i,
					buf + i * FX_SECTOR_SIZE) != LX_SUCCESS) {
				media_ptr->fx_media_driver_status = FX_IO_ERROR;
				break;
			}
		}
		break;

	/*
	 * 写扇区：逐个调用 LevelX 写
	 * LevelX 会自动分配新物理扇区并把旧扇区标记为失效，实现磨损均衡
	 */
	case FX_DRIVER_WRITE:
		sector = media_ptr->fx_media_driver_logical_sector
				+ media_ptr->fx_media_hidden_sectors;
		count  = media_ptr->fx_media_driver_sectors;
		buf    = media_ptr->fx_media_driver_buffer;

		media_ptr->fx_media_driver_status = FX_SUCCESS;
		for (i = 0; i < count; i++) {
			if (lx_nor_flash_sector_write(&g_lx_nor_flash, sector + i,
					buf + i * FX_SECTOR_SIZE) != LX_SUCCESS) {
				xil_printf("[LX] sector_write fail @lx%lu\r\n", (unsigned long)(sector + i));
				media_ptr->fx_media_driver_status = FX_IO_ERROR;
				break;
			}
		}
		break;

	/*
	 * 释放扇区：通知 LevelX 这些逻辑扇区已失效
	 * 这是触发块回收（garbage collect）与磨损均衡的关键入口
	 */
	case FX_DRIVER_RELEASE_SECTORS:
		sector = media_ptr->fx_media_driver_logical_sector
				+ media_ptr->fx_media_hidden_sectors;
		count  = media_ptr->fx_media_driver_sectors;

		media_ptr->fx_media_driver_status = FX_SUCCESS;
		for (i = 0; i < count; i++) {
			if (lx_nor_flash_sector_release(&g_lx_nor_flash, sector + i)
					!= LX_SUCCESS) {
				media_ptr->fx_media_driver_status = FX_IO_ERROR;
				break;
			}
		}
		break;

	case FX_DRIVER_UNINIT:
		if (g_lx_opened) {
			lx_nor_flash_close(&g_lx_nor_flash);
			g_lx_opened = 0;
		}
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;

	case FX_DRIVER_FLUSH:
	case FX_DRIVER_ABORT:
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;

	default:
		media_ptr->fx_media_driver_status = FX_IO_ERROR;
		break;
	}
}
