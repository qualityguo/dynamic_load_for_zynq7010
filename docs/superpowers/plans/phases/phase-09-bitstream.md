> **章节导航**：
  [← 上一章：YMODEM 两阶段升级](phase-08-ymodem-upgrade.md) ｜ [↑ 返回总述](../2026-06-22-zynq-multistage-boot.md) ｜ [下一章：错误处理 + LED 错误码 + OCM 标记 →](phase-10-error-handling.md)

> **Phase 元信息**
> - 对应 Spec：`§4.2`
> - 里程碑：`M9`
> - 交付物：bitstream 动态加载（FileX + PCAP）
> - 分章文件：`phases/phase-09-bitstream.md`

> **接口定位**：本章对其他 Phase 的依赖与对外暴露的符号，见
> [总述章 · 模块依赖矩阵](../2026-06-22-zynq-multistage-boot.md#3-模块依赖矩阵)。
> 新增功能时，先查该清单确认接口落点，再回本章扩展。

---
## Phase 9：bitstream 动态加载（M9）

**目标**：实现 spec §4.2 + §6.2 `test bitstream` 命令。SSBL 读 `.bit` 文件、搬到 DDR、通过 PCAP（devcfg）触发 PL 下载。

**Spec 引用**：§4.2（bit 文件格式）、§6.2 `test bitstream`、§1.3 决策（FSBL 不下 bit）。

### Task 9.1：用 FSBL 的 pcap.c API（devcfg 驱动）

**Files:**
- Create: `boot/ssbl/loader/bitstream_loader.c`（替换 Phase 6 stub）

**说明**：spec §7.5 关键约束："FSBL 初始化的 devcfg 寄存器状态 SSBL 可继承"。SSBL 直接复用 FSBL 留下的 PCAP 初始化，调 `XDcfg_Transfer`。

- [ ] **Step 1：把 FSBL 的 pcap.h 暴露给 SSBL include path**

```bash
cp boot/fsbl/pcap.h boot/ssbl/include/pcap.h
```

> **Note**：FSBL 跑在 OCM、SSBL 跑在 DDR，二者地址空间隔离，但 devcfg 寄存器（0xF8007000）全局可见。FSBL 调 `InitPcap()` 后，devcfg 处于可用状态，SSBL 调 `XDcfg_Transfer` 直接复用。

- [ ] **Step 2：替换 bitstream_loader.c stub 为完整实现**

```c
/* boot/ssbl/loader/bitstream_loader.c — 读 .bit + PCAP 下载到 PL
 * spec §4.2 + §6.2 test bitstream。
 *
 * 步骤：
 *   1. storage_file_open(path)
 *   2. 整文件读到 DDR 临时区（用 0x00800000 起，避开 app/ssbl/staging）
 *   3. 跳过 .bit 文件头（前 ~60 字节 Xilinx 字段头），取 bitstream payload
 *   4. XDcfg_Transfer 把 payload 推给 PCAP
 */

#include "storage.h"
#include "xil_printf.h"
#include "xil_cache.h"
#include "xparameters.h"
#include "pcap.h"
#include <string.h>

/* .bit 文件在 DDR 的临时存放区（避开 0x100000 ssbl / 0x1000000 app / 0x2000000 staging）*/
#define BIT_DDR_TEMP    0x00800000u
#define BIT_MAX_SIZE    (4u << 20)   /* 4MB，足够 Zynq-7010 PL（~1.6MB bit） */

/* FSBL 留下的 PCAP 实例（FSBL pcap.c 全局变量；SSBL extern）*/
extern XDcfg g_pcap_instance;

/* Xilinx .bit 头格式：13 字节固定前缀 + 多个 (字段类型, 长度, 值) 三元组。
 * 字段：a=设计名, b=工具版本, c=PL part, d=bitstream 日期, e=PL 时间, 后面是 payload。
 * 本函数找到 payload 起点（'e' 字段后），返回 payload 偏移。
 */
static uint32_t skip_bit_header(const uint8_t *buf, uint32_t total)
{
    uint32_t off = 0;
    /* 0x00 0x09 0x0F 0xF0 0x5A 0x00 ... 共 13 字节固定头 */
    if (total < 13) return 0;
    if (!(buf[0] == 0x00 && buf[1] == 0x09 && buf[2] == 0x0F &&
          buf[3] == 0xF0 && buf[4] == 0x5A)) {
        /* 不是标准 .bit 头；假定是 raw bin，直接用 */
        return 0;
    }
    off = 13;
    /* 跳过字段 a/b/c/d/e（每个：1 字节 type + 2 字节 length + length 字节 data）*/
    while (off + 3 <= total) {
        uint8_t type = buf[off];
        if (type < 'a' || type > 'e') break;
        uint16_t L = ((uint16_t)buf[off + 1] << 8) | buf[off + 2];
        off += 3 + L;
    }
    return off;
}

int bit_loader_download(const char *path)
{
    /* 1. 读整文件到 DDR 临时区 */
    int rc = storage_file_open(path, STORAGE_OPEN_READ);
    if (rc != STORAGE_OK) {
        xil_printf("[bit] open %s failed (%d)\r\n", path, rc);
        return rc;
    }
    uint8_t *dst = (uint8_t *)BIT_DDR_TEMP;
    uint32_t total = 0;
    while (total < BIT_MAX_SIZE) {
        int n = storage_file_read(dst + total, BIT_MAX_SIZE - total);
        if (n <= 0) break;
        total += n;
        if (n < (int)(BIT_MAX_SIZE - total < 4096 ? BIT_MAX_SIZE - total : 4096)) {
            break;   /* 短读视为 EOF */
        }
    }
    storage_file_close();
    xil_printf("[bit] %s loaded: %u bytes\r\n", path, total);

    /* 2. 跳 Xilinx .bit 头 */
    uint32_t payload_off = skip_bit_header(dst, total);
    uint32_t payload_size = total - payload_off;
    if (payload_size == 0) {
        xil_printf("[bit] empty payload after header\r\n");
        return -1;
    }
    xil_printf("[bit] payload @0x%08X size=%u\r\n",
               BIT_DDR_TEMP + payload_off, payload_size);

    /* 3. clean D-cache 确保 DDR 与 PL DMA 一致 */
    Xil_DCacheFlush();

    /* 4. PCAP transfer */
    u32 Status = XDcfg_Transfer(&g_pcap_instance,
                                (u8 *)(BIT_DDR_TEMP + payload_off), payload_size,
                                NULL, 0, XDCFG_NON_SECURE_PCAP_WRITE);
    if (Status != XST_SUCCESS) {
        xil_printf("[bit] PCAP transfer failed: %lu\r\n", Status);
        return -2;
    }
    xil_printf("[bit] PL configured OK\r\n");
    return 0;
}
```

> **Note**：`XDcfg_Transfer` 的 API 签名依 Xilinx 版本而异（5 参数 vs 6 参数）。实现期查 FSBL `pcap.h` 实际声明。`g_pcap_instance` 名字依 FSBL 实际全局变量名（vendor 可能叫 `PCAP` 或 `s_Pcap`）。

- [ ] **Step 3：把 FSBL 的 pcap.c 加入 SSBL Vitis 工程**

```bash
cp boot/fsbl/pcap.c boot/ssbl/loader/pcap.c   # 或直接 Vitis Import
```

SSBL 工程 include path 加 `boot/ssbl/include`（已有 pcap.h）。

- [ ] **Step 4：Build + 验证 `test bitstream`**

CLI 进：
```
ssbl> test bitstream pl_a.bit
[bit] pl_a.bit loaded: 1623456 bytes
[bit] payload @0x00800xxx size=...
[bit] PL configured OK
test bitstream pl_a.bit: OK
```

期望：板子上对应 PL 引脚的功能变化（如 PL 上的 LED 反转，或 spec §12.2 #6 描述的 "PL 可能用旧 bit" 场景被新 bit 替换）。

- [ ] **Step 5：验证完整流程：cfg 配 bit + auto boot**

cfg：
```
app = app_current.bin
bit = pl_a.bit
```

期望：
```
[cfg] loaded: app=app_current.bin bit=pl_a.bit delay=3s auto=1
[boot] downloading bitstream pl_a.bit
[bit] ...
[bit] PL configured OK
[loader] app_current.bin OK: ...
[boot] handoff to app @0x01000000
```

- [ ] **Step 6：提交**

```bash
git add boot/ssbl/loader/bitstream_loader.c boot/ssbl/include/pcap.h \
        boot/ssbl/loader/pcap.c
git commit -m "Phase 9.1: dynamic bitstream load via FSBL-reused PCAP (spec §4.2 + §6.2)"
```

---
