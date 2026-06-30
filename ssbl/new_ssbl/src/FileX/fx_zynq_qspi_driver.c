/***************************** Include Files *********************************/
#include "fx_zynq_qspi_driver.h"
#include "lx_api.h"
#include "lx_zynq_qspi_flash_driver.h"

/************************** Constant Definitions *****************************/

/*
 * FileX 扇区大小 = 512 字节，与 LevelX 逻辑扇区（LX_NOR_SECTOR_SIZE * 4）一致，
 * 因此 FileX 逻辑扇区号与 LevelX 逻辑扇区号一一对应，无需换算。
 */
#define FX_SECTOR_SIZE                  512

/************************** Variable Definitions *****************************/

/* LevelX NOR 实例（磨损均衡由其在内部完成） */
static LX_NOR_FLASH  g_lx_nor_flash;


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
		if (lx_qspi_hardware_init() != 0) {
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		if (lx_nor_flash_open(&g_lx_nor_flash, "qspi_nor",
				lx_qspi_nor_initialize) != LX_SUCCESS) {
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;

	/*
	 * 读 boot sector（LevelX 逻辑扇区 0）
	 */
	case FX_DRIVER_BOOT_READ:
		if (lx_nor_flash_sector_read(&g_lx_nor_flash, 0,
				media_ptr->fx_media_driver_buffer) != LX_SUCCESS) {
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		media_ptr->fx_media_hidden_sectors = 0;
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;

	/*
	 * 写 boot sector（LevelX 逻辑扇区 0）
	 */
	case FX_DRIVER_BOOT_WRITE:
		if (lx_nor_flash_sector_write(&g_lx_nor_flash, 0,
				media_ptr->fx_media_driver_buffer) != LX_SUCCESS) {
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;

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
		lx_nor_flash_close(&g_lx_nor_flash);
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
