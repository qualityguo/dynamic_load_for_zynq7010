/*
 * fx_zynq_sdio_driver.c - FileX port layer for ZYNQ SD card
 *
 * Thin adapter between FileX media driver interface and the
 * board-level SD block device driver (sd_driver.c / "sd0").
 *
 * Controller init is done in board_init.c; this layer only
 * dispatches FileX requests to device_ioctl.
 */

#include "fx_zynq_sdio_driver.h"
#include "device_core.h"
#include "ioctl_cmd.h"

static struct device *sd_dev;

static int sd_read_sectors(ULONG sector, UINT count, void *buf)
{
	struct sd_rw_args args = { .sector = sector, .count = count, .buf = buf };
	int ret = device_ioctl(sd_dev, SD_IOCTL_READ_SECTORS, &args);
	return ret;
}

static int sd_write_sectors(ULONG sector, UINT count, void *buf)
{
	struct sd_rw_args args = { .sector = sector, .count = count, .buf = buf };
	int ret = device_ioctl(sd_dev, SD_IOCTL_WRITE_SECTORS, &args);
	return ret;
}

VOID fx_zynq_sd_driver(FX_MEDIA *media_ptr)
{
	ULONG partition_start;

	/* 要挂载的分区的绝对起始 LBA（0 = 整盘 superfloppy / 第一个分区）。
	 * 由 fx_media_open() 的 driver_info 参数传入：sd_port.c 已自行解析 MBR
	 * 得到 P2 起始 LBA，直接传过来。这样就不依赖 FileX 自带的
	 * _fx_partition_offset_calculate——后者会因 MBR 首 3 字节是 "EB xx 90"
	 * 跳转指令而误判 MBR 为 boot sector，从而返回 start=0。
	 */
	partition_start = 0;
	if (media_ptr->fx_media_driver_info != NULL) {
		partition_start = *(ULONG *)media_ptr->fx_media_driver_info;
	}

	switch (media_ptr->fx_media_driver_request)
	{
	case FX_DRIVER_READ:
		if (sd_read_sectors(media_ptr->fx_media_driver_logical_sector +
				media_ptr->fx_media_hidden_sectors,
				media_ptr->fx_media_driver_sectors,
				media_ptr->fx_media_driver_buffer) != 0) {
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;

	case FX_DRIVER_WRITE:
		if (sd_write_sectors(media_ptr->fx_media_driver_logical_sector +
				 media_ptr->fx_media_hidden_sectors,
				 media_ptr->fx_media_driver_sectors,
				 media_ptr->fx_media_driver_buffer) != 0) {
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;

	case FX_DRIVER_FLUSH:
	case FX_DRIVER_ABORT:
	case FX_DRIVER_UNINIT:
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;

	case FX_DRIVER_INIT:
		sd_dev = device_find("sd0");
		media_ptr->fx_media_driver_status =
			sd_dev ? FX_SUCCESS : FX_IO_ERROR;
		break;

	case FX_DRIVER_BOOT_READ:
		/* 直接读目标分区的第一个扇区（即该分区的 boot sector / BPB）。
		 * partition_start 由 sd_port.c 解析 MBR 得到并经 driver_info 传入。 */
		if (sd_read_sectors(partition_start, 1,
				media_ptr->fx_media_driver_buffer) != 0) {
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		/* 记录分区起始 LBA，后续 FX_DRIVER_READ/WRITE 都要在逻辑扇区号上
		 * 叠加该偏移（FileX 用 fx_media_hidden_sectors 存）。
		 * 注：FileX 在 boot_info_extract 里会用 BPB 的 hidden-sectors 字段
		 * 覆盖本值，对规范格式化的 FAT32 两者一致（都 = 分区起始 LBA）。 */
		media_ptr->fx_media_hidden_sectors = partition_start;
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;

	case FX_DRIVER_BOOT_WRITE:
		if (sd_write_sectors(media_ptr->fx_media_hidden_sectors, 1,
					media_ptr->fx_media_driver_buffer) != 0) {
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;

	default:
		media_ptr->fx_media_driver_status = FX_IO_ERROR;
		break;
	}
}
