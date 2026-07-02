/***************************** Include Files *********************************/
#include "qspi_driver.h"
#include "ssbl.h"                              /* ssbl_printf 日志 + stdint */
#include "xqspips.h"
#include "xparameters.h"
#include <string.h>

/*
 * 第三层：QSPI NOR Flash 硬件驱动实现
 *
 * 严格参考官方 FSBL：ssbl/new_fsbl/src/qspi.c（InitQspi / FlashReadID / FlashRead）。
 * FSBL 为只读 BootLoader，本层在其基础上补齐 page program(0x02) 与 4K 擦除(0x20)。
 *
 * 关键约定（与 XQspiPs 驱动一致，详见 xqspips.c）：
 *   - 发送缓冲至少 4 字节：驱动以 *(u32*)SendBufferPtr 取首字作为指令。
 *   - 带接收缓冲的读传输中，cmd+3addr(4B) 之后的所有发送字节由驱动自动填
 *     DUMMY_TX_DATA，故 FAST_READ 的 1 字节 dummy 无需调用方提供。
 */

/************************** 常量定义 *****************************************/

#define QSPI_DEVICE_ID              XPAR_XQSPIPS_0_DEVICE_ID

/* Flash 命令（3 字节寻址，IO 模式） */
#define COMMAND_OFFSET              0                       /* FLASH 指令 */
#define ADDRESS_1_OFFSET            1                       /* 地址 MSB */
#define ADDRESS_2_OFFSET            2                       /* 地址中字节 */
#define ADDRESS_3_OFFSET            3                       /* 地址 LSB */
#define DATA_OFFSET                 4                       /* 读回数据起始 */
#define DUMMY_OFFSET                4                       /* FAST_READ dummy 偏移 */
#define DUMMY_SIZE                  1                       /* FAST_READ 的 1 字节 dummy */
#define OVERHEAD_SIZE               4                       /* cmd + 3 字节地址 */
#define RD_ID_SIZE                  4                       /* RDID 命令 + 3 字节 ID 回应 */

#define WRITE_ENABLE_CMD            0x06
#define READ_ID_CMD                 0x9F
#define FAST_READ_CMD               0x0B
#define PP_CMD                      0x02                    /* 页编程 */
#define BE_4K_CMD                   0x20                    /* 4KB 块擦除 */
#define RDSR_CMD                    0x05                    /* 读状态寄存器 */
#define WRSR_CMD                    0x01                    /* 写状态寄存器 */

#define SR_WIP_BIT                  0x01                    /* 状态寄存器忙标志 */

/* 单次传输最大数据量（与 FSBL DATA_SIZE 一致，受静态缓冲约束） */
#define QSPI_DATA_SIZE              4096
/* 页编程页大小 */
#define QSPI_PAGE_SIZE              256

/* 期望的 JEDEC ID：Winbond W25Q256 = EF 40 19（256Mbit/32MB）。
 * 当前工程按 16MB(3 字节寻址上限)使用，但 ID 仍按真实器件校验。 */
#define JEDEC_WINBOND               0xEF
#define JEDEC_W25Q_TYPE             0x40
#define JEDEC_W25Q256_SIZE          0x19

/************************** 变量定义 *****************************************/

static XQspiPs QspiInstance;                                /* 控制器实例（全局唯一） */
static XQspiPs *QspiInstancePtr = &QspiInstance;

/* 初始化标志：幂等保护，避免对同一控制器重复 CfgInitialize（与 led/key 驱动一致） */
static int g_qspi_init_flag = 0;

/* 收发缓冲（与 FSBL 同名同布局）。读：DATA_SIZE + 4 overhead + 1 dummy。
 * 写：DATA_OFFSET(4) + 一次页编程最大 256B。 */
static uint8_t ReadBuffer[QSPI_DATA_SIZE + DATA_OFFSET + DUMMY_SIZE];
static uint8_t WriteBuffer[DATA_OFFSET + QSPI_PAGE_SIZE];


/************************** 内部函数 prototypes *****************************/
static void    QspiWriteEnable(void);
static uint8_t QspiReadSR1(void);
static void    QspiWaitBusy(void);
static void    QspiGlobalUnprotect(void);


/************************** 内部静态函数 *************************************/

/*
 * 写使能（WREN）。每次页编程/擦除前必须调用。
 */
static void QspiWriteEnable(void)
{
	uint8_t Cmd[4] = { WRITE_ENABLE_CMD, 0, 0, 0 };         /* 缓冲 ≥4B，避免 *(u32*) 越界 */

	XQspiPs_PolledTransfer(QspiInstancePtr, Cmd, NULL, 1);
}

