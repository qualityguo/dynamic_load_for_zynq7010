/****************************======================================================================
 * 第二层：LevelX NOR 驱动胶水层
 *===================================================================================================
 * 本文件只负责 LevelX NOR 语义：Flash 几何参数、逻辑地址→物理地址翻译、LevelX
 * 驱动回调、磨损均衡实例初始化、管理区擦除/扇区计数等。
 *
 * 所有真实 Flash 硬件操作（读/写/擦/初始化/JEDEC）已下沉到第三层 qspi_driver.c，
 * 本层通过 #include "qspi_driver.h" 调用，自身不再直接碰 XQspiPs。
 *
 * 层次：
 *   Layer1 FileX  (fx_zynq_qspi_driver.c)  —— FAT 文件系统
 *      ↓ lx_nor_flash_sector_read/write/...
 *   Layer2 LevelX (本文件)                  —— 磨损均衡 / 逻辑扇区
 *      ↓ qspi_drv_read/write/erase_4k/init
 *   Layer3 QSPI   (qspi_driver.c)           —— XQspiPs 控制器 + W25Q NOR
 *===================================================================================================*/
#include "lx_zynq_qspi_flash_driver.h"
#include "qspi_driver.h"
#include "xil_printf.h"
#include <string.h>

/************************** 常量定义 ******************************************/

/*
 * Flash 最前面 QSPI_BOOT_OFFSET(1MB) 字节存放 BOOT.bin，后续空间交给 LevelX。
 * LevelX 管理区域起始地址 = QSPI_BOOT_OFFSET，本层所有回调都把 LevelX 给出的
 * 逻辑偏移加上该基址，得到第三层使用的绝对 Flash 物理地址。
 */
#define QSPI_BOOT_OFFSET                0x100000

#define QSPI_BLOCK_SIZE                 4096                                /* 4KB 块擦除粒度 */

/*
 * 注意：实际 Flash 为 32MB(W25Q256)。当前先用 16MB 配置保证跑通（3 字节地址寻址上限）。
 * LevelX 几何参数：
 *   LX_NOR_SECTOR_SIZE      = 512/sizeof(ULONG) = 128 words（=512B，与 FileX 扇区一致）
 *   LX_QSPI_WORDS_PER_BLOCK = 4096/4 = 1024 words（1 个 LevelX 块 = 1 个 Flash 4KB 块）
 *   LX_QSPI_TOTAL_BLOCKS    = (16MB - 1MB) / 4KB = 3840
 * 升级到 32MB 时：改 QSPI_FLASH_SIZE 为 32MB，并在第三层对 >=16MB 的地址增加
 * Bank Register(0x17) 切换（参考 FSBL qspi.c SendBankSelect）。
 */
#define LX_QSPI_WORDS_PER_BLOCK         (QSPI_BLOCK_SIZE / sizeof(ULONG))
#define QSPI_FLASH_SIZE                 (16u * 1024 * 1024)
#define LX_QSPI_TOTAL_BLOCKS            ((QSPI_FLASH_SIZE - QSPI_BOOT_OFFSET) / QSPI_BLOCK_SIZE)

/*
 * 逻辑扇区大小：与 LevelX LX_NOR_SECTOR_SIZE(128 words) 和 FileX 扇区一致，均为 512B。
 *
 * 注意：LevelX NOR 每个块的【首扇区】用于存放块头（擦除计数 + 扇区映射状态），
 * 因此每个块的数据扇区数 = (words_per_block / LX_NOR_SECTOR_SIZE) − 1 = 8 − 1 = 7。
 * 这正是 lx_nor_flash_open 后 total_physical_sectors = 3840 × 7 = 26880 的由来，
 * 而不是裸 Flash 的 15MB/512 = 30720。
 *
 * FileX 驱动按 (logical_sector + hidden_sectors) 映射到 LevelX 逻辑扇区，故 FileX
 * 可寻址的 LevelX 逻辑扇区上限 = 26880。fx_media_format 的 total_sectors 不得超过
 * 26880 − hidden_sectors，否则 format 写 FAT/根目录时会越界写坏 LevelX 元数据，
 * 导致后续 lx_nor_flash_open 返回 LX_ERROR(0x1)。
 */
#define LX_QSPI_SECTORS_PER_BLOCK       (LX_QSPI_WORDS_PER_BLOCK / LX_NOR_SECTOR_SIZE)          /* 8 */
#define LX_QSPI_DATA_SECTORS            (LX_QSPI_TOTAL_BLOCKS * (LX_QSPI_SECTORS_PER_BLOCK - 1))/* 26880 */

/************************** 变量定义 ******************************************/

