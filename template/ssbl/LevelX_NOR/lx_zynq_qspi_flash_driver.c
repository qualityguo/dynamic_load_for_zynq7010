/***************************** Include Files *********************************/
#include "lx_zynq_qspi_flash_driver.h"
#include "ssbl.h"

/************************** Constant Definitions *****************************/
#define QSPI_BOOT_OFFSET                0x100000													// 前1M空间不被FileX管理
#define QSPI_FLASH_SIZE                 (16u * 1024 * 1024)											// 16MB空间
#define	QSPI_BLOCK_SIZE					4096														// 每个块4KB
#define LX_QSPI_TOTAL_BLOCKS            ((QSPI_FLASH_SIZE - QSPI_BOOT_OFFSET) / QSPI_BLOCK_SIZE)	// LevelX管理的块的数量
#define LX_QSPI_WORDS_PER_BLOCK         (QSPI_BLOCK_SIZE / sizeof(ULONG))							// 每个块1024字
#define LX_QSPI_SECTORS_PER_BLOCK       (LX_QSPI_WORDS_PER_BLOCK / LX_NOR_SECTOR_SIZE)				// 每个块1扇区
#define LX_QSPI_DATA_SECTORS            (LX_QSPI_TOTAL_BLOCKS * (LX_QSPI_SECTORS_PER_BLOCK - 1))	// LevelX管理的扇区的数量
#define QSPI_DEVICE_ID                  XPAR_XQSPIPS_0_DEVICE_ID									// FLASH 3字节寻址 IO模式
#define COMMAND_OFFSET                  0                       									// FLASH 指令
#define ADDRESS_1_OFFSET                1                       									// 地址 MSB
#define ADDRESS_2_OFFSET                2                       									// 地址中字节
#define ADDRESS_3_OFFSET                3                       									// 地址 LSB
#define DATA_OFFSET                     4                       									// 读回数据起始
#define DUMMY_OFFSET                    4                       									// FAST_READ dummy 偏移
#define DUMMY_SIZE                      1                       									// FAST_READ 的 1 字节 dummy
#define OVERHEAD_SIZE                   4                       									// cmd + 3 字节地址
#define RD_ID_SIZE                      4                       									// RDID 命令 + 3 字节 ID 回应
#define WRITE_ENABLE_CMD                0x06
#define READ_ID_CMD                     0x9F
#define FAST_READ_CMD                   0x0B
#define PP_CMD                          0x02                    									// 页编程
#define BE_4K_CMD                       0x20                    									// 4KB 块擦除
#define RDSR_CMD                        0x05                    									// 读状态寄存器
#define WRSR_CMD                        0x01                    									// 写状态寄存器
#define SR_WIP_BIT                      0x01                    									// 状态寄存器忙标志
#define QSPI_DATA_SIZE                  4096														// 单次传输最大数据量
#define QSPI_PAGE_SIZE                  256															// 页编程页大小

/************************** Function Prototypes ******************************/
static void qspi_write_enable(void);										// 写使能
static u8 qspi_read_sr1(void);												// 读SR1
static void qspi_wait_busy(void);											// 等待擦写完成
static void qspi_global_unprotect(void);									// 解除Flash块保护
static int qspi_hw_read_id(u8 id[3]);										// 读设备ID
static int qspi_hw_init(void);												// 初始化
static int qspi_hw_read(u32 addr, u8 *buf, u32 len);						// 读
static int qspi_hw_write(u32 addr, const u8 *data, u32 len);				// 写
static int qspi_hw_erase_4k(u32 addr);										// 擦除

/************************** Variable Definitions *****************************/
static XQspiPs  QspiInstance;
static XQspiPs *QspiInstancePtr = &QspiInstance;							// 控制器对象指针
static int      g_qspi_init_flag = 0;										// 初始化标志
static u8       ReadBuffer[QSPI_DATA_SIZE + DATA_OFFSET + DUMMY_SIZE];		// 收缓冲
static u8       WriteBuffer[DATA_OFFSET + QSPI_PAGE_SIZE];					// 发缓冲
static u8       verify_buf[QSPI_BLOCK_SIZE];								// 校验用缓冲
static ULONG    lx_sector_buffer[LX_NOR_SECTOR_SIZE];						// 工作扇区缓冲


static void qspi_write_enable(void)
{
	u8 Cmd[4] = { WRITE_ENABLE_CMD, 0, 0, 0 };              /* 缓冲 ≥4B，避免 *(u32*) 越界 */

	XQspiPs_PolledTransfer(QspiInstancePtr, Cmd, NULL, 1);
}

static u8 qspi_read_sr1(void)
{
	u8 Cmd[4] = { RDSR_CMD, 0, 0, 0 };
	u8 Buf[4] = { 0, 0, 0, 0 };

	XQspiPs_PolledTransfer(QspiInstancePtr, Cmd, Buf, 2);
	return Buf[1];
}

