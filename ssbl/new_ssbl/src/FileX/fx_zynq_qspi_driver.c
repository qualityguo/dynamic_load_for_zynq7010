/***************************** Include Files *********************************/
#include "fx_zynq_qspi_driver.h"
#include "xqspips.h"
#include "xparameters.h"
#include <string.h>

/************************** Constant Definitions *****************************/

/*
 * Flash 最前面 QSPI_BOOT_OFFSET 字节存放 BOOT.bin，后续空间交给 FileX 管理。
 * QSPI_BOOT_OFFSET 可据此宏调整。
 */
#define QSPI_BOOT_OFFSET                0x100000

#define QSPI_DEVICE_ID                  XPAR_XQSPIPS_0_DEVICE_ID

#define FX_SECTOR_SIZE                  512
#define QSPI_PAGE_SIZE                  256                                 // 页编程最大长度
#define QSPI_BLOCK_SIZE                 4096                                // 4KB 块擦除粒度

/* IO 模式使用的 Flash 命令 */
#define QSPI_CMD_WREN                   0x06                                // 写使能
#define QSPI_CMD_RDSR                   0x05                                // 读状态寄存器
#define QSPI_CMD_PP                     0x02                                // 页编程
#define QSPI_CMD_BE_4K                  0x20                                // 4KB 块擦除
#define QSPI_CMD_FAST_READ              0x0B                                // 快速读

#define QSPI_SR_WIP                     0x01                                // 状态寄存器忙标志

/* 传输开销：命令 + 3 字节地址 + 1 字节 dummy */
#define QSPI_OVERHEAD                   5

/************************** Variable Definitions *****************************/
static XQspiPs  qspi_instance;                                              // QSPI 控制器实例

/* 读-改-写 工作缓冲区 4K+overhead */
static u8       blk_buf[QSPI_BLOCK_SIZE + QSPI_OVERHEAD];


/* ============================ 底层操作方法 ================================ */

/*
 * 写使能（每次写/擦前必须调用）
 */
static void qspi_write_enable(void)
{
	u8 cmd = QSPI_CMD_WREN;
	XQspiPs_PolledTransfer(&qspi_instance, &cmd, NULL, 1);
}

/*
 * 等待 Flash 内部操作完成（轮询 WIP 位）
 */
static void qspi_wait_busy(void)
{
	u8 buf[2];
	buf[0] = QSPI_CMD_RDSR;
	do {
		XQspiPs_PolledTransfer(&qspi_instance, buf, buf, 2);
	} while (buf[1] & QSPI_SR_WIP);
}

/*
 * 4KB 块擦除
 */
static int qspi_erase_4k(u32 addr)
{
	u8 buf[4];
	buf[0] = QSPI_CMD_BE_4K;
	buf[1] = (u8)(addr >> 16);
	buf[2] = (u8)(addr >> 8);
	buf[3] = (u8)(addr);
	qspi_write_enable();
	if (XQspiPs_PolledTransfer(&qspi_instance, buf, NULL, 4) != XST_SUCCESS)
		return -1;
	qspi_wait_busy();
	return 0;
}

/*
 * 页编程（最多 256 字节）
 */
static int qspi_page_program(u32 addr, const u8 *data, u32 len)
{
	u8 buf[4 + QSPI_PAGE_SIZE];
	if (len > QSPI_PAGE_SIZE)
		len = QSPI_PAGE_SIZE;
	buf[0] = QSPI_CMD_PP;
	buf[1] = (u8)(addr >> 16);
	buf[2] = (u8)(addr >> 8);
	buf[3] = (u8)(addr);
	memcpy(&buf[4], data, len);
	qspi_write_enable();
	if (XQspiPs_PolledTransfer(&qspi_instance, buf, NULL, 4 + len) != XST_SUCCESS)
		return -1;
	qspi_wait_busy();
	return 0;
}

/*
 * 从 Flash 读取 len 字节到 buf
 */
static int qspi_read(u32 addr, u8 *buf, u32 len)
{
	u32 remaining = len;
	u32 offset = 0;

	while (remaining > 0) {
		u32 chunk = remaining;
		if (chunk > QSPI_BLOCK_SIZE)
			chunk = QSPI_BLOCK_SIZE;

		u8 cmd[5];
		cmd[0] = QSPI_CMD_FAST_READ;
		cmd[1] = (u8)((addr + offset) >> 16);
		cmd[2] = (u8)((addr + offset) >> 8);
		cmd[3] = (u8)(addr + offset);
		cmd[4] = 0;

		u32 total = chunk + QSPI_OVERHEAD;
		if (XQspiPs_PolledTransfer(&qspi_instance, cmd, blk_buf, total)
				!= XST_SUCCESS)
			return -1;

		memcpy(buf + offset, blk_buf + QSPI_OVERHEAD, chunk);
		offset += chunk;
		remaining -= chunk;
	}
	return 0;
}

