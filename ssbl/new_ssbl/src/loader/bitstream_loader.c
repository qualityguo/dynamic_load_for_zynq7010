/*
*********************************************************************************************************
*
*	模块名称 : bitstream 动态加载 API
*	文件名称 : bitstream_loader.c
*	版    本 : V1.0
*	说    明 : 分层实现：
*	             - bitstream_init()      开机一次：建 devcfg 实例 + 开 PS→PL 电平转换器
*	             - bitstream_program()   核心：校验 .bit 头 → 剥头 → 字节反序
*	                                    → PROG_B 脉冲 → PCAP DMA → 轮询 DONE
*	             - bitstream_load_file() 壳：FileX 读 .bit 到 DDR staging → program
*
*	           实现 note（继承自旧 ssbl 的已验证逻辑）：
*	             - 不 extern FSBL 的 PCAP 实例（独立 ELF，地址空间隔离），
*	               用 SSBL 自有 BSP 的 xdevcfg 驱动建本地实例。
*	             - FSBL 初始化的 devcfg 寄存器状态 SSBL 可继承，CfgInitialize 只填结构体。
*	             - .bit 每 32 位字按大端存，devcfg DMA 按小端读 DDR，须字节反序，
*	               否则 PL 找不到同步字、DONE 永不拉高。
*	             - XDcfg_Transfer 的 SrcWordLength 单位是 32 位"字"，不是字节。
*	             - 源地址 bit0 置 PCAP_LAST_TRANSFER，DMA 才会置 DMA_DONE。
*	             - PROG_B 脉冲不可省：FSBL 无 bit 时不调 FabricInit，SSBL 必须自己做，
*	               否则 XDcfg_Transfer 只发起 DMA，PL 不会真正配置（现象：打印 OK 但灯不亮）。
*
*********************************************************************************************************
*/

#include "ssbl.h"
#include "bitstream_loader.h"
#include <string.h>

/* xil_cache.c 中有实现但 xil_cache.h 未声明（BSP 版本差异），见 handoff.c */
extern void Xil_L2CacheFlush(void);

/* FileX 全局介质（boot_config.c 定义）*/
extern FX_MEDIA  g_fx_media;


/* DDR staging 区地址 / 大小来自 bitstream_loader.h（BITSTREAM_DDR_ADDR / _SIZE）。*/

/* PS→PL 电平转换器使能寄存器（SLCR）。LVL_PS_PL=0xA 开 PS→PL。*/
#define PS_LVL_SHFTR_EN         0xF8000900u
#define LVL_PS_PL               0x0000000Au

/* devcfg DMA "最后一笔传输"标志：置源地址 bit0，DMA 完成后才会置 DMA_DONE。*/
#define PCAP_LAST_TRANSFER      1u

/* PCAP 完成轮询超时（tick，1 tick≈10ms）。2MB bit 经 PCAP 远小于 5s。*/
#define PCAP_DONE_TIMEOUT_TICKS 500u

/* devcfg / PCAP 实例（SSBL 自有）。bitstream_init 里初始化一次。*/
static XDcfg   s_dcfg;
static int     s_ready = 0;

/* load_file 私有的 FileX 文件句柄 */
static FX_FILE s_fx_file;


/*
*********************************************************************************************************
*	本地函数
*********************************************************************************************************
*/

/* 解析 Xilinx .bit 头，返回 bitstream payload 起始偏移（字节）。
 * 调用前已由 bitstream_program 校验过 00 09 0F F0 签名。
 * 固定 13 字节前缀；字段 a..d 为 tag(1)+2B big-endian 长度+data；
 * 字段 e 为 tag(1)+4B big-endian bitstream 字节数，其后即 payload。*/
static uint32_t skip_bit_header(const uint8_t *buf, uint32_t total)
{
	uint32_t off = 13u;   /* Xilinx .bit 固定 13 字节前缀 */

	if (total < 20u) return 0;

	/* 顺序处理字段 a..d（tag + 2B 长度 + data）。遇到 'e' 走特殊路径。*/
	while (off + 3u <= total) {
		uint8_t  tag = buf[off];
		uint16_t fl;

		if (tag == 'e') {
			/* 'e'：tag(1) + 4B big-endian bitstream 字节数，其后即 payload。*/
			off += 1u + 4u;
			break;
		}
		if (tag < 'a' || tag > 'd') break;   /* 异常：停在当前位置 */
		fl = (uint16_t)(((uint16_t)buf[off + 1] << 8) | buf[off + 2]);
		off += 3u + (uint32_t)fl;
	}
	return off;
}

