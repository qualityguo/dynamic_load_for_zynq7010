/***************************** Include Files *********************************/
#include "fx_zynq_sdio_driver.h"
#include "xsdps.h"
#include "xparameters.h"

/************************** Constant Definitions *****************************/
#define SD_SECTOR_SIZE  512					// 固定扇区512

/************************** Variable Definitions *****************************/
static 	XSdPs	xSdPs;							// SD控制器实例对象
static 	ULONG	p2_start_lba = 0;				// P2分区起始LBA（从MBR解析）
static 	uint8_t	mbr_buf[SD_SECTOR_SIZE];		// 读MBR用缓冲区


/* ============================== MBR 分区解析 ============================== */
/*
 * 读 LBA 0（MBR），解析第 2 个分区表项得到 P2 起始 LBA。
 *
 * MBR 分区表布局（每项 16 字节，从偏移 0x1BE 起）：
 *   第 1 项 = 0x1BE   ← P1（BOOT.BIN，本驱动不挂载）
 *   第 2 项 = 0x1CE   ← P2（数据分区，本驱动挂载）
 *     byte[8..11] = 该分区起始 LBA（little-endian u32）
 *   即 P2 起始 LBA 在 MBR 偏移 0x1D6 处
 *
 * 返回 0=成功，负值=失败。
 */
static int parse_mbr_p2(void)
{
	if (XSdPs_ReadPolled(&xSdPs, 0, 1, mbr_buf) != XST_SUCCESS)
		return -1;

	/* MBR 签名校验 */
	if (mbr_buf[510] != 0x55 || mbr_buf[511] != 0xAA)
		return -2;

	/* P2 起始 LBA（MBR 偏移 0x1D6，4 字节 little-endian） */
	p2_start_lba =   (ULONG)mbr_buf[0x1D6]
		           | ((ULONG)mbr_buf[0x1D7] << 8)
		           | ((ULONG)mbr_buf[0x1D8] << 16)
		           | ((ULONG)mbr_buf[0x1D9] << 24);

	return (p2_start_lba != 0) ? 0 : -3;
}


VOID fx_zynq_sd_driver(FX_MEDIA *media_ptr)
{
	switch (media_ptr->fx_media_driver_request)
	{
	// 初始化SD卡 + 解析P2分区
	case FX_DRIVER_INIT:
	{
		XSdPs_Config *cfg = XSdPs_LookupConfig(XPAR_XSDPS_0_DEVICE_ID);
		if(cfg == NULL)
		{
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		if (XSdPs_CfgInitialize(&xSdPs, cfg, cfg->BaseAddress) != XST_SUCCESS) {
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		if (XSdPs_CardInitialize(&xSdPs) != XST_SUCCESS) {
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		/* 读 MBR，解析 P2 起始 LBA */
		if (parse_mbr_p2() != 0) {
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;
	}

	// 读SD卡
	case FX_DRIVER_READ:
	{
		if (XSdPs_ReadPolled(&xSdPs,
				media_ptr->fx_media_driver_logical_sector + media_ptr->fx_media_hidden_sectors,
				media_ptr->fx_media_driver_sectors,
				media_ptr->fx_media_driver_buffer) != XST_SUCCESS)
		{
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;
	}

	// 写SD卡
	case FX_DRIVER_WRITE:
	{
		if (XSdPs_WritePolled(&xSdPs,
				media_ptr->fx_media_driver_logical_sector + media_ptr->fx_media_hidden_sectors,
				media_ptr->fx_media_driver_sectors,
				media_ptr->fx_media_driver_buffer) != XST_SUCCESS)
		{
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;
	}

	case FX_DRIVER_FLUSH:
	case FX_DRIVER_ABORT:
	case FX_DRIVER_UNINIT:
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;

	// 读P2的boot sector（不读LBA 0，绕开FileX的MBR误判问题）
	case FX_DRIVER_BOOT_READ:
	{
		if (XSdPs_ReadPolled(&xSdPs, p2_start_lba, 1,
				media_ptr->fx_media_driver_buffer) != XST_SUCCESS)
		{
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		/* 记录P2起始LBA，后续READ/WRITE自动叠加该偏移 */
		media_ptr->fx_media_hidden_sectors = p2_start_lba;
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;
	}

	// 写boot sector
	case FX_DRIVER_BOOT_WRITE:
	{
		if (XSdPs_WritePolled(&xSdPs,
				media_ptr->fx_media_hidden_sectors,
				1,
				media_ptr->fx_media_driver_buffer) != XST_SUCCESS)
		{
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;
	}

	default:
		media_ptr->fx_media_driver_status = FX_IO_ERROR;
		break;
	}
}

