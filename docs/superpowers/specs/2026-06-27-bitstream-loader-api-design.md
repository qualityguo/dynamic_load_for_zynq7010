# Bitstream 加载 API 设计

> 起始时间：2026-06-27
>
> 目标：为 new_ssbl 重构一个分层清晰、可复用、可测试的 bitstream 加载 API，
> 取代旧 `ssbl/src/loader/bitstream_loader.c` 里那个把"取数据 + 解析 `.bit` 头 +
> PCAP 编程"耦合在一起的单体函数 `bit_loader_download(path)`。
>
> 本文档是 brainstorming 讨论结论的逐条沉淀。讨论顺序 = 架构 → 契约 → 生命周期 → 同步模型。

---

## 0. 决策摘要

| # | 决策点 | 选择 |
|---|---|---|
| 1 | 架构形态 | **B. 缓冲区核心 + 路径壳**：核心 `bitstream_program(buf,len)` 与数据来源解耦；`bitstream_load_file(path)` 是 FileX 读到 DDR 再调核心的薄壳 |
| 2 | `.bit` 头处理 | **核心要求必须带 `.bit` 头**，自动剥头；签名缺失直接报错，**不做 raw bin 回退** |
| 3 | 初始化生命周期 | **i. 显式两步**：`bitstream_init()` 开机一次（AppTaskStart 里调）+ `bitstream_program()` 每次下载 |
| 4 | 完成等待 | **轮询**：busy-spin 读 `INT_STS`，`tx_time_get()` 超时；不接 GIC 中断 |

---

## 1. 背景：现状与问题

旧 `ssbl/src/loader/bitstream_loader.c` 的 `bit_loader_download(path)` 已能工作，但：

1. **三职责耦合**：FileX/SD 取数据、`.bit` 头解析、devcfg/PCAP 编程混在一个 285 行函数里。
2. **绑死旧存储抽象**：用 `storage_file_open/read/close`（`storage_ops_t` 虚表），而重构计划 §1 已砍掉该层，新代码须直接用 FileX。
3. **不可复用**：换数据源（UART/网络下发）就得改函数内部；PCAP 编程段无法脱离文件单独测试。
4. **错误码散乱**：返回 `-1/-3/-7...` 这类裸负数，无类型、无集中定义。

---

## 2. 公共接口（`bitstream_loader.h`）

```c
#ifndef __BITSTREAM_LOADER_H__
#define __BITSTREAM_LOADER_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 错误码（见 §3）*/
typedef enum {
    BIT_OK          =  0,
    BIT_ERR_INIT    = -1,   /* bitstream_init 失败（devcfg 实例 / 电平转换器）*/
    BIT_ERR_INVAL   = -2,   /* 参数非法（NULL / 未 4 字节对齐 / len 太小）*/
    BIT_ERR_FORMAT  = -3,   /* 非 .bit（缺 00 09 0F F0 签名 / 头解析失败），不回退 raw */
    BIT_ERR_EMPTY   = -4,   /* 剥头后 payload 为空 */
    BIT_ERR_FABRIC  = -5,   /* PROG_B 脉冲后 INIT 未就绪 */
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
 * 形参 buf : 指向 DDR 中完整 .bit 数据（含头），须 4 字节对齐
 * 形参 len : buf 字节数
 * 契约    : 函数就地破坏 buf（剥头 memmove + 字节反序），返回后调用方不得再用 buf
 * 前提    : bitstream_init() 已调用
 * 返回    : BIT_OK 成功；负值见 bit_err_t */
bit_err_t  bitstream_program(uint8_t *buf, uint32_t len);

/* 壳：FileX 读 .bit 到内部 DDR staging 区 → 调 bitstream_program。
 * 形参 path : 介质上的 .bit 路径，如 "pl_a.bit"
 * 返回    : BIT_OK 成功；负值见 bit_err_t */
bit_err_t  bitstream_load_file(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* __BITSTREAM_LOADER_H__ */
```

**设计要点：**

- `buf` 用**非 `const`**：类型本身即声明"会就地改写 buffer"的契约。
- 三个公共函数职责分明：`init` = 让硬件可用；`program` = 做一次配置周期；`load_file` = 取数据 + 委托 `program`。
- 错误码用类型化 `bit_err_t`，集中定义，取代旧裸负数。

---

## 3. 错误码语义