static void qspi_wait_busy(void)
{
	while (qspi_read_sr1() & SR_WIP_BIT) {
		/* 等待 WIP 清零 */
	}
}

static void qspi_global_unprotect(void)
{
	u8 Cmd[4] = { WRSR_CMD, 0x00, 0, 0 };

	qspi_write_enable();
	XQspiPs_PolledTransfer(QspiInstancePtr, Cmd, NULL, 2);
	qspi_wait_busy();
}

static int qspi_hw_read_id(u8 id[3])
{
	u8 Cmd[4] = { READ_ID_CMD, 0x00, 0x00, 0x00 };
	int Status;

	Status = XQspiPs_PolledTransfer(QspiInstancePtr, Cmd, ReadBuffer, RD_ID_SIZE);
	if (Status != XST_SUCCESS) {
		return -1;
	}

	id[0] = ReadBuffer[1];                                   /* 厂商 */
	id[1] = ReadBuffer[2];                                   /* 类型 */
	id[2] = ReadBuffer[3];                                   /* 容量 */
	return 0;
}

static int qspi_hw_init(void)
{
	XQspiPs_Config *QspiConfig;
	int Status;

	if (g_qspi_init_flag == 1) {
		return 0;
	}

	/* 初始化 QSPI 驱动（对照 FSBL InitQspi） */
	QspiConfig = XQspiPs_LookupConfig(QSPI_DEVICE_ID);
	if (NULL == QspiConfig) {
		ssbl_printf(LOG_ERR, "QSPI LookupConfig failed\r\n");
		return -1;
	}

	Status = XQspiPs_CfgInitialize(QspiInstancePtr, QspiConfig, QspiConfig->BaseAddress);
	if (Status != XST_SUCCESS) {
		ssbl_printf(LOG_ERR, "QSPI CfgInitialize failed\r\n");
		return -1;
	}

	/* 手动片选 + 驱动 HOLD_B 高（IO 模式，不进入 LQSPI 线性模式） */
	XQspiPs_SetOptions(QspiInstancePtr, XQSPIPS_FORCE_SSELECT_OPTION |
			XQSPIPS_HOLD_B_DRIVE_OPTION);

	/* QSPI 时钟预分频（对照 FSBL：PRESCALE_8） */
	XQspiPs_SetClkPrescaler(QspiInstancePtr, XQSPIPS_CLK_PRESCALE_8);

	/* 片选有效 */
	XQspiPs_SetSlaveSelect(QspiInstancePtr);

	/* 读 JEDEC ID 校验器件存在且通信正常 */
	u8 Id[3];
	if (qspi_hw_read_id(Id) != 0) {
		ssbl_printf(LOG_ERR, "QSPI read_id transfer failed\r\n");
		return -1;
	}
	ssbl_printf(LOG_INFO, "QSPI JEDEC ID = %02X %02X %02X\r\n", Id[0], Id[1], Id[2]);

	/* 解除块保护，确保后续 program/erase 可生效 */
	qspi_global_unprotect();

	g_qspi_init_flag = 1;
	return 0;
}

static int qspi_hw_read(u32 addr, u8 *buf, u32 len)
{
	u32 Remaining = len;
	u32 Offset    = 0;

	while (Remaining > 0) {
		u32 Length = (Remaining > QSPI_DATA_SIZE) ? QSPI_DATA_SIZE : Remaining;
		u32 CurAddr = addr + Offset;
		int Status;

		/* 装配读命令（对照 FSBL FlashRead）：
		 * 仅 cmd + 3 字节地址写入发送缓冲；dummy 由 XQspiPs 驱动自动注入。 */
		WriteBuffer[COMMAND_OFFSET]   = FAST_READ_CMD;
		WriteBuffer[ADDRESS_1_OFFSET] = (u8)((CurAddr & 0xFF0000) >> 16);
		WriteBuffer[ADDRESS_2_OFFSET] = (u8)((CurAddr & 0xFF00) >> 8);
		WriteBuffer[ADDRESS_3_OFFSET] = (u8)(CurAddr & 0xFF);

		/* ByteCount = 数据 + dummy + overhead */
		Status = XQspiPs_PolledTransfer(QspiInstancePtr, WriteBuffer, ReadBuffer,
				Length + DUMMY_SIZE + OVERHEAD_SIZE);
		if (Status != XST_SUCCESS) {
			return -1;
		}

		/* 回读布局：[cmd回显][addr回显][dummy][数据...]，数据起始于偏移 5 */
		memcpy(buf + Offset, &ReadBuffer[DATA_OFFSET + DUMMY_SIZE], Length);

		Offset    += Length;
		Remaining -= Length;
	}
	return 0;
}

