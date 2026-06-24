/*
*********************************************************************************************************
*
*	模块名称 : bitstream 动态加载（spec §4.2 + §6.2，Phase 9）
*	文件名称 : bitstream_loader.c
*	版    本 : V1.0
*	说    明 : 读 SD 卡上的 .bit 文件 → 搬到 DDR 临时区 → 跳过 Xilinx .bit 头
*	           → 经 devcfg/PCAP 把 payload 下载到 PL。
*
*	           实现 note（与 plan 的偏差，已修正）：
*	             - 不 extern FSBL 的 PCAP 实例：FSBL 的 XDcfg 实例是 static，且 FSBL
*	               与 SSBL 是两个独立 ELF，地址空间隔离，无法 extern。改用 SSBL 自己
*	               BSP 的 xdevcfg 驱动（devcfg_v3_7，已在 SSBL libxil）建本地实例。
*	             - spec §7.5：FSBL 初始化的 devcfg 寄存器状态 SSBL 可继承。SSBL 调
*	               XDcfg_CfgInitialize 只填实例结构体（不动 CTRL 寄存器），再补设
*	               PCAP_MODE/PCAP_PR 后直接 XDcfg_Transfer。
*	             - XDcfg_Transfer 的 SrcWordLength 单位是 32 位"字"，不是字节。
*
*	           下载后 PL 即被新 bitstream 配置（spec §6.2 test bitstream / boot.cfg bit=）。
*
*********************************************************************************************************
*/

#include "storage.h"
#include "xil_printf.h"
#include "xil_cache.h"
#include "xil_io.h"
#include "xparameters.h"
#include "xdevcfg.h"
#include "xdevcfg_hw.h"
#include "tx_api.h"
#include <stdint.h>
#include <string.h>

/* .bit 在 DDR 的临时存放区：避开 0x00100000 SSBL / 0x01000000 APP。
 * 0x00800000 起 4MB，足够 Zynq-7010 PL（~1.6MB bit）。基地址 4 字节对齐。*/
#define BIT_DDR_TEMP    0x00800000u
#define BIT_MAX_SIZE    (4u << 20)

/* PS→PL 电平转换器使能寄存器（SLCR，spec/FabricInit 用）。LVL_PS_PL=0xA 开 PS→PL。*/
#define PS_LVL_SHFTR_EN 0xF8000900u
#define LVL_PS_PL       0x0000000Au

/* devcfg DMA "最后一笔传输"标志：置源地址 bit0，DMA 完成后才会置 DMA_DONE
 * （源自 FSBL pcap.h 的 PCAP_LAST_TRANSFER）。*/
#define PCAP_LAST_TRANSFER  1u

/* devcfg / PCAP 实例（SSBL 自有，BSP xdevcfg 驱动）。懒初始化一次。*/
static XDcfg s_dcfg;
static int    s_dcfg_ready = 0;

/* PCAP 完成轮询超时（tick，1 tick≈10ms）。2MB bit 经 PCAP 远小于 5s。*/
#define PCAP_DONE_TIMEOUT_TICKS  500u

/* 解析 Xilinx .bit 头，返回 bitstream payload 起始偏移（字节）。
 * 实测格式（Vivado，如 pl_a.bit）：
 *   固定 13 字节前缀：00 09 0F F0 0F F0 0F F0 0F F0 00 00 01（字段 'a' 固定在偏移 13）
 *   字段 a/b/c/d：tag(1) + 2B big-endian 长度 + data
 *   字段 e      ：tag(1) + 4B big-endian "bitstream 字节数"，其后即 bitstream payload
 *                 （'e' 不是 a..d 那样的 TLV；其后的 4B 即 bitstream 长度值本身）
 * 签名 00 09 0F F0 不匹配则视为 raw bin，返回 0。*/
static uint32_t skip_bit_header(const uint8_t *buf, uint32_t total)
{
    uint32_t off = 13u;   /* Xilinx .bit 固定 13 字节前缀 */

    if (total < 20) return 0;   /* 太小，当 raw bin */

    /* .bit 签名：00 09 0F F0（所有 Xilinx 工具一致）。不匹配则当 raw bin。*/
    if (!(buf[0] == 0x00 && buf[1] == 0x09 &&
          buf[2] == 0x0F && buf[3] == 0xF0))
        return 0;

    /* 顺序处理字段 a..d（tag + 2B 长度 + data）。遇到 'e' 走特殊路径。*/
    while (off + 3u <= total) {
        uint8_t  tag = buf[off];
        uint16_t fl;

        if (tag == 'e') {
            /* 'e'：tag(1) + 4B big-endian bitstream 字节数，其后即 payload。*/
            off += 1u + 4u;
            break;
        }
        if (tag < 'a' || tag > 'd') break;   /* 异常：停在当前位置（当 raw payload 起点）*/
        fl = (uint16_t)(((uint16_t)buf[off + 1] << 8) | buf[off + 2]);
        off += 3u + (uint32_t)fl;
    }
    return off;
}