/* 就地把 words 个 32 位字做字节反序（big-endian .bit → devcfg DMA 小端）。*/
static void swap_words(uint32_t *p, uint32_t words)
{
	uint32_t i;
	for (i = 0; i < words; i++) {
		uint32_t w = p[i];
		p[i] = ((w & 0x000000FFu) << 24) |
		       ((w & 0x0000FF00u) << 8)  |
		       ((w & 0x00FF0000u) >> 8)  |
		       ((w & 0xFF000000u) >> 24);
	}
}

/* 等 PCFG_INIT 到指定电平（want_high=1 等高，0 等低），带 tick 超时。
 * 0=OK，-1=超时（INIT 跳变是微秒级，用 busy-spin + tx_time_get）。*/
static int wait_pcfg_init(int want_high, uint32_t ticks)
{
	u32   base  = s_dcfg.Config.BaseAddr;
	ULONG start = tx_time_get();
	for (;;) {
		u32 init = XDcfg_ReadReg(base, XDCFG_STATUS_OFFSET) &
		           XDCFG_STATUS_PCFG_INIT_MASK;
		if (want_high ? (init != 0) : (init == 0))
			return 0;
		if ((tx_time_get() - start) >= ticks)
			return -1;
	}
}

/* Fabric 初始化（每次 program 调）：
 *   1) CTRL 置 PCAP_MODE/PCAP_PR
 *   2) PROG_B 脉冲：high→low（等 INIT 低）→ high（等 INIT 高），触发全新配置周期
 * 没有这步，XDcfg_Transfer 只发起 DMA，PL 不会真正配置。*/
static bit_err_t bit_fabric_init(void)
{
	u32 base = s_dcfg.Config.BaseAddr;
	u32 ctrl;

	/* 1. CTRL：保留现有位，置 PCAP_PR | PCAP_MODE。*/
	ctrl  = XDcfg_ReadReg(base, XDCFG_CTRL_OFFSET);
	ctrl |= (XDCFG_CTRL_PCAP_PR_MASK | XDCFG_CTRL_PCAP_MODE_MASK);

	/* 2. PROG_B 脉冲。*/
	XDcfg_WriteReg(base, XDCFG_CTRL_OFFSET, ctrl | XDCFG_CTRL_PCFG_PROG_B_MASK);
	XDcfg_WriteReg(base, XDCFG_CTRL_OFFSET, ctrl & (u32)~XDCFG_CTRL_PCFG_PROG_B_MASK);
	if (wait_pcfg_init(0, 50u) != 0) {     /* 等 INIT 拉低（≤500ms）*/
		ssbl_printf(LOG_INFO, "WARN: PCFG_INIT did not go low after PROG_B\r\n");
	}
	XDcfg_WriteReg(base, XDCFG_CTRL_OFFSET, ctrl | XDCFG_CTRL_PCFG_PROG_B_MASK);
	if (wait_pcfg_init(1, 100u) != 0) {    /* 等 INIT 拉高（≤1s）*/
		ssbl_printf(LOG_ERR, "ERR: PCFG_INIT did not go high, PL not ready\r\n");
		return BIT_ERR_FABRIC;
	}
	return BIT_OK;
}

/* 轮询 PCAP 完成（XDcfg_Transfer 非阻塞，发起后立即返回）。
 * mask 命中清位并返回 BIT_OK，超时返回 BIT_ERR_TIMEOUT。*/
static bit_err_t pcap_poll(u32 mask, uint32_t ticks)
{
	u32   base  = s_dcfg.Config.BaseAddr;
	ULONG start = tx_time_get();
	for (;;) {
		if (XDcfg_ReadReg(base, XDCFG_INT_STS_OFFSET) & mask) {
			XDcfg_IntrClear(&s_dcfg, mask);
			return BIT_OK;
		}
		if ((tx_time_get() - start) >= ticks)
			return BIT_ERR_TIMEOUT;
	}
}


