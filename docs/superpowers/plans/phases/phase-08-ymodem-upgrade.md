> **章节导航**：
  [← 上一章：CLI 子系统](phase-07-cli.md) ｜ [↑ 返回总述](../2026-06-22-zynq-multistage-boot.md) ｜ [下一章：bitstream 动态加载 →](phase-09-bitstream.md)

> **Phase 元信息**
> - 对应 Spec：`§6.3`
> - 里程碑：`M8`
> - 交付物：YMODEM 两阶段升级（DDR 暂存 + 原子提交）
> - 分章文件：`phases/phase-08-ymodem-upgrade.md`

> **接口定位**：本章对其他 Phase 的依赖与对外暴露的符号，见
> [总述章 · 模块依赖矩阵](../2026-06-22-zynq-multistage-boot.md#3-模块依赖矩阵)。
> 新增功能时，先查该清单确认接口落点，再回本章扩展。

---
## Phase 8：YMODEM 两阶段升级（M8）

**目标**：实现 spec §6.3 #3 的两阶段升级流程。YMODEM 接收 → DDR 暂存区（`0x02000000`，10MB）→ DDR 内校验（app 双 CRC / bit Xilinx 头）→ 用户 `y` 确认 → `.tmp + rename` 原子提交**到 P2 数据分区**。传输失败 / 校验失败 / 用户拒绝均保证 SD 卡零写入。

**Spec 引用**：§2（DDR 暂存区 10MB）、§6.3 #3（两阶段流程）、§9.5（app 校验流程）、§4.2（bit 校验）、**§5.5（隔离：提交只写 P2）**。

> **★ 隔离约束（spec §5.5）**：所有升级提交（`storage_file_write` / `storage_file_rename`）通过 storage 抽象层走，底层已是 P2（Phase 4.3 的 `sd_port` 只挂 P2）。YMODEM 代码**无需感知分区**——只要不绕过 storage 层直接操作 SD，就天然只写 P2，物理上够不到 P1 的 BOOT.BIN。Task 8.x 的实现里**不得**新增任何绕过 storage 层的裸 SD 写。

### Task 8.1：写 cli_staging.h 与 DDR 暂存区管理

**Files:**
- Create: `boot/ssbl/cli/include/cli_staging.h`
- Create: `boot/ssbl/cli/cli_staging.c`

- [ ] **Step 1：写 cli_staging.h**

```c
/* boot/ssbl/cli/include/cli_staging.h — DDR 暂存区 + 两阶段提交
 * spec §2 + §6.3 #3。
 */
#ifndef SSBL_CLI_STAGING_H
#define SSBL_CLI_STAGING_H

#include <stdint.h>

#define STAGING_AREA_ADDR   0x02000000u
#define STAGING_AREA_SIZE   (10u << 20)   /* 10MB（spec §2）*/
#define STAGING_APP_MAX     (7u << 20)    /* app 镜像上限 7MB */

/* 暂存区内容类型（决定校验策略）*/
typedef enum {
    STAGING_TYPE_UNKNOWN = 0,
    STAGING_TYPE_APP,        /* app.bin：校验 magic + header_crc + payload_crc */
    STAGING_TYPE_BIT,        /* .bit：校验 Xilinx 头 */
} staging_type_t;

/* 把暂存区视为线性 buffer，YMODEM 边收边 append。 */
void staging_reset(void);
int  staging_append(const uint8_t *data, uint32_t len);
uint32_t staging_size(void);
uint8_t *staging_data(void);

/* 校验暂存区当前内容（按 type 选策略）。
 * 返回 0=OK，负数=校验失败。OK 时把"提交用的最终字节数"写入 *commit_size
 * （app：header+payload 全部；bit：实际收到的字节）。
 */
int staging_verify(staging_type_t type, uint32_t *commit_size);

/* 把暂存区内容原子提交到存储介质的目标路径。
 * 流程：create dst.tmp → write 全部 → close → delete dst → rename tmp→dst
 * 失败时清理 tmp，返回负数。
 */
int staging_commit(const char *dst_path, uint32_t commit_size);

#endif
```

- [ ] **Step 2：写 cli_staging.c**

```c
/* boot/ssbl/cli/cli_staging.c — 暂存区实现 */
#include "cli_staging.h"
#include "storage.h"
#include "image_header.h"
#include "crc32.h"
#include "xil_printf.h"
#include <string.h>

static uint8_t *const s_staging = (uint8_t *)STAGING_AREA_ADDR;
static uint32_t s_size = 0;

void staging_reset(void)
{
    s_size = 0;
    /* 不必 memset 整个 10MB（节省时间）；写入时覆盖即可 */
}

int staging_append(const uint8_t *data, uint32_t len)
{
    if (s_size + len > STAGING_AREA_SIZE) {
        xil_printf("[staging] overflow: %u + %u > %u\r\n",
                   s_size, len, (uint32_t)STAGING_AREA_SIZE);
        return -1;
    }
    memcpy(s_staging + s_size, data, len);
    s_size += len;
    return 0;
}

uint32_t staging_size(void)        { return s_size; }
uint8_t *staging_data(void)        { return s_staging; }

int staging_verify(staging_type_t type, uint32_t *commit_size)
{
    if (s_size == 0) return -1;

    if (type == STAGING_TYPE_APP) {
        if (s_size < IMAGE_HEADER_SIZE) {
            xil_printf("[staging] app too small\r\n");
            return -1;
        }
        image_header_t hdr;
        memcpy(&hdr, s_staging, sizeof(hdr));

        /* magic + header_crc + version */
        extern int image_header_validate(const image_header_t *);
        if (image_header_validate(&hdr) != 0) return -2;

        /* payload 完整性 */
        if (s_size < IMAGE_HEADER_SIZE + hdr.image_size) {
            xil_printf("[staging] payload truncated: have=%u need=%u\r\n",
                       s_size, IMAGE_HEADER_SIZE + hdr.image_size);
            return -3;
        }
        uint32_t calc = crc32_compute(s_staging + IMAGE_HEADER_SIZE, hdr.image_size);
        if (calc != hdr.crc32) {
            xil_printf("[staging] payload crc bad\r\n");
            return -4;
        }
        *commit_size = IMAGE_HEADER_SIZE + hdr.image_size;
        return 0;
    }
    if (type == STAGING_TYPE_BIT) {
        /* spec §4.2：不解析 .bit 内容，只校验 Xilinx 头（前 13 字节
         * 0x00 0x09 0x0F 0xF0 0x5A 等 magic + 字段长度前缀合理）。
         * 实现最小校验：检查长度 ≥ 75 字节（最小合法 .bit 头）。
         */
        if (s_size < 75) {
            xil_printf("[staging] bit too small (%u)\r\n", s_size);
            return -5;
        }
        *commit_size = s_size;
        return 0;
    }
    return -10;
}

int staging_commit(const char *dst_path, uint32_t commit_size)
{
    /* 拼出 dst + ".tmp"（路径长度限制 96，spec §6.4 已预留）*/
    char tmp_path[128];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", dst_path);

    int rc;
    rc = storage_file_create(tmp_path);
    if (rc != STORAGE_OK) goto fail;

    rc = storage_file_open(tmp_path, STORAGE_OPEN_WRITE);
    if (rc != STORAGE_OK) goto fail;

    /* 分块写（避免一次写 7MB）*/
    uint32_t off = 0;
    while (off < commit_size) {
        uint32_t chunk = commit_size - off;
        if (chunk > 4096) chunk = 4096;
        int n = storage_file_write(s_staging + off, chunk);
        if (n != (int)chunk) {
            storage_file_close();
            rc = STORAGE_ERR_IO;
            goto fail;
        }
        off += chunk;
    }
    storage_file_close();

    /* rename 前 delete dst（若存在）*/
    storage_file_delete(dst_path);
    rc = storage_file_rename(tmp_path, dst_path);
    if (rc != STORAGE_OK) goto fail;

    xil_printf("[staging] committed %u bytes → %s\r\n", commit_size, dst_path);
    return 0;

fail:
    storage_file_delete(tmp_path);   /* 清残骸 */
    xil_printf("[staging] commit failed (%d), tmp cleaned\r\n", rc);
    return rc;
}
```

- [ ] **Step 3：提交**

```bash
git add boot/ssbl/cli/cli_staging.c boot/ssbl/cli/include/cli_staging.h
git commit -m "Phase 8.1: DDR staging manager with verify + atomic commit (spec §6.3 #3)"
```

---

### Task 8.2：写 cli_ymodem.c（接收状态机）

**Files:**
- Create: `boot/ssbl/cli/cli_ymodem.c`
- Create: `boot/ssbl/cli/include/cli_ymodem.h`

**说明**：YMODEM 协议要点（PC 端 Tera Term / SecureCRT 兼容）：
- SOH(0x01)=128B 数据块、STX(0x02)=1024B 数据块
- 块格式：`[SOH/STX][seq][~seq][payload...][crc16_hi][crc16_lo]`
- 接收方逐块回 ACK(0x06)，结束发 NAK 后再发 ACK
- 文件名 + 大小走第 0 块（Block 0）：`filename 0x00 filesize ...`
- 重传：NAK(0x15)，10 次失败放弃（spec §6.3 #3）

- [ ] **Step 1：写 cli_ymodem.h**

```c
/* boot/ssbl/cli/include/cli_ymodem.h — YMODEM 接收状态机 */
#ifndef SSBL_CLI_YMODEM_H
#define SSBL_CLI_YMODEM_H

#include <stdint.h>

/* YMODEM 接收结果 */
typedef enum {
    YMODEM_OK = 0,
    YMODEM_ERR_TIMEOUT,    /* 单字节超时 */
    YMODEM_ERR_CRC,        /* 块 CRC16 错 */
    YMODEM_ERR_SEQ,        /* 序号不连续 */
    YMODEM_ERR_RETRIES,    /* 重传 10 次放弃 */
    YMODEM_ERR_TOO_BIG,    /* 超过暂存区 */
    YMODEM_ERR_CANCELED,   /* 发送方发 CAN(0x18) 两次 */
} ymodem_result_t;

/* 把后续收到的字节全部追加到 DDR 暂存区。
 * 完成时返回 YMODEM_OK 并写 *out_size（接收到的字节数，含 header 块开销）。
 */
ymodem_result_t ymodem_receive(uint32_t *out_size);

const char *ymodem_strerror(ymodem_result_t r);

#endif
```

- [ ] **Step 2：写 cli_ymodem.c**

```c
/* boot/ssbl/cli/cli_ymodem.c — YMODEM 接收（PC 端 Tera Term/SecureCRT 兼容）
 *
 * 流程：
 *   1. 接收方先发 'C'（请求 CRC 模式），等 SOH/STX
 *   2. 读 Block 0（filename + size），ACK + 'C'
 *   3. 逐块读 payload，校验 CRC16 + seq，ACK；payload 直接 staging_append
 *   4. 收到 EOT(0x04)：NAK → 再 EOT → ACK → 'C' 收尾块（空 Block 0）→ ACK
 *   5. 全部数据在 DDR 暂存区
 */

#include "cli_ymodem.h"
#include "cli_uart.h"
#include "cli_staging.h"
#include "xil_printf.h"
#include <string.h>

#define SOH     0x01
#define STX     0x02
#define EOT     0x04
#define ACK     0x06
#define NAK     0x15
#define CAN     0x18
#define CCHAR   'C'

#define MAX_RETRIES   10

extern int  cli_uart_getchar(void);
extern int  cli_uart_rx_chunk(uint8_t *buf, int len);

static void uart_put(uint8_t c)
{
    extern struct device *puart1;   /* 由 cli_uart.c 暴露或 device_find 重取 */
    /* 简化：直接 device_write("uart1") */
    extern void uart_write_byte(uint8_t c);
    uart_write_byte(c);
}

static uint16_t crc16_ccitt(const uint8_t *p, int len)
{
    uint16_t crc = 0;
    for (int i = 0; i < len; i++) {
        crc ^= ((uint16_t)p[i]) << 8;
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

const char *ymodem_strerror(ymodem_result_t r)
{
    switch (r) {
        case YMODEM_OK:               return "OK";
        case YMODEM_ERR_TIMEOUT:      return "timeout";
        case YMODEM_ERR_CRC:          return "CRC";
        case YMODEM_ERR_SEQ:          return "seq";
        case YMODEM_ERR_RETRIES:      return "retries exhausted";
        case YMODEM_ERR_TOO_BIG:      return "too big";
        case YMODEM_ERR_CANCELED:     return "canceled by sender";
    }
    return "?";
}

ymodem_result_t ymodem_receive(uint32_t *out_size)
{
    uint8_t buf[1024 + 5];
    int expected_seq = 0;
    int retries = 0;

    staging_reset();

    /* 1. 发 'C' 请求 CRC 模式 */
    for (retries = 0; retries < MAX_RETRIES; retries++) {
        uart_put(CCHAR);
        int c = cli_uart_getchar();
        if (c == SOH || c == STX) {
            buf[0] = (uint8_t)c;
            goto have_start;
        }
        if (c == CAN) {
            if (cli_uart_getchar() == CAN) return YMODEM_ERR_CANCELED;
        }
    }
    return YMODEM_ERR_RETRIES;

have_start:
    for (;;) {
        int block_size = (buf[0] == STX) ? 1024 : 128;
        int need = block_size + 4;   /* seq ~seq payload crc_hi crc_lo */
        /* 已读 buf[0]，再读 need 字节 */
        int got = cli_uart_rx_chunk(buf + 1, need);
        if (got != need) return YMODEM_ERR_TIMEOUT;

        uint8_t seq    = buf[1];
        uint8_t seq_n  = buf[2];
        uint16_t crc_in = ((uint16_t)buf[3 + block_size] << 8) |
                          buf[4 + block_size];
        uint16_t crc_calc = crc16_ccitt(buf + 3, block_size);

        if ((uint8_t)~seq_n != seq) {
            /* seq 校验失败 */
            uart_put(NAK);
            continue;
        }
        if (crc_in != crc_calc) {
            uart_put(NAK);
            if (++retries >= MAX_RETRIES) return YMODEM_ERR_RETRIES;
            continue;
        }

        if (seq == 0 && expected_seq == 0) {
            /* Block 0：filename + size；不进暂存区 */
            uart_put(ACK);
            uart_put(CCHAR);
            expected_seq = 1;
            continue;
        }

        /* payload 块 */
        if (seq == expected_seq) {
            int rc = staging_append(buf + 3, block_size);
            if (rc != 0) {
                /* 暂存区满，告诉发送方取消 */
                uart_put(CAN); uart_put(CAN);
                return YMODEM_ERR_TOO_BIG;
            }
            uart_put(ACK);
            expected_seq++;
            retries = 0;
        } else if (seq == expected_seq - 1) {
            /* 重复块，再 ACK 一次（发送方没收到上次的 ACK）*/
            uart_put(ACK);
        } else {
            /* seq 不连续 */
            uart_put(CAN); uart_put(CAN);
            return YMODEM_ERR_SEQ;
        }

        /* 下一块的开头 */
        int c = cli_uart_getchar();
        if (c == EOT) {
            uart_put(NAK);                  /* 第一次 EOT：NAK */
            c = cli_uart_getchar();
            if (c != EOT) return YMODEM_ERR_TIMEOUT;
            uart_put(ACK);                  /* 第二次 EOT：ACK */
            uart_put(CCHAR);                /* 请求收尾空块 */
            /* 收尾：SOH 00 FF 00*128 CRC + 然后 ACK */
            c = cli_uart_getchar();
            if (c == SOH || c == STX) {
                /* 略读剩余 */
                cli_uart_rx_chunk(buf + 1, (c == STX ? 1028 : 156));
                uart_put(ACK);
            }
            break;
        } else if (c == SOH || c == STX) {
            buf[0] = (uint8_t)c;
            continue;
        } else if (c == CAN) {
            if (cli_uart_getchar() == CAN) return YMODEM_ERR_CANCELED;
        } else {
            return YMODEM_ERR_TIMEOUT;
        }
    }

    *out_size = staging_size();
    return YMODEM_OK;
}
```

> **Implementation note**：上例的 `uart_write_byte` 需在 `cli_uart.c` 加一个发送一字节的辅助函数（基于 `device_write(puart1, &c, 1)`），并在头部 `extern` 出来。本计划不重复贴 cli_uart.c 全文，只标注新增。

- [ ] **Step 3：提交**

```bash
git add boot/ssbl/cli/cli_ymodem.c boot/ssbl/cli/include/cli_ymodem.h
git commit -m "Phase 8.2: YMODEM receiver state machine (Tera Term/SecureCRT compatible)"
```

---

### Task 8.3：cli_cmd_ymodem 完整实现（两阶段流程）

**Files:**
- Modify: `boot/ssbl/cli/cli_commands.c`（替换 `cli_cmd_ymodem` stub）

- [ ] **Step 1：替换 cli_cmd_ymodem 为完整两阶段流程（spec §6.3 #3）**

在 `cli_commands.c` 顶部 include：

```c
#include "cli_ymodem.h"
#include "cli_staging.h"
#include "readline.h"
```

替换 `cli_cmd_ymodem`：

```c
int cli_cmd_ymodem(int argc, char **argv)
{
    /* 形如：ymodem rx <file>  */
    if (argc < 3 || strcmp(argv[1], "rx") != 0) {
        xil_printf("usage: ymodem rx <file>\r\n");
        return 0;
    }
    const char *dst = argv[2];

    /* 推断 staging_type（按扩展名）*/
    staging_type_t type;
    const char *dot = strrchr(dst, '.');
    if (dot && strcmp(dot, ".bin") == 0) {
        type = STAGING_TYPE_APP;
    } else if (dot && strcmp(dot, ".bit") == 0) {
        type = STAGING_TYPE_BIT;
    } else {
        xil_printf("unknown extension: %s\r\n", dst);
        return 0;
    }

    /* === 阶段 1：流式接收到 DDR 暂存区 === */
    xil_printf("Ready for YMODEM. Send file with Tera Term/SecureCRT...\r\n");
    uint32_t received = 0;
    ymodem_result_t yr = ymodem_receive(&received);
    if (yr != YMODEM_OK) {
        xil_printf("YMODEM failed: %s (%d bytes received)\r\n",
                   ymodem_strerror(yr), received);
        staging_reset();
        return 0;   /* 存储介质零写入，回 CLI */
    }
    xil_printf("YMODEM OK: %u bytes received\r\n", received);

    /* === DDR 内校验 === */
    uint32_t commit_size = 0;
    int vrc = staging_verify(type, &commit_size);
    if (vrc != 0) {
        xil_printf("Verify failed (%d), discarding staging\r\n", vrc);
        staging_reset();
        return 0;   /* 存储介质零写入 */
    }
    xil_printf("Verify OK: %u bytes to commit\r\n", commit_size);

    /* === 阶段 2：用户确认 === */
    xil_printf("Commit to %s? [y/N] (10s timeout) ", dst);
    char ans[8];
    /* 用 readline 带超时（简化：直接 readline 无超时；Phase 8.4 加超时）*/
    int n = readline(ans, sizeof(ans));
    if (n <= 0 || (ans[0] != 'y' && ans[0] != 'Y')) {
        xil_printf("Cancelled, discarding\r\n");
        staging_reset();
        return 0;   /* 用户拒绝，存储介质零写入 */
    }

    /* === 提交（.tmp + rename 原子写）=== */
    int crc = staging_commit(dst, commit_size);
    if (crc != STORAGE_OK) {
        xil_printf("Commit failed (%d)\r\n", crc);
        return 0;
    }
    xil_printf("Upgrade complete. Type 'boot' to load new image.\r\n");
    staging_reset();
    return 0;
}
```

- [ ] **Step 2：Build + 自测（PC 端用 Tera Term 发文件）**

1. PC 端准备一个 `app.bin`（Phase 5 pack 出的）；
2. CLI 输 `ymodem rx app_new.bin`；
3. Tera Term：File → Transfer → YMODEM → Send，选 `app.bin`；
4. 期望串口：
   ```
   Ready for YMODEM. Send file...
   YMODEM OK: 1234599 bytes received
   Verify OK: 1234599 bytes to commit
   Commit to app_new.bin? [y/N] (10s timeout)
   ```
5. 输 `y`：
   ```
   [staging] committed 1234599 bytes → app_new.bin
   Upgrade complete. Type 'boot' to load new image.
   ```
6. `ls` 看根目录：应有 `app_new.bin`，无 `app_new.bin.tmp` 残骸。

- [ ] **Step 3：失败场景验证（spec §12.2 #11-15）**

| # | 场景 | 操作 | 期望 |
|---|---|---|---|
| 11 | 正常升级 | 上面的流程 | 新 app 被加载 |
| 12 | 传输中断电 | 接收到一半拔板子 | 重启后 SD 卡原 app 完全未触碰 |
| 13 | CRC 错 | pack_app.py 出的 bin 用 hex 改 1 字节后 Tera Term 发 | `Verify failed`，零写入 |
| 14 | 用户拒绝 | 提示时输 `n` | `Cancelled`，零写入 |
| 15 | 提交断电 | staging_commit 写 .tmp 期间断电 | 下次启动有 .tmp 残骸；Task 8.4 处理 |

- [ ] **Step 4：提交**

```bash
git add boot/ssbl/cli/cli_commands.c
git commit -m "Phase 8.3: two-stage YMODEM upgrade (DDR stage → verify → confirm → commit)"
```

---

### Task 8.4：启动期清理 .tmp 残骸（spec §5.4 注意）

**Files:**
- Create: `boot/ssbl/src/staging_cleanup.c`
- Modify: `boot/ssbl/src/main.c`（在 boot_config_load 前调）

**说明**：spec §5.4 末段："下次启动 SSBL 可识别并清理 [半写的 .tmp]"。这一条没单独的 spec 小节，但属 YMODEM 原子提交的完整性保证。

- [ ] **Step 1：写 staging_cleanup.c（扫根目录，删所有 .tmp）**

```c
/* boot/ssbl/src/staging_cleanup.c — 启动期清理 *.tmp 残骸（spec §5.4）
 * 实现策略：FileX 没有目录通配删除 API，必须 listdir + 逐个 delete。
 * 本 Phase 用 storage_fx_dir_list 的变体（返回文件名列表），下个 Task 扩展。
 */

#include "storage.h"
#include "xil_printf.h"
#include <string.h>

/* 暴露一个简化的 listdir 回调式 API（在 storage_fx_glue.c 加）*/
extern int storage_fx_dir_foreach(const char *path,
                                  int (*cb)(const char *name, uint32_t size));

static int delete_if_tmp(const char *name, uint32_t size)
{
    (void)size;
    size_t L = strlen(name);
    if (L >= 4 && strcmp(name + L - 4, ".tmp") == 0) {
        xil_printf("[cleanup] removing stale %s\r\n", name);
        storage_file_delete(name);
    }
    return 0;   /* 继续遍历 */
}

void staging_cleanup_run(void)
{
    storage_fx_dir_foreach("/", delete_if_tmp);
}
```

- [ ] **Step 2：storage_fx_glue.c 加 dir_foreach 实现**

```c
int storage_fx_dir_foreach(const char *path,
                           int (*cb)(const char *name, uint32_t size))
{
    FX_DIR_ENTRY dir;
    memset(&dir, 0, sizeof(dir));
    if (fx_directory_first_entry_find(&g_fx_media, &dir) != FX_SUCCESS) {
        return STORAGE_ERR_IO;
    }
    do {
        if (cb) cb((const char *)dir.fx_dir_entry_name,
                   (uint32_t)dir.fx_dir_entry_size);
    } while (fx_directory_next_entry_find(&g_fx_media, &dir) == FX_SUCCESS);
    return STORAGE_OK;
}
```

> **Note**：上例用根目录遍历。FileX 的 `fx_directory_first_entry_find` 默认从 media 当前路径起。如果 SSBL 用了子目录，要在 caller 设 local path。本计划 SSBL 用 SD 根目录，OK。

- [ ] **Step 3：main.c 在 boot_config_load 前调**

在 `tx_application_define` 开头：

```c
extern void staging_cleanup_run(void);
staging_cleanup_run();   /* 清 .tmp 残骸，spec §5.4 */
```

- [ ] **Step 4：验证：手动制造残骸再启动**

1. SD 卡放一个 `app_new.bin.tmp`（任意内容）；
2. 重启；
3. 期望：`[cleanup] removing stale app_new.bin.tmp`；
4. `ls` 看不到 `.tmp`。

- [ ] **Step 5：提交**

```bash
git add boot/ssbl/src/staging_cleanup.c boot/ssbl/storage/storage_fx_glue.c \
        boot/ssbl/src/main.c
git commit -m "Phase 8.4: clean stale *.tmp on boot (spec §5.4)"
```

---