| 错误码 | 触发点 | 含义 |
|---|---|---|
| `BIT_OK` | 全部 | 成功 |
| `BIT_ERR_INIT` | `bitstream_init` | `XDcfg_LookupConfig` / `CfgInitialize` 失败，或电平转换器写入异常 |
| `BIT_ERR_INVAL` | `program` / `load_file` | `program`：`buf==NULL`、`len<20`、或 `buf` 未 4 字节对齐；`load_file`：`path==NULL` 或空串 |
| `BIT_ERR_FORMAT` | `program` | `buf[0..3]` 非 `{00 09 0F F0}`，或 TLV 字段 `a..e` 解析中断。**不回退当 raw bin** |
| `BIT_ERR_EMPTY` | `program` | 剥头后 `payload_size==0` |
| `BIT_ERR_FABRIC` | `program` | PROG_B 脉冲后 `PCFG_INIT` 未按预期拉低/拉高（PL 未就绪） |
| `BIT_ERR_DMA` | `program` | `XDcfg_Transfer` 返回非 `XST_SUCCESS` |
| `BIT_ERR_TIMEOUT` | `program` | 轮询 `D_P_DONE` 超时（默认 500 tick ≈ 5s） |
| `BIT_ERR_PL` | `program` | DMA done 但 `PCFG_DONE` 未拉高（典型为字节序错误） |
| `BIT_ERR_IO` | `load_file` | `fx_file_open/read/close` 任一返回非 `FX_SUCCESS` |
| `BIT_ERR_TOO_BIG` | `load_file` | 文件大小超过 staging 区（4MB） |

---

## 4. 分层与文件结构

```
new_ssbl/src/loader/
├── bitstream_loader.c    新增（移植自旧 ssbl，按本框架重构）
├── bitstream_loader.h    新增（§2 接口 + §3 错误码）
└── handoff.c/.h/.S       已有（jump_to_app，与本模块并列）
```

对应重构计划 §3.1 的 `utils/loader/`（new_ssbl 当前用扁平 `loader/`，与现有 `handoff.c` 同目录）。

**`bitstream_loader.c` 内部结构：**

```
[public]  bitstream_init()        一次性：XDcfg 实例 + LVL_PS_PL
[public]  bitstream_program()     核心：剥头 → 反序 → PROG_B → DMA → 轮询 DONE
[public]  bitstream_load_file()   壳：fx_file_* 读 staging → program
[static]  skip_bit_header()       解析 .bit TLV；签名缺失即失败
[static]  wait_pcfg_init()        等 PCFG_INIT 电平 + tick 超时
[static]  pcap_poll()             轮询 INT_STS 位 + tick 超时
[static]  swap_words()            就地 32 位字反序（big-endian → devcfg 小端）
```

---

## 5. 数据流

### 5.1 `bitstream_init()`（开机一次，AppTaskStart）

```
XDcfg_LookupConfig(XPAR_XDCFG_0_DEVICE_ID)
  → XDcfg_CfgInitialize(&s_dcfg, cfg, cfg->BaseAddr)   只填实例结构体（不动 CTRL）
  → Xil_Out32(PS_LVL_SHFTR_EN, 0xA)                    开 PS→PL 电平转换器
  → s_ready = 1
```

- 幂等：`s_ready` 已置位则直接返回 `BIT_OK`。
- 寄存器级状态继承自 FSBL（spec/旧代码 §7.5），`CfgInitialize` 不重置 CTRL。

### 5.2 `bitstream_program(buf, len)`（核心，每次下载）

```
1. 校验：buf!=NULL, len>=20, ((uintptr_t)buf & 3)==0   → 否则 BIT_ERR_INVAL
2. 校验签名：buf[0..3] == {0x00,0x09,0x0F,0xF0}        → 否则 BIT_ERR_FORMAT
3. off = skip_bit_header(buf, len)                      顺序解析字段 a..e
4. payload_size = len - off；若 0                       → BIT_ERR_EMPTY
5. memmove(buf, buf+off, payload_size)                  就地剥头（buf 已 4 字节对齐）
   若 (payload_size & 3)!=0 则告警并向下取整为整字
6. swap_words(buf, words)                               每 32 位字字节反序
7. Xil_DCacheFlush()                                    确保 DDR 与 PCAP DMA 一致
8. CTRL |= PCAP_PR | PCAP_MODE
9. PROG_B 脉冲：H → L(等 INIT 低, ≤500ms) → H(等 INIT 高, ≤1s)
                                                        → 失败 BIT_ERR_FABRIC
10. XDcfg_Transfer(buf | PCAP_LAST_TRANSFER, words,
                   XDCFG_DMA_INVALID_ADDRESS, 0,
                   XDCFG_NON_SECURE_PCAP_WRITE)         → 失败 BIT_ERR_DMA
11. pcap_poll(D_P_DONE, 500 ticks)                      → 超时 BIT_ERR_TIMEOUT
12. (INT_STS & PCFG_DONE) != 0 ?                        → 未拉高 BIT_ERR_PL
```

**关键实现 note（继承自旧代码，已验证）：**

