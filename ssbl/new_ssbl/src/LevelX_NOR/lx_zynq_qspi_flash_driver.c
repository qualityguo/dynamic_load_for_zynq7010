/***************************** Include Files *********************************/
#include "lx_zynq_qspi_flash_driver.h"
#include "xqspips.h"
#include "xparameters.h"
#include <string.h>

/************************** Constant Definitions *****************************/

/*
 * Flash 最前面 QSPI_BOOT_OFFSET 字节存放 BOOT.bin，后续空间交给 LevelX 管理。
 * LevelX 管理区域起始地址 = QSPI_BOOT_OFFSET。
 */
#define QSPI_BOOT_OFFSET                0x100000

#define QSPI_DEVICE_ID                  XPAR_XQSPIPS_0_DEVICE_ID

#define QSPI_PAGE_SIZE                  256                                 // 页编程最大长度
#define QSPI_BLOCK_SIZE                 4096                                // 4KB 块擦除粒度

/*
 * 注意：实际 Flash 为 32MB。当前先用 16MB 配置保证跑通（3 字节地址寻址上限）。
 * LevelX 几何参数：
 *   LX_NOR_SECTOR_SIZE      = 512/sizeof(ULONG) = 128 words（=512B，与 FileX 扇区一致）
 *   LX_QSPI_WORDS_PER_BLOCK = 4096/4 = 1024 words（1 个 LevelX 块 = 1 个 Flash 4KB 块）
 *   LX_QSPI_TOTAL_BLOCKS    = (16MB - 1MB) / 4KB = 3840
 * 升级到 32MB 时：改 QSPI_FLASH_SIZE 为 32MB，并在 qspi_read/write/erase 中
 * 对 >=16MB 的地址增加 Bank Register (0x17) 切换（参考 FSBL qspi.c SendBankSelect）。
 */
#define LX_QSPI_WORDS_PER_BLOCK         (QSPI_BLOCK_SIZE / sizeof(ULONG))
#define QSPI_FLASH_SIZE                 (16u * 1024 * 1024)
#define LX_QSPI_TOTAL_BLOCKS            ((QSPI_FLASH_SIZE - QSPI_BOOT_OFFSET) / QSPI_BLOCK_SIZE)

/*
 * IO 模式使用的 Flash 命令（3 字节寻址模式）。
 * 重要：必须使用 3 字节地址命令，因为 Zynq-7000 PS QSPI 驱动 xqspips.c 的 FlashInst[]
 * 命令识别表只收录 3 字节地址命令（0x02/0x20/0x0B）。若使用 4 字节地址命令（0x12/0x21/0x0C），
 * PolledTransfer 会走 switch(ByteCount%4) 回退分支，其 ShiftReadData 与 dummy 处理对
 * 4B+dummy 的 FAST_READ 会读回错位数据，导致 LevelX 读到垃圾、lx_nor_flash_open 失败。
 * 参考官方 FSBL：ssbl/zynq7010/zynq_fsbl/qspi.c（FlashRead, FlashWrite）。
 */
#define QSPI_CMD_WREN                   0x06                                // 写使能
#define QSPI_CMD_RDSR                   0x05                                // 读状态寄存器
#define QSPI_CMD_WRSR                   0x01                                // 写状态寄存器
#define QSPI_CMD_PP                     0x02                                // 页编程（3 字节地址）
#define QSPI_CMD_BE_4K                  0x20                                // 4KB 块擦除（3 字节地址）
#define QSPI_CMD_FAST_READ              0x0B                                // 快速读（3 字节地址）

#define QSPI_SR_WIP                     0x01                                // 状态寄存器忙标志

/* 传输开销：命令 + 3 字节地址（与 FSBL OVERHEAD_SIZE 一致） */
#define QSPI_ADDR_BYTES                 3
#define QSPI_OVERHEAD                   4
#define QSPI_DUMMY_SIZE                 1                                   // FAST_READ 的 1 字节 dummy

/************************** Variable Definitions *****************************/
static XQspiPs  qspi_instance;                                              // QSPI 控制器实例

/* qspi_read 内部使用的硬件传输缓冲区（4K + overhead + dummy） */
static u8       blk_buf[QSPI_BLOCK_SIZE + QSPI_OVERHEAD + QSPI_DUMMY_SIZE];

/* block_erased_verify 专用缓冲区（不可与 blk_buf 复用，否则 qspi_read 内部 memcpy 会重叠） */
static u8       verify_buf[QSPI_BLOCK_SIZE];

/* LevelX 工作扇区缓冲，大小为 LX_NOR_SECTOR_SIZE 个 ULONG（=512 字节） */
static ULONG    lx_sector_buffer[LX_NOR_SECTOR_SIZE];


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
 * 4KB 块擦除（3 字节地址命令 0x20）
 */