/*
*********************************************************************************************************
*	公共 API
*********************************************************************************************************
*/

/* 开机一次性初始化：建 devcfg 实例 + 开 PS→PL 电平转换器。幂等。*/
bit_err_t bitstream_init(void)
{
	XDcfg_Config *cfg;

	if (s_ready) return BIT_OK;

	cfg = XDcfg_LookupConfig(XPAR_XDCFG_0_DEVICE_ID);
	if (cfg == NULL) {
		ssbl_printf(LOG_ERR, "XDcfg_LookupConfig failed\r\n");
		return BIT_ERR_INIT;
	}
	if (XDcfg_CfgInitialize(&s_dcfg, cfg, cfg->BaseAddr) != XST_SUCCESS) {
		ssbl_printf(LOG_ERR, "XDcfg_CfgInitialize failed\r\n");
		return BIT_ERR_INIT;
	}

	/* 开 PS→PL 电平转换器（不开则 PCAP 数据物理上到不了 PL）。*/
	Xil_Out32(PS_LVL_SHFTR_EN, LVL_PS_PL);

	s_ready = 1;
	ssbl_printf(LOG_INFO, "devcfg + level shifter ready\r\n");
	return BIT_OK;
}

/* 核心：含 .bit 头的 buffer → 剥头 → 反序 → PROG_B → PCAP DMA → 轮询 DONE。
 * 就地破坏 buf。*/
bit_err_t bitstream_program(uint8_t *buf, uint32_t len)
{
	uint32_t off, payload_size, words;
	u32      st;

	/* 1. 参数校验。*/
	if (buf == NULL || len < 20u) return BIT_ERR_INVAL;
	if (((uintptr_t)buf & 3u) != 0u) {
		ssbl_printf(LOG_ERR, "buf not 4-byte aligned\r\n");
		return BIT_ERR_INVAL;
	}
	if (!s_ready) {
		ssbl_printf(LOG_ERR, "not initialized\r\n");
		return BIT_ERR_INIT;
	}

	/* 2. 校验 .bit 签名 00 09 0F F0。不符直接报错，不回退 raw。*/
	if (!(buf[0] == 0x00 && buf[1] == 0x09 &&
	      buf[2] == 0x0F && buf[3] == 0xF0)) {
		ssbl_printf(LOG_ERR, "not a .bit file (bad signature)\r\n");
		return BIT_ERR_FORMAT;
	}

	/* 3. 跳 .bit 头。*/
	off = skip_bit_header(buf, len);
	payload_size = len - off;
	if (payload_size == 0u) {
		ssbl_printf(LOG_ERR, "empty payload after header\r\n");
		return BIT_ERR_EMPTY;
	}

	/* 4. 剥头：payload 前移到 4 字节对齐的 buf 基址。*/
	if (off != 0u) {
		memmove(buf, buf + off, payload_size);
	}
	if ((payload_size & 3u) != 0u) {
		ssbl_printf(LOG_INFO, "WARN: payload size %u not word-aligned, truncating\r\n",
		            (unsigned)payload_size);
	}
	words = payload_size >> 2;
	if (words == 0u) {
		ssbl_printf(LOG_ERR, "payload < 1 word\r\n");
		return BIT_ERR_EMPTY;
	}
	ssbl_printf(LOG_INFO, "payload @0x%08X words=%u\r\n",
	            (unsigned)(uintptr_t)buf, (unsigned)words);

	/* 5. 字节反序（big-endian .bit → devcfg DMA 小端）。*/
	swap_words((uint32_t *)(void *)buf, words);

	/* 6. clean D-cache：确保反序后的 DDR 与 PCAP DMA 一致（L1+L2 全部 flush）。*/
	Xil_DCacheFlush();
	Xil_L2CacheFlush();

	/* 7. Fabric：PCAP_MODE/PCAP_PR + PROG_B 脉冲 + 等 INIT 就绪。*/
	{
		bit_err_t rc = bit_fabric_init();
		if (rc != BIT_OK) return rc;
	}

	/* 8. 发起 PCAP 写（非阻塞，仅发起 DMA）。
	 *    关键：源地址 bit0 置 PCAP_LAST_TRANSFER，否则 devcfg DMA 不知道这是最后一笔，
	 *    永不置 DMA_DONE。目标 XDCFG_DMA_INVALID_ADDRESS 表示写往 PL fabric。*/
	st = XDcfg_Transfer(&s_dcfg,
	                    (void *)((UINTPTR)buf | PCAP_LAST_TRANSFER), words,
	                    (void *)XDCFG_DMA_INVALID_ADDRESS, 0,
	                    XDCFG_NON_SECURE_PCAP_WRITE);
	if (st != XST_SUCCESS) {
		ssbl_printf(LOG_ERR, "PCAP transfer initiate failed (0x%08lX)\r\n",
		            (unsigned long)st);
		return BIT_ERR_DMA;
	}

	/* 9. 等 DMA + PCAP 完成（D_P_DONE）。*/
	{
		bit_err_t rc = pcap_poll(XDCFG_IXR_D_P_DONE_MASK, PCAP_DONE_TIMEOUT_TICKS);
		if (rc != BIT_OK) {
			ssbl_printf(LOG_ERR, "ERR: PCAP DMA did not complete\r\n");
			return rc;
		}
	}

	/* 10. 确认 PL DONE 拉高（真正配置成功的判据；不亮即数据/字节序问题）。*/
	if ((XDcfg_ReadReg(s_dcfg.Config.BaseAddr, XDCFG_INT_STS_OFFSET) &
	     XDCFG_IXR_PCFG_DONE_MASK) == 0u) {
		ssbl_printf(LOG_ERR, "ERR: DMA done but PL DONE not asserted (check byte order)\r\n");
		return BIT_ERR_PL;
	}

	ssbl_printf(LOG_INFO, "PL configured OK (DONE asserted)\r\n");
	return BIT_OK;
}