- **源地址 bit0 置 `PCAP_LAST_TRANSFER`**：否则 devcfg DMA 不知是最后一笔，永不置 `DMA_DONE`。
- **目标 `XDCFG_DMA_INVALID_ADDRESS`**：表示"写往 PL fabric"而非 DDR 某地址。
- **字节反序**：`.bit` 每 32 位字按大端存（同步字字节 `AA 99 55 66`），devcfg DMA 按小端读 DDR，不反序则 PL 找不到同步字、`DONE` 永不拉高。
- **`XDcfg_Transfer` 的 `SrcWordLength` 单位是 32 位"字"**，不是字节。
- **PROG_B 脉冲不可省**：FSBL 无 bit 时不调 `FabricInit`，故 SSBL 必须自己做电平转换器 + PROG_B；否则 `XDcfg_Transfer` 只发起 DMA，PL 不会真正配置（现象：打印 OK 但灯不亮）。

### 5.3 `bitstream_load_file(path)`（壳）

```
1. 校验 path 非 NULL/非空                                → 否则 BIT_ERR_INVAL
2. fx_file_open(&g_fx_media, &s_fx_file, path, FX_OPEN_FOR_READ)
                                                        → 失败 BIT_ERR_IO
3. 循环 fx_file_read → DDR staging(0x00800000, 4MB)
   累计 total > BIT_MAX_SIZE                            → BIT_ERR_TOO_BIG
4. fx_file_close                                        → 失败 BIT_ERR_IO
5. return bitstream_program(staging, total)             委托核心
```

---

## 6. 关键约定

### 6.1 DDR staging 区

- 地址 `0x00800000`，大小 4MB（`BIT_MAX_SIZE`）。
- 选址避开 SSBL（`0x00100000`）与 APP（`0x01000000`）；Zynq-7010 PL bitstream ~1.6MB，4MB 足够。
- 基地址 4 字节对齐，满足 devcfg DMA 要求。
- `bitstream_load_file` 私有，不暴露给调用方。

### 6.2 buffer 契约

- `bitstream_program` **就地破坏** `buf`：`memmove` 剥头 + `swap_words` 反序。返回后调用方不得再使用 `buf`。
- `bitstream_load_file` 用内部 staging 区，不污染调用方 buffer。
- 对齐：`program` 要求 `buf` 4 字节对齐；不对齐返回 `BIT_ERR_INVAL`。`load_file` 的 staging 区天然对齐。

### 6.3 完成等待

- **轮询**：`pcap_poll` busy-spin 读 `INT_STS`，`tx_time_get()` 计超时（默认 500 tick ≈ 5s）。
- 不接 GIC 中断：boot 是一次性阻塞下载，DMA 秒级完成，中断驱动收益小、复杂度高。

### 6.4 线程安全 / 并发

- PCAP 硬件不可重入，**假定 boot 路径单一调用者**（auto_boot 线程）。
- `load_file` 的 `fx_file_*` 访问 FileX，应在 `g_storage_mutex` 下（该互斥在 `AppObjCreate` 建好后；现阶段 boot 单线程先不加锁）。
- 若将来 CLI `test bitstream` 与 auto_boot 并发，调用方需自行串行化，或后续在模块内加内部 mutex。

---

## 7. 与旧代码的差异（迁移要点）

| 旧 `bit_loader_download(path)` | 新 API |
|---|---|
| 单体函数，三职责耦合 | 三函数分层：`init` / `program` / `load_file` |
| `storage_file_open/read/close` | `fx_file_open/read/close`（直接 FileX） |
| 裸负数错误码 | 类型化 `bit_err_t` |
| raw bin 回退（签名不符当 raw） | **不回退**，签名不符直接 `BIT_ERR_FORMAT` |
| 懒初始化（devcfg + fabric 每次都做） | `init` 一次性做 devcfg 实例 + 电平转换器；`program` 每次做 PROG_B+DMA |
| `xil_printf` 日志 | 沿用 `ssbl_printf`（EasyLogger 接入前） |

---

## 8. 验收标准

1. `bitstream_init()` 在 AppTaskStart 调用成功，devcfg 实例就绪、电平转换器开启。
2. `bitstream_load_file("pl_a.bit")` 能把 PL 配置成功，`PCFG_DONE` 拉高（板载 LED 按新 bitstream 亮）。
3. 传 raw bin（无 `.bit` 签名）给 `bitstream_program` → 返回 `BIT_ERR_FORMAT`，不误烧。
4. 传未对齐 `buf` → 返回 `BIT_ERR_INVAL`。
5. 拔掉 SD 卡 / 文件不存在 → `bitstream_load_file` 返回 `BIT_ERR_IO`，不崩。

---

## 9. 非目标（YAGNI）

- 不支持 partial reconfiguration（仅全配置）。
- 不支持中断驱动 DMA（轮询足够）。
- 不暴露 init/program/done 多步状态机（三函数足够）。
- 不做 `.bit` 字段 b/c/d（设计名/时间戳/器件名）的语义校验，只用来定位 payload。
- 不做日志落盘 / hexdump。