static int qspi_hw_write(u32 addr, const u8 *data, u32 len)
{
	u32 Remaining = len;
	u32 Offset    = 0;

	while (Remaining > 0) {
		/* NOR 页编程不得跨 256B 页边界，按页边界分片 */
		u32 PageLeft = QSPI_PAGE_SIZE - ((addr + Offset) % QSPI_PAGE_SIZE);
		u32 Length   = (Remaining < PageLeft) ? Remaining : PageLeft;
		u32 CurAddr  = addr + Offset;
		int Status;

		WriteBuffer[COMMAND_OFFSET]   = PP_CMD;
		WriteBuffer[ADDRESS_1_OFFSET] = (u8)((CurAddr & 0xFF0000) >> 16);
		WriteBuffer[ADDRESS_2_OFFSET] = (u8)((CurAddr & 0xFF00) >> 8);
		WriteBuffer[ADDRESS_3_OFFSET] = (u8)(CurAddr & 0xFF);
		memcpy(&WriteBuffer[DATA_OFFSET], data + Offset, Length);

		qspi_write_enable();
		Status = XQspiPs_PolledTransfer(QspiInstancePtr, WriteBuffer, NULL, DATA_OFFSET + Length);
		if (Status != XST_SUCCESS) {
			return -1;
		}
		qspi_wait_busy();

		Offset    += Length;
		Remaining -= Length;
	}
	return 0;
}

static int qspi_hw_erase_4k(u32 addr)
{
	u8 Cmd[4];
	int Status;

	Cmd[0] = BE_4K_CMD;
	Cmd[1] = (u8)((addr & 0xFF0000) >> 16);
	Cmd[2] = (u8)((addr & 0xFF00) >> 8);
	Cmd[3] = (u8)(addr & 0xFF);

	qspi_write_enable();
	Status = XQspiPs_PolledTransfer(QspiInstancePtr, Cmd, NULL, 4);
	if (Status != XST_SUCCESS) {
		return -1;
	}
	qspi_wait_busy();
	return 0;
}


/*==================================================================================================
 *                              LevelX 管理区维护接口
 *==================================================================================================*/

/*
 * 初始化 QSPI 硬件。FileX 驱动在 FX_DRIVER_INIT 中调用。
 * @return  0 成功；-1 失败
 */
int lx_qspi_hardware_init(void)
{
	return qspi_hw_init();
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
		if (qspi_hw_erase_4k(QSPI_BOOT_OFFSET + (u32)i * QSPI_BLOCK_SIZE) != 0)
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


/*==================================================================================================
 *                              LevelX NOR 驱动回调
 *==================================================================================================
 * 地址翻译：lx_qspi_nor_initialize 中 base_address 设为 0（虚拟基址），
 * 因此 flash_address 的数值即相对于 LevelX 管理区域起点的字节偏移。
 * 实际 Flash 字节地址 = QSPI_BOOT_OFFSET + (ULONG)flash_address，交给硬件原语执行。
 *==================================================================================================*/

UINT lx_qspi_read(ULONG *flash_address, ULONG *destination, ULONG words)
{
	u32 byte_addr = QSPI_BOOT_OFFSET + (u32)flash_address;

	if (qspi_hw_read(byte_addr, (u8 *)destination, words * (u32)sizeof(ULONG)) != 0)
		return LX_ERROR;
	return LX_SUCCESS;
}

UINT lx_qspi_write(ULONG *flash_address, ULONG *source, ULONG words)
{
	u32 byte_addr = QSPI_BOOT_OFFSET + (u32)flash_address;

	/* 硬件原语 qspi_hw_write 内部已按 256B 页边界分片，可直接传任意长度 */
	if (qspi_hw_write(byte_addr, (const u8 *)source, words * (u32)sizeof(ULONG)) != 0)
		return LX_ERROR;
	return LX_SUCCESS;
}

UINT lx_qspi_block_erase(ULONG block, ULONG erase_count)
{
	u32 byte_addr;

	(void)erase_count;                                                    /* 擦除次数由 LevelX 写入块头，回调只负责擦除 */

	byte_addr = QSPI_BOOT_OFFSET + (u32)block * QSPI_BLOCK_SIZE;
	if (qspi_hw_erase_4k(byte_addr) != 0)
		return LX_ERROR;
	return LX_SUCCESS;
}

UINT lx_qspi_block_erased_verify(ULONG block)
{
	u32 byte_addr = QSPI_BOOT_OFFSET + (u32)block * QSPI_BLOCK_SIZE;
	u32 i;

	if (qspi_hw_read(byte_addr, verify_buf, QSPI_BLOCK_SIZE) != 0)
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


/*==================================================================================================
 *                          LevelX NOR 实例初始化入口
 *==================================================================================================*/

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