/* block_erased_verify 专用回读缓冲（一个 4KB 块）。第三层内部有自己的硬件收发缓冲，
 * 二者互不干扰。 */
static u8    verify_buf[QSPI_BLOCK_SIZE];

/* LevelX 工作扇区缓冲，大小为 LX_NOR_SECTOR_SIZE 个 ULONG（=512 字节） */
static ULONG lx_sector_buffer[LX_NOR_SECTOR_SIZE];


/* ========================= LevelX 管理区维护接口 =========================== */

/*
 * 初始化 QSPI 硬件（委托第三层）。FileX 驱动在 FX_DRIVER_INIT 中调用。
 * @return  0 成功；-1 失败
 */
int lx_qspi_hardware_init(void)
{
	return qspi_drv_init();
}

/*
 * 擦除整片 LevelX 管理区域（QSPI_BOOT_OFFSET 起，到 QSPI_FLASH_SIZE）。
 * 用途：LevelX 元数据损坏导致 lx_nor_flash_open 失败时，全片擦除后重新格式化。
 * BOOT.bin 存放在前 QSPI_BOOT_OFFSET(1MB) 内，不受影响。
 * 3840 块每块等待 ~0.6-3ms，全程约 2-12 秒，故带进度输出。
 */
void lx_qspi_erase_levelx_region(void)
{
	ULONG i;
	int   erase_fail = 0;

	xil_printf("[LX] erasing LevelX region (%lu blocks)...\r\n",
			(unsigned long)LX_QSPI_TOTAL_BLOCKS);

	for (i = 0; i < LX_QSPI_TOTAL_BLOCKS; i++) {
		if (qspi_drv_erase_4k(QSPI_BOOT_OFFSET + (u32)i * QSPI_BLOCK_SIZE) != 0)
			erase_fail++;
		if ((i % 500) == 0)
			xil_printf("[LX] erasing %lu / %lu\r\n",
					(unsigned long)i, (unsigned long)LX_QSPI_TOTAL_BLOCKS);
	}

	xil_printf("[LX] erased %lu blocks, %d fail\r\n",
			(unsigned long)LX_QSPI_TOTAL_BLOCKS, erase_fail);
}

/*
 * 返回 LevelX 可提供的逻辑扇区总数（=数据扇区数，已扣除每块的块头扇区）。
 * 调用方（fx_media_format）需再减去 FileX 的 hidden_sectors(FX_QSPI_META_SECTORS)，
 * 得到 FileX 实际可用的 total_sectors，避免越界写坏 LevelX 元数据。
 */
ULONG lx_qspi_get_total_sectors(void)
{
	return LX_QSPI_DATA_SECTORS;
}


/* ========================= LevelX NOR 驱动回调 ============================ */
/*
 * 地址翻译：lx_qspi_nor_initialize 中 base_address 设为 0（虚拟基址），
 * 因此 flash_address 的数值即相对于 LevelX 管理区域起点的字节偏移。
 * 实际 Flash 字节地址 = QSPI_BOOT_OFFSET + (ULONG)flash_address，交给第三层执行。
 */

UINT lx_qspi_read(ULONG *flash_address, ULONG *destination, ULONG words)
{
	u32 byte_addr = QSPI_BOOT_OFFSET + (u32)flash_address;

	if (qspi_drv_read(byte_addr, (u8 *)destination, words * (u32)sizeof(ULONG)) != 0)
		return LX_ERROR;
	return LX_SUCCESS;
}

UINT lx_qspi_write(ULONG *flash_address, ULONG *source, ULONG words)
{
	u32 byte_addr = QSPI_BOOT_OFFSET + (u32)flash_address;

	/* 第三层 qspi_drv_write 内部已按 256B 页边界分片，可直接传任意长度 */
	if (qspi_drv_write(byte_addr, (const u8 *)source, words * (u32)sizeof(ULONG)) != 0)
		return LX_ERROR;
	return LX_SUCCESS;
}

UINT lx_qspi_block_erase(ULONG block, ULONG erase_count)
{
	u32 byte_addr;

	(void)erase_count;                                                    /* 擦除次数由 LevelX 写入块头，回调只负责擦除 */

	byte_addr = QSPI_BOOT_OFFSET + (u32)block * QSPI_BLOCK_SIZE;
	if (qspi_drv_erase_4k(byte_addr) != 0)
		return LX_ERROR;
	return LX_SUCCESS;
}

UINT lx_qspi_block_erased_verify(ULONG block)
{
	u32 byte_addr = QSPI_BOOT_OFFSET + (u32)block * QSPI_BLOCK_SIZE;
	u32 i;

	if (qspi_drv_read(byte_addr, verify_buf, QSPI_BLOCK_SIZE) != 0)
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