/*
 * 读取状态寄存器 SR1（收发分离缓冲）。
 *
 * ==== 重要：不得用 send==recv 的原地缓冲 ====
 * XQspiPs_PolledTransfer 在 sendBuf==recvBuf（原地）路径下，2 字节 RDSR 回读经
 * ShiftReadData 处理后 WIP 位不会落到 Buf[1]（实测恒为 0），导致轮询误判"不忙"
 * 提前退出。必须用收发分离的两个缓冲，SR 才会正确落到 RecvBuf[1]（与 Xilinx
 * 标准 RDSR 范式一致）。否则写/擦操作还没完成就返回，紧跟的读会撞上 Flash
 * 仍在忙（WIP=1），读回全 0x00，表现为"擦除/写入后读到全 0"。
 */
static uint8_t QspiReadSR1(void)
{
	uint8_t Cmd[4] = { RDSR_CMD, 0, 0, 0 };
	uint8_t Buf[4] = { 0, 0, 0, 0 };

	XQspiPs_PolledTransfer(QspiInstancePtr, Cmd, Buf, 2);
	return Buf[1];
}

/*
 * 轮询状态寄存器 WIP 位，等待 Flash 内部写/擦操作真正完成。
 */
static void QspiWaitBusy(void)
{
	while (QspiReadSR1() & SR_WIP_BIT) {
		/* 等待 WIP 清零 */
	}
}

/*
 * 解除 Flash 块保护（BP/TB/SRWD）。部分 NOR 出厂或前次使用会置位 BP，导致
 * program/erase 静默失败（WIP 正常、状态无错但数据不变）。
 * WREN + WRSR(0x00) 一次性解锁全部块。
 */
static void QspiGlobalUnprotect(void)
{
	uint8_t Cmd[4] = { WRSR_CMD, 0x00, 0, 0 };

	QspiWriteEnable();
	XQspiPs_PolledTransfer(QspiInstancePtr, Cmd, NULL, 2);
	QspiWaitBusy();
}


/************************** 公开接口 *****************************************/

int qspi_drv_init(void)
{
	XQspiPs_Config *QspiConfig;
	int Status;

	/* 幂等：已初始化则直接返回成功 */
	if (g_qspi_init_flag == 1) {
		return 0;
	}

	/*
	 * 初始化 QSPI 驱动（对照 FSBL InitQspi）
	 */
	QspiConfig = XQspiPs_LookupConfig(QSPI_DEVICE_ID);
	if (NULL == QspiConfig) {
		ssbl_printf(LOG_ERR, "QSPI LookupConfig failed\r\n");
		return -1;
	}

	Status = XQspiPs_CfgInitialize(QspiInstancePtr, QspiConfig,
			QspiConfig->BaseAddress);
	if (Status != XST_SUCCESS) {
		ssbl_printf(LOG_ERR, "QSPI CfgInitialize failed\r\n");
		return -1;
	}

	/*
	 * 手动片选 + 驱动 HOLD_B 高（IO 模式，不进入 LQSPI 线性模式）
	 */
	XQspiPs_SetOptions(QspiInstancePtr, XQSPIPS_FORCE_SSELECT_OPTION |
			XQSPIPS_HOLD_B_DRIVE_OPTION);

	/*
	 * QSPI 时钟预分频（对照 FSBL：PRESCALE_8）
	 */
	XQspiPs_SetClkPrescaler(QspiInstancePtr, XQSPIPS_CLK_PRESCALE_8);

	/*
	 * 片选有效
	 */
	XQspiPs_SetSlaveSelect(QspiInstancePtr);

	/*
	 * 读 JEDEC ID 校验器件存在且通信正常
	 */
	uint8_t Id[3];
	if (qspi_drv_read_id(Id) != 0) {
		ssbl_printf(LOG_ERR, "QSPI read_id transfer failed\r\n");
		return -1;
	}
	ssbl_printf(LOG_INFO, "QSPI JEDEC ID = %02X %02X %02X\r\n",
			Id[0], Id[1], Id[2]);
	if (Id[0] != JEDEC_WINBOND || Id[1] != JEDEC_W25Q_TYPE ||
			Id[2] != JEDEC_W25Q256_SIZE) {
		ssbl_printf(LOG_ERR, "QSPI JEDEC ID mismatch (expect EF 40 19)\r\n");
		return -1;
	}

	/*
	 * 解除块保护，确保后续 program/erase 可生效
	 */
	QspiGlobalUnprotect();

	g_qspi_init_flag = 1;
	return 0;
}

