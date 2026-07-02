/***************************** Include Files *********************************/
#include "fx_zynq_qspi_driver.h"
#include "lx_api.h"
#include "ssbl.h"
#include "lx_zynq_qspi_flash_driver.h"

/************************** Constant Definitions *****************************/
#define FX_SECTOR_SIZE                  512				// FileX的扇区大小=512B 等于LevelX的逻辑扇区大小

/************************** Variable Definitions *****************************/
static LX_NOR_FLASH  g_lx_nor_flash;
static int g_lx_opened = 0;								// 实例被打开标志



VOID fx_zynq_qspi_driver(FX_MEDIA *media_ptr)
{
	ULONG sector;
	ULONG count;
	ULONG i;
	UCHAR *buf;

	switch (media_ptr->fx_media_driver_request)
	{
	case FX_DRIVER_INIT:
	{
		int  hwrc;
		UINT lxrc;
		/* 如果已经初始化了直接返回 */
		if (g_lx_opened) {
			media_ptr->fx_media_driver_status = FX_SUCCESS;
			break;
		}
		/* QSPI初始化 */
		hwrc = lx_qspi_hardware_init();
		if (hwrc != 0) {
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		/* levelx-open */
		lxrc = lx_nor_flash_open(&g_lx_nor_flash, "qspi_nor",
				lx_qspi_nor_initialize);
		if (lxrc != LX_SUCCESS) {
			/* 擦空重试 */
			lx_qspi_erase_levelx_region();
			lxrc = lx_nor_flash_open(&g_lx_nor_flash, "qspi_nor",
					lx_qspi_nor_initialize);
		}
		if (lxrc != LX_SUCCESS) {
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		/* 标记初始化完成 */
		g_lx_opened = 1;
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;
	}

	/* BOOT扇区从FX_QSPI_META_SECTORS开始读 */
	case FX_DRIVER_BOOT_READ:
		if (lx_nor_flash_sector_read(&g_lx_nor_flash, FX_QSPI_META_SECTORS,
				media_ptr->fx_media_driver_buffer) != LX_SUCCESS) {
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		media_ptr->fx_media_hidden_sectors = FX_QSPI_META_SECTORS;
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;

	/* BOOT扇区从FX_QSPI_META_SECTORS开始写 */
	case FX_DRIVER_BOOT_WRITE:
		if (lx_nor_flash_sector_write(&g_lx_nor_flash, FX_QSPI_META_SECTORS,
				media_ptr->fx_media_driver_buffer) != LX_SUCCESS) {
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;

	/* 读扇区 */
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

	/* 写扇区 */
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

	/* 释放扇区 */
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
	/* 逆初始化 */
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