/* 懒初始化 SSBL 自有 devcfg 实例（只填结构体 + 开锁）。spec §7.5：寄存器级状态继承
 * 自 FSBL，此处只确保实例 IsReady（XDcfg_Transfer 断言要求）。PCAP 模式位在 fabric_init 设。*/
static int bit_pcap_init(void)
{
    XDcfg_Config *cfg;

    if (s_dcfg_ready) return 0;

    cfg = XDcfg_LookupConfig(XPAR_XDCFG_0_DEVICE_ID);
    if (cfg == NULL) {
        xil_printf("[bit] XDcfg_LookupConfig failed\r\n");
        return -1;
    }
    if (XDcfg_CfgInitialize(&s_dcfg, cfg, cfg->BaseAddr) != XST_SUCCESS) {
        xil_printf("[bit] XDcfg_CfgInitialize failed\r\n");
        return -2;
    }
    s_dcfg_ready = 1;
    return 0;
}

/* 等 PCFG_INIT 到指定电平（want_high=1 等高，0 等低），带 tick 超时。
 * 用 tx_time_get 计时（busy-spin 检查，INIT 跳变是微秒级）。0=OK，-1=超时。*/
static int wait_pcfg_init(int want_high, uint32_t ticks)
{
    u32 base = s_dcfg.Config.BaseAddr;
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

/* Fabric 初始化：复刻 FSBL FabricInit() 的关键步骤（spec §7.5 继承假设不充分——
 * FSBL 无 bit 时不调 FabricInit，故 SSBL 必须自己做）：
 *   1) 开 PS→PL 电平转换器（不开则 PCAP 数据物理上到不了 PL）
 *   2) PCAP_MODE/PCAP_PR 置位
 *   3) PROG_B 脉冲：high→low（等 INIT 低）→ high（等 INIT 高），触发一次全新配置周期
 * 没有这一步，XDcfg_Transfer 只是发起 DMA，PL 不会真正配置（现象：打印 OK 但灯不亮）。*/
static int bit_fabric_init(void)
{
    u32 base = s_dcfg.Config.BaseAddr;
    u32 ctrl;

    /* 1. PS→PL 电平转换器。*/
    Xil_Out32(PS_LVL_SHFTR_EN, LVL_PS_PL);

    /* 2. CTRL：保留现有位，置 PCAP_PR | PCAP_MODE，作后续 PROG_B 脉冲的基值。*/
    ctrl  = XDcfg_ReadReg(base, XDCFG_CTRL_OFFSET);
    ctrl |= (XDCFG_CTRL_PCAP_PR_MASK | XDCFG_CTRL_PCAP_MODE_MASK);

    /* 3. PROG_B 脉冲。*/
    XDcfg_WriteReg(base, XDCFG_CTRL_OFFSET, ctrl | XDCFG_CTRL_PCFG_PROG_B_MASK);
    XDcfg_WriteReg(base, XDCFG_CTRL_OFFSET, ctrl & (u32)~XDCFG_CTRL_PCFG_PROG_B_MASK);
    if (wait_pcfg_init(0, 50u) != 0) {     /* 等 INIT 拉低（≤500ms）*/
        xil_printf("[bit] WARN: PCFG_INIT did not go low after PROG_B\r\n");
    }
    XDcfg_WriteReg(base, XDCFG_CTRL_OFFSET, ctrl | XDCFG_CTRL_PCFG_PROG_B_MASK);
    if (wait_pcfg_init(1, 100u) != 0) {    /* 等 INIT 拉高（≤1s）*/
        xil_printf("[bit] ERR: PCFG_INIT did not go high, PL not ready\r\n");
        return -3;
    }
    return 0;
}

/* 轮询 PCAP 完成（XDcfg_Transfer 非阻塞，发起后立即返回）。mask 命中清位并返回 0，
 * 超时返回 -1。*/
static int pcap_poll(u32 mask, uint32_t ticks)
{
    u32 base = s_dcfg.Config.BaseAddr;
    ULONG start = tx_time_get();
    for (;;) {
        if (XDcfg_ReadReg(base, XDCFG_INT_STS_OFFSET) & mask) {
            XDcfg_IntrClear(&s_dcfg, mask);
            return 0;
        }
        if ((tx_time_get() - start) >= ticks)
            return -1;
    }
}

int bit_loader_download(const char *path)
{
    uint8_t *dst = (uint8_t *)BIT_DDR_TEMP;
    uint32_t total = 0;
    uint32_t payload_off, payload_size, words;
    int rc;

    if (path == NULL || path[0] == '\0') return -1;

    /* 1. 读整文件到 DDR 临时区（分块，遇 EOF/超限停）。*/
    rc = storage_file_open(path, STORAGE_OPEN_READ);
    if (rc != STORAGE_OK) {
        xil_printf("[bit] open %s failed (%d)\r\n", path, rc);
        return rc;
    }
    while (total < BIT_MAX_SIZE) {
        uint32_t want = BIT_MAX_SIZE - total;
        int n = storage_file_read(dst + total, want);
        if (n <= 0) break;          /* EOF */
        total += (uint32_t)n;
    }
    storage_file_close();
    if (total == 0) {
        xil_printf("[bit] %s empty / unreadable\r\n", path);
        return -3;
    }
    xil_printf("[bit] %s loaded: %u bytes\r\n", path, (unsigned)total);

    /* 2. 跳 Xilinx .bit 头。*/
    payload_off  = skip_bit_header(dst, total);
    payload_size = total - payload_off;
    if (payload_size == 0) {
        xil_printf("[bit] empty payload after header\r\n");
        return -4;
    }

    /* 3. PCAP 源必须 4 字节对齐：把 payload 搬到 4 字节对齐的缓冲基地址。
     *    BIT_DDR_TEMP 已对齐；memmove 处理重叠（前移）。*/
    if (payload_off != 0) {
        memmove(dst, dst + payload_off, payload_size);
    }
    /* payload 应为整字；向下取整并告警非整字尾部。*/
    if ((payload_size & 3u) != 0) {
        xil_printf("[bit] WARN: payload size %u not word-aligned, truncating\r\n",
                   (unsigned)payload_size);
    }
    words = payload_size >> 2;
    xil_printf("[bit] payload @0x%08X words=%u\r\n",
               (unsigned)(uintptr_t)dst, (unsigned)words);

    /* 4. 字节序转换：.bit 的 bitstream 每个字按大端存（同步字字节 AA 99 55 66），
     *    但 devcfg DMA 按小端读 DDR → 喂给 PL 的是 0x665599AA，而 PL 期望
     *    0xAA995566。故把每个 32 位字的 4 字节反序。否则 DMA 能完成（PL 会排空
     *    数据）但永远找不到同步字 → DONE 不拉高（现象：DMA done 但灯不亮）。
     *    （bootgen 生成 BOOT.bin 时已做此交换，故 FSBL 喂原数据即可；SSBL 直接
     *    读 .bit 需自己交换。）*/
    {
        uint32_t *p = (uint32_t *)(void *)dst;
        uint32_t i;
        for (i = 0; i < words; i++) {
            uint32_t w = p[i];
            p[i] = ((w & 0x000000FFu) << 24) |
                   ((w & 0x0000FF00u) << 8)  |
                   ((w & 0x00FF0000u) >> 8)  |
                   ((w & 0xFF000000u) >> 24);
        }
    }

    /* 5. clean D-cache：确保交换后的 DDR 与 PCAP DMA 一致。*/
    Xil_DCacheFlush();

    /* 6. 初始化 devcfg 实例 + Fabric（电平转换器 + PROG_B 脉冲 + INIT 就绪）。*/
    rc = bit_pcap_init();
    if (rc != 0) return rc;
    rc = bit_fabric_init();
    if (rc != 0) return rc;

    /* 7. 发起 PCAP 写（XDcfg_Transfer 非阻塞，仅发起 DMA）。
     *    关键（与 FSBL 一致）：源地址 bit0 置 PCAP_LAST_TRANSFER(1)，否则 devcfg DMA
     *    不知道这是最后一笔传输，永不置 DMA_DONE → "DMA did not complete"。
     *    目标用 XDCFG_DMA_INVALID_ADDRESS 表示"写往 PL fabric"（不是 DDR 某地址）。*/
    {
        u32 st = XDcfg_Transfer(&s_dcfg,
                                (void *)((UINTPTR)dst | PCAP_LAST_TRANSFER), words,
                                (void *)XDCFG_DMA_INVALID_ADDRESS, 0,
                                XDCFG_NON_SECURE_PCAP_WRITE);
        if (st != XST_SUCCESS) {
            xil_printf("[bit] PCAP transfer initiate failed (0x%08lX)\r\n",
                       (unsigned long)st);
            return -5;
        }
    }

    /* 8. 等 DMA+PCAP 完成（XDCFG_IXR_D_P_DONE_MASK）。*/
    if (pcap_poll(XDCFG_IXR_D_P_DONE_MASK, PCAP_DONE_TIMEOUT_TICKS) != 0) {
        xil_printf("[bit] ERR: PCAP DMA did not complete\r\n");
        return -6;
    }

    /* 9. 确认 PL DONE 拉高（真正配置成功的判据；不亮即数据/字节序问题）。*/
    if ((XDcfg_ReadReg(s_dcfg.Config.BaseAddr, XDCFG_INT_STS_OFFSET) &
         XDCFG_IXR_PCFG_DONE_MASK) == 0) {
        xil_printf("[bit] ERR: DMA done but PL DONE not asserted (check byte order)\r\n");
        return -7;
    }

    xil_printf("[bit] PL configured OK (DONE asserted)\r\n");
    return 0;
}

/***************************** (END OF FILE) *********************************/