int qspi_drv_read_id(uint8_t id[3])
{
	uint8_t Cmd[4] = { READ_ID_CMD, 0x00, 0x00, 0x00 };
	int Status;

	Status = XQspiPs_PolledTransfer(QspiInstancePtr, Cmd, ReadBuffer,
			RD_ID_SIZE);
	if (Status != XST_SUCCESS) {
		return -1;
	}

	id[0] = ReadBuffer[1];                                   /* 厂商 */
	id[1] = ReadBuffer[2];                                   /* 类型 */
	id[2] = ReadBuffer[3];                                   /* 容量 */
	return 0;
}

int qspi_drv_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
	uint32_t Remaining = len;
	uint32_t Offset = 0;

	while (Remaining > 0) {
		uint32_t Length = (Remaining > QSPI_DATA_SIZE) ? QSPI_DATA_SIZE : Remaining;
		uint32_t CurAddr = addr + Offset;
		int Status;

		/*
		 * 装配读命令（对照 FSBL FlashRead）：
		 * 仅 cmd + 3 字节地址写入发送缓冲；dummy 由 XQspiPs 驱动自动注入。
		 */
		WriteBuffer[COMMAND_OFFSET]   = FAST_READ_CMD;
		WriteBuffer[ADDRESS_1_OFFSET] = (uint8_t)((CurAddr & 0xFF0000) >> 16);
		WriteBuffer[ADDRESS_2_OFFSET] = (uint8_t)((CurAddr & 0xFF00) >> 8);
		WriteBuffer[ADDRESS_3_OFFSET] = (uint8_t)(CurAddr & 0xFF);

		/* ByteCount = 数据 + dummy + overhead */
		Status = XQspiPs_PolledTransfer(QspiInstancePtr, WriteBuffer,
				ReadBuffer, Length + DUMMY_SIZE + OVERHEAD_SIZE);
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

int qspi_drv_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
	uint32_t Remaining = len;
	uint32_t Offset = 0;

	while (Remaining > 0) {
		/* NOR 页编程不得跨 256B 页边界，按页边界分片 */
		uint32_t PageLeft = QSPI_PAGE_SIZE - ((addr + Offset) % QSPI_PAGE_SIZE);
		uint32_t Length   = (Remaining < PageLeft) ? Remaining : PageLeft;
		uint32_t CurAddr  = addr + Offset;
		int Status;

		WriteBuffer[COMMAND_OFFSET]   = PP_CMD;
		WriteBuffer[ADDRESS_1_OFFSET] = (uint8_t)((CurAddr & 0xFF0000) >> 16);
		WriteBuffer[ADDRESS_2_OFFSET] = (uint8_t)((CurAddr & 0xFF00) >> 8);
		WriteBuffer[ADDRESS_3_OFFSET] = (uint8_t)(CurAddr & 0xFF);
		memcpy(&WriteBuffer[DATA_OFFSET], data + Offset, Length);

		QspiWriteEnable();
		Status = XQspiPs_PolledTransfer(QspiInstancePtr, WriteBuffer, NULL,
				DATA_OFFSET + Length);
		if (Status != XST_SUCCESS) {
			return -1;
		}
		QspiWaitBusy();

		Offset    += Length;
		Remaining -= Length;
	}
	return 0;
}

int qspi_drv_erase_4k(uint32_t addr)
{
	uint8_t Cmd[4];
	int Status;

	Cmd[0] = BE_4K_CMD;
	Cmd[1] = (uint8_t)((addr & 0xFF0000) >> 16);
	Cmd[2] = (uint8_t)((addr & 0xFF00) >> 8);
	Cmd[3] = (uint8_t)(addr & 0xFF);

	QspiWriteEnable();
	Status = XQspiPs_PolledTransfer(QspiInstancePtr, Cmd, NULL, 4);
	if (Status != XST_SUCCESS) {
		return -1;
	}
	QspiWaitBusy();
	return 0;
}

int qspi_drv_read_sr(int which)
{
	uint8_t Cmd[4] = { 0, 0, 0, 0 };
	uint8_t Buf[4] = { 0, 0, 0, 0 };
	int Status;

	switch (which) {
	case 1: Cmd[0] = 0x05; break;                            /* RDSR1 */
	case 2: Cmd[0] = 0x35; break;                            /* RDSR2 */
	case 3: Cmd[0] = 0x15; break;                            /* RDSR3 */
	default: return -1;
	}

	Status = XQspiPs_PolledTransfer(QspiInstancePtr, Cmd, Buf, 2);
	if (Status != XST_SUCCESS) {
		return -1;
	}
	return (int)Buf[1];
}