static int qspi_erase_4k(u32 addr)
{
	u8 buf[1 + QSPI_ADDR_BYTES];
	buf[0] = QSPI_CMD_BE_4K;
	buf[1] = (u8)((addr & 0xFF0000) >> 16);
	buf[2] = (u8)((addr & 0xFF00) >> 8);
	buf[3] = (u8)(addr & 0xFF);
	qspi_write_enable();
	if (XQspiPs_PolledTransfer(&qspi_instance, buf, NULL, sizeof(buf)) != XST_SUCCESS)
		return -1;
	qspi_wait_busy();
	return 0;
}

/*
 * 页编程（最多 256 字节，调用者负责保证不跨页；3 字节地址命令 0x02）
 */
static int qspi_page_program(u32 addr, const u8 *data, u32 len)
{
	u8 buf[1 + QSPI_ADDR_BYTES + QSPI_PAGE_SIZE];
	if (len > QSPI_PAGE_SIZE)
		len = QSPI_PAGE_SIZE;
	buf[0] = QSPI_CMD_PP;
	buf[1] = (u8)((addr & 0xFF0000) >> 16);
	buf[2] = (u8)((addr & 0xFF00) >> 8);
	buf[3] = (u8)(addr & 0xFF);
	memcpy(&buf[1 + QSPI_ADDR_BYTES], data, len);
	qspi_write_enable();
	if (XQspiPs_PolledTransfer(&qspi_instance, buf, NULL, 1 + QSPI_ADDR_BYTES + len) != XST_SUCCESS)
		return -1;
	qspi_wait_busy();
	return 0;
}

/*
 * 从 Flash 读取 len 字节到 buf（3 字节地址命令 0x0B，对照 FSBL FlashRead）
 * FSBL 范式：OVERHEAD=4(cmd+3addr)，dummy 单独算 1 字节。
 * PolledTransfer 回读时 blk_buf 布局：[cmd回显][addr回显][dummy][数据...]，
 * 数据起始偏移 = OVERHEAD + DUMMY_SIZE = 5。
 */
static int qspi_read(u32 addr, u8 *buf, u32 len)
{
	u32 remaining = len;
	u32 offset = 0;

	while (remaining > 0) {
		u32 chunk = remaining;
		u32 cur_addr;
		u8  cmd[1 + QSPI_ADDR_BYTES + QSPI_DUMMY_SIZE];
		u32 total;

		if (chunk > QSPI_BLOCK_SIZE)
			chunk = QSPI_BLOCK_SIZE;

		cur_addr = addr + offset;
		cmd[0] = QSPI_CMD_FAST_READ;
		cmd[1] = (u8)((cur_addr & 0xFF0000) >> 16);
		cmd[2] = (u8)((cur_addr & 0xFF00) >> 8);
		cmd[3] = (u8)(cur_addr & 0xFF);
		cmd[4] = 0;                                              /* 1 字节 dummy */

		total = chunk + QSPI_OVERHEAD + QSPI_DUMMY_SIZE;
		if (XQspiPs_PolledTransfer(&qspi_instance, cmd, blk_buf, total)
				!= XST_SUCCESS)
			return -1;

		memcpy(buf + offset, blk_buf + QSPI_OVERHEAD + QSPI_DUMMY_SIZE, chunk);
		offset += chunk;
		remaining -= chunk;
	}
	return 0;
}


/* =========================== QSPI 硬件初始化 ============================== */

int lx_qspi_hardware_init(void)
{
	XQspiPs_Config *cfg = XQspiPs_LookupConfig(QSPI_DEVICE_ID);
	if (cfg == NULL)
		return -1;

	if (XQspiPs_CfgInitialize(&qspi_instance, cfg, cfg->BaseAddress)
			!= XST_SUCCESS)
		return -1;

	XQspiPs_SetOptions(&qspi_instance, XQSPIPS_FORCE_SSELECT_OPTION |
			XQSPIPS_HOLD_B_DRIVE_OPTION);
	XQspiPs_SetClkPrescaler(&qspi_instance, XQSPIPS_CLK_PRESCALE_8);
	XQspiPs_SetSlaveSelect(&qspi_instance);

	/*
	 * 当前使用 3 字节寻址模式（寻址范围 16MB，足够本次配置）。
	 * 不进入 4 字节模式（EN4B）——Zynq-7000 PS QSPI 驱动的 FlashInst[] 表不识别
	 * 4 字节地址命令。若后续升级到 32MB，改用 Bank Register (0x17) 切换高 16MB。
	 *
	 * 清除 Flash 块保护位（BP/TB/SRWD）：部分 NOR 出厂或前次使用时会置位 BP，
	 * 导致 program/erase 静默失败（WIP 正常、状态无错但数据不变）。
	 * WREN + WRSR(0x00) 一次性解锁全部块。
	 */
	qspi_write_enable();
	u8 wrsr_cmd[2] = { QSPI_CMD_WRSR, 0x00 };
	XQspiPs_PolledTransfer(&qspi_instance, wrsr_cmd, NULL, 2);
	qspi_wait_busy();

	return 0;
}