/*
 * 写一个扇区（512 字节）：读-改-写
 *   - 读整个 4KB 块到 blk_buf
 *   - 修改其中对应扇区
 *   - 擦除 4KB 块
 *   - 按 256 字节页编程写回
 */
static int qspi_write_sector(u32 addr, const u8 *data)
{
	u32 block_start      = addr & ~(QSPI_BLOCK_SIZE - 1);
	u32 offset_in_block  = addr &  (QSPI_BLOCK_SIZE - 1);

	if (qspi_read(block_start, blk_buf, QSPI_BLOCK_SIZE) != 0)
		return -1;

	memcpy(blk_buf + offset_in_block, data, FX_SECTOR_SIZE);

	if (qspi_erase_4k(block_start) != 0)
		return -1;

	for (u32 i = 0; i < QSPI_BLOCK_SIZE; i += QSPI_PAGE_SIZE) {
		if (qspi_page_program(block_start + i, blk_buf + i, QSPI_PAGE_SIZE) != 0)
			return -1;
	}

	return 0;
}


/* =========================== FileX 驱动入口 ================================ */

VOID fx_zynq_qspi_driver(FX_MEDIA *media_ptr)
{
	switch (media_ptr->fx_media_driver_request)
	{
	// 初始化 QSPI 控制器
	case FX_DRIVER_INIT:
	{
		XQspiPs_Config *cfg = XQspiPs_LookupConfig(QSPI_DEVICE_ID);
		if (cfg == NULL) {
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		if (XQspiPs_CfgInitialize(&qspi_instance, cfg, cfg->BaseAddress)
				!= XST_SUCCESS) {
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		XQspiPs_SetOptions(&qspi_instance, XQSPIPS_FORCE_SSELECT_OPTION |
				XQSPIPS_HOLD_B_DRIVE_OPTION);
		XQspiPs_SetClkPrescaler(&qspi_instance, XQSPIPS_CLK_PRESCALE_8);
		XQspiPs_SetSlaveSelect(&qspi_instance);
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;
	}

	// 读扇区
	case FX_DRIVER_READ:
	{
		ULONG sector = media_ptr->fx_media_driver_logical_sector
				+ media_ptr->fx_media_hidden_sectors;
		ULONG count  = media_ptr->fx_media_driver_sectors;
		UCHAR *buf   = media_ptr->fx_media_driver_buffer;
		ULONG addr   = QSPI_BOOT_OFFSET + sector * FX_SECTOR_SIZE;

		if (qspi_read(addr, buf, count * FX_SECTOR_SIZE) != 0) {
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;
	}

	// 写扇区
	case FX_DRIVER_WRITE:
	{
		ULONG sector = media_ptr->fx_media_driver_logical_sector
				+ media_ptr->fx_media_hidden_sectors;
		ULONG count  = media_ptr->fx_media_driver_sectors;
		UCHAR *buf   = media_ptr->fx_media_driver_buffer;
		ULONG addr   = QSPI_BOOT_OFFSET + sector * FX_SECTOR_SIZE;

		for (ULONG i = 0; i < count; i++) {
			if (qspi_write_sector(addr + i * FX_SECTOR_SIZE,
					buf + i * FX_SECTOR_SIZE) != 0) {
				media_ptr->fx_media_driver_status = FX_IO_ERROR;
				break;
			}
		}
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;
	}

	case FX_DRIVER_FLUSH:
	case FX_DRIVER_ABORT:
	case FX_DRIVER_UNINIT:
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;

	// 读 boot sector（从 BOOT_OFFSET 处读）
	case FX_DRIVER_BOOT_READ:
	{
		if (qspi_read(QSPI_BOOT_OFFSET,
				media_ptr->fx_media_driver_buffer,
				FX_SECTOR_SIZE) != 0) {
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		media_ptr->fx_media_hidden_sectors = 0;
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;
	}

	// 写 boot sector
	case FX_DRIVER_BOOT_WRITE:
	{
		if (qspi_write_sector(QSPI_BOOT_OFFSET,
				media_ptr->fx_media_driver_buffer) != 0) {
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;
	}

	case FX_DRIVER_RELEASE_SECTORS:
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;

	default:
		media_ptr->fx_media_driver_status = FX_IO_ERROR;
		break;
	}
}