/* 仅把 .bit 原始字节读入 DDR staging 区（BITSTREAM_DDR_ADDR），不触发配置。
 * 成功时 *out_size = 读到的字节数。*/
bit_err_t bitstream_load_file(const char *path, uint32_t *out_size)
{
	uint8_t *dst   = (uint8_t *)BITSTREAM_DDR_ADDR;
	uint32_t total = 0;
	UINT     status;
	ULONG    br;

	if (out_size != NULL) *out_size = 0u;
	if (path == NULL || path[0] == '\0') return BIT_ERR_INVAL;

	/* 1. 打开 .bit。*/
	status = fx_file_open(&g_fx_media, &s_fx_file, (CHAR *)path, FX_OPEN_FOR_READ);
	if (status != FX_SUCCESS) {
		ssbl_printf(LOG_ERR, "fx_file_open %s failed (0x%08X)\r\n",
		            path, (unsigned)status);
		return BIT_ERR_IO;
	}

	/* 2. 分块读到 DDR staging 区（不解析、不配置）。*/
	for (;;) {
		ULONG want;
		if (total >= BITSTREAM_DDR_SIZE) {        /* 读满 staging 仍没 EOF → 文件过大 */
			fx_file_close(&s_fx_file);
			ssbl_printf(LOG_ERR, "%s too big (>%u)\r\n",
			            path, (unsigned)BITSTREAM_DDR_SIZE);
			return BIT_ERR_TOO_BIG;
		}
		want = BITSTREAM_DDR_SIZE - total;
		status = fx_file_read(&s_fx_file, dst + total, want, &br);
		if (status != FX_SUCCESS && status != FX_END_OF_FILE) {
			ssbl_printf(LOG_ERR, "fx_file_read error (0x%08X)\r\n", (unsigned)status);
			fx_file_close(&s_fx_file);
			return BIT_ERR_IO;
		}
		if (br == 0u) break;                /* EOF: FileX 以 FX_END_OF_FILE 通知，actual=0 */
		total += (uint32_t)br;
	}
	fx_file_close(&s_fx_file);

	if (total == 0u) {
		ssbl_printf(LOG_ERR, "%s empty / unreadable\r\n", path);
		return BIT_ERR_IO;
	}
	ssbl_printf(LOG_INFO, "%s loaded: %u bytes @0x%08X\r\n",
	            path, (unsigned)total, (unsigned)(uintptr_t)dst);

	if (out_size != NULL) *out_size = total;
	return BIT_OK;
}

/***************************** (END OF FILE) *********************************/