/* ========================= LevelX NOR 驱动回调 ============================ */
/*
 * 地址翻译：lx_qspi_nor_initialize 中 base_address 设为 0（虚拟基址），
 * 因此 flash_address 的数值即相对于 LevelX 管理区域起点的字节偏移。
 * 实际 Flash 字节地址 = QSPI_BOOT_OFFSET + (ULONG)flash_address。
 * 当前为 I/O 模式（未定义 LX_DIRECT_READ），flash_address 永不会被解引用。
 */

UINT lx_qspi_read(ULONG *flash_address, ULONG *destination, ULONG words)
{
	u32 byte_addr = QSPI_BOOT_OFFSET + (u32)flash_address;

	if (qspi_read(byte_addr, (u8 *)destination, words * (u32)sizeof(ULONG)) != 0)
		return LX_ERROR;
	return LX_SUCCESS;
}

UINT lx_qspi_write(ULONG *flash_address, ULONG *source, ULONG words)
{
	u32   byte_addr = QSPI_BOOT_OFFSET + (u32)flash_address;
	u32   remaining = words * (u32)sizeof(ULONG);
	u8   *src       = (u8 *)source;

	/* NOR 页编程不得跨页（256B），按页边界分片下发 */
	while (remaining > 0) {
		u32 page_left = QSPI_PAGE_SIZE - (byte_addr % QSPI_PAGE_SIZE);
		u32 chunk     = (remaining < page_left) ? remaining : page_left;

		if (qspi_page_program(byte_addr, src, chunk) != 0)
			return LX_ERROR;

		byte_addr += chunk;
		src       += chunk;
		remaining -= chunk;
	}
	return LX_SUCCESS;
}

UINT lx_qspi_block_erase(ULONG block, ULONG erase_count)
{
	u32 byte_addr;

	(void)erase_count;                                                    /* 擦除次数由 LevelX 写入块头，回调只负责擦除 */

	byte_addr = QSPI_BOOT_OFFSET + (u32)block * QSPI_BLOCK_SIZE;
	if (qspi_erase_4k(byte_addr) != 0)
		return LX_ERROR;
	return LX_SUCCESS;
}

UINT lx_qspi_block_erased_verify(ULONG block)
{
	u32 byte_addr = QSPI_BOOT_OFFSET + (u32)block * QSPI_BLOCK_SIZE;
	u32 i;

	if (qspi_read(byte_addr, verify_buf, QSPI_BLOCK_SIZE) != 0)
		return LX_ERROR;

	for (i = 0; i < QSPI_BLOCK_SIZE; i++) {
		if (verify_buf[i] != 0xFF)
			return LX_ERROR;
	}
	return LX_SUCCESS;
}

UINT lx_qspi_system_error(UINT error_code)
{
	/*
	 * LevelX 系统级错误钩子。_lx_nor_flash_system_error 本身是 VOID，不接收本回调
	 * 的返回值，但按官方 simulator + Azure RTOS 通用做法，记录错误后返回 LX_SUCCESS，
	 * 避免某些内部路径的连锁反应。
	 */
	(void)error_code;
	return LX_SUCCESS;
}


/* ===================== LevelX NOR 实例初始化入口 ========================== */

UINT lx_qspi_nor_initialize(LX_NOR_FLASH *nor_flash)
{
	/* 虚拟基址 = 0，回调中按 (ULONG)flash_address 计算字节偏移 */
	nor_flash->lx_nor_flash_base_address       = (ULONG *)0;

	/* Flash 几何参数 */
	nor_flash->lx_nor_flash_total_blocks       = LX_QSPI_TOTAL_BLOCKS;
	nor_flash->lx_nor_flash_words_per_block    = LX_QSPI_WORDS_PER_BLOCK;

	/* 注册 LevelX 驱动回调 */
	nor_flash->lx_nor_flash_driver_read                = lx_qspi_read;
	nor_flash->lx_nor_flash_driver_write               = lx_qspi_write;
	nor_flash->lx_nor_flash_driver_block_erase         = lx_qspi_block_erase;
	nor_flash->lx_nor_flash_driver_block_erased_verify = lx_qspi_block_erased_verify;
	nor_flash->lx_nor_flash_driver_system_error        = lx_qspi_system_error;

	/* LevelX 内部读-改-写所需扇区缓冲（非 LX_DIRECT_READ 时必须非空） */
	nor_flash->lx_nor_flash_sector_buffer              = lx_sector_buffer;

	return LX_SUCCESS;
}
