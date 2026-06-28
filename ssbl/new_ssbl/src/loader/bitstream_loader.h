/*
*********************************************************************************************************
*
*	模块名称 : bitstream 动态加载 API
*	文件名称 : bitstream_loader.h
*	版    本 : V1.0
*	说    明 : 分层 bitstream 加载：
*	             - bitstream_init()      开机一次：建 devcfg 实例 + 开 PS→PL 电平转换器
*	             - bitstream_program()   核心：含 .bit 头的 buffer → 剥头 → 字节反序
*	                                    → PROG_B 脉冲 → PCAP DMA → 轮询 DONE
*	             - bitstream_load_file() 壳：FileX 读 .bit 到 DDR staging → program
*
*	           核心要求 buffer 必须含 Xilinx .bit 头（00 09 0F F0 签名），否则报错，不回退 raw。
*
*********************************************************************************************************
*/

#ifndef __BITSTREAM_LOADER_H__
#define __BITSTREAM_LOADER_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 可配置项（编译期，可用 -D 覆盖）────────────────────────────────── */
/* DDR staging 区：load_file 把 .bit 原始字节读到这里，program 从这里烧写。
 * 避开 SSBL(0x00100000)/APP(0x01000000)；基地址须 4 字节对齐。*/
#ifndef BITSTREAM_DDR_ADDR
#define BITSTREAM_DDR_ADDR    0x00800000u
#endif
#ifndef BITSTREAM_DDR_SIZE
#define BITSTREAM_DDR_SIZE    (4u << 20)   /* 4MB，Zynq-7010 PL ~1.6MB 足够 */
#endif
/* ──────────────────────────────────────────────────────────────────── */

/* 错误码 */
typedef enum {
    BIT_OK          =  0,
    BIT_ERR_INIT    = -1,   /* bitstream_init 失败（devcfg 实例 / 电平转换器）*/
    BIT_ERR_INVAL   = -2,   /* 参数非法（buf NULL / 未 4 字节对齐 / len 太小 / path 空）*/
    BIT_ERR_FORMAT  = -3,   /* 非 .bit（缺 00 09 0F F0 签名 / 头解析失败），不回退 raw */
    BIT_ERR_EMPTY   = -4,   /* 剥头后 payload 为空 */
    BIT_ERR_FABRIC  = -5,   /* PROG_B 脉冲后 PCFG_INIT 未就绪 */
    BIT_ERR_DMA     = -6,   /* XDcfg_Transfer 发起失败 */
    BIT_ERR_TIMEOUT = -7,   /* DMA / DONE 轮询超时 */
    BIT_ERR_PL      = -8,   /* DMA done 但 PL DONE 未拉高（数据 / 字节序问题）*/
    BIT_ERR_IO      = -9,   /* FileX 读失败（load_file 用）*/
    BIT_ERR_TOO_BIG = -10,  /* 超出 staging 区 4MB（load_file 用）*/
} bit_err_t;

/* 开机一次性初始化：建 devcfg 实例 + 开 PS→PL 电平转换器。
 * 在 AppTaskStart 里调，与 AppFileXInit() 并列。重复调用幂等。
 * 返回：BIT_OK / BIT_ERR_INIT */
bit_err_t  bitstream_init(void);

/* 核心：把含 .bit 头的 bitstream 烧进 PL。
 * 形参 buf : DDR 中完整 .bit 数据（含头），须 4 字节对齐
 * 形参 len : buf 字节数
 * 契约    : 函数就地破坏 buf（剥头 memmove + 字节反序），返回后调用方不得再用 buf
 * 前提    : bitstream_init() 已调用
 * 返回    : BIT_OK 成功；负值见 bit_err_t */
bit_err_t  bitstream_program(uint8_t *buf, uint32_t len);

/* 仅把 .bit 文件原始字节读入 DDR staging 区（BITSTREAM_DDR_ADDR），不触发 FPGA 配置。
 * 需配置 FPGA 时，用返回的 size 调用：
 *     bitstream_program((uint8_t *)BITSTREAM_DDR_ADDR, size)
 * 形参 path     : 介质上的 .bit 路径，如 "pl_a.bit"
 * 形参 out_size : 成功时写入读到的字节数；不需要可传 NULL
 * 返回    : BIT_OK 成功；负值见 bit_err_t（BIT_ERR_IO / BIT_ERR_TOO_BIG / BIT_ERR_INVAL）*/
bit_err_t  bitstream_load_file(const char *path, uint32_t *out_size);

#ifdef __cplusplus
}
#endif

#endif /* __BITSTREAM_LOADER_H__ */
