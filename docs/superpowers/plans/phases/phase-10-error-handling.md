> **章节导航**：
  [← 上一章：bitstream 动态加载](phase-09-bitstream.md) ｜ [↑ 返回总述](../2026-06-22-zynq-multistage-boot.md) ｜ [下一章：QSPI 迁移 →](phase-11-qspi.md)

> **Phase 元信息**
> - 对应 Spec：`§10`
> - 里程碑：`M10`
> - 交付物：错误处理 + LED 错误码 + boot.log/OCM 标记
> - 分章文件：`phases/phase-10-error-handling.md`

> **接口定位**：本章对其他 Phase 的依赖与对外暴露的符号，见
> [总述章 · 模块依赖矩阵](../2026-06-22-zynq-multistage-boot.md#3-模块依赖矩阵)。
> 新增功能时，先查该清单确认接口落点，再回本章扩展。

---
## Phase 10：错误处理 + LED 错误码 + OCM 标记（M10）

**目标**：实现 spec §10 全章节——错误分级、LED 错误码（无串口诊断）、OCM 双状态机（BOOT_ATTEMPT_MAGIC / BOOT_OK_MAGIC）。

> **★ boot.log 当前阶段排除（用户决策，spec §10.4）**：本 Phase **不实现** boot.log 的 SD 持久化部分（每次启动的 FAT 写是共享 FAT 表变砖风险的最大来源，spec §5.5.1）。仅实现：
> - §10.1 错误分级、§10.2 日志宏、§10.3 LED 错误码 —— **本次实现**
> - §10.4 OCM 双状态机（写 magic + 下次启动读回） —— **本次实现，但回填结果只走串口打印，不落 SD**
> - §10.4 boot.log 文件持久化 —— **推迟**，待后期明确隔离需求后 reintroduce（届时必须落 P2/数据段，见 spec §10.4 末尾约束）

**Spec 引用**：§10.1 错误分级、§10.2 日志、§10.3 LED 错误码、§10.4 boot.log（OCM 双状态机，**仅 OCM 部分；FAT 持久化推迟**）。

### Task 10.1：写 ssbl_error.h 与 ssbl_config.h

**Files:**
- Create: `boot/ssbl/include/ssbl_error.h`
- Create: `boot/ssbl/include/ssbl_config.h`
- Create: `boot/ssbl/include/ssbl_log.h`

- [ ] **Step 1：写 ssbl_error.h（spec §10.1 错误分级 + §10.3 LED 码）**

```c
/* boot/ssbl/include/ssbl_error.h */
#ifndef SSBL_ERROR_H
#define SSBL_ERROR_H

/* 错误等级（spec §10.1）*/
typedef enum {
    SSBL_ERR_FATAL = 0,         /* 死循环 + LED 闪 1/2/3 次 + 串口 */
    SSBL_ERR_BOOT_REFUSED,      /* 进 CLI */
    SSBL_ERR_RECOVERABLE,       /* 告警继续 */
    SSBL_ERR_INFO,
} ssbl_err_level_t;

/* LED 闪烁码（spec §10.3）*/
#define LED_CODE_BOARD_INIT     1
#define LED_CODE_SD_MOUNT       2
#define LED_CODE_DDR_FAIL       3
#define LED_CODE_PANIC          0xFF   /* 持续快闪 */

void ssbl_fatal(uint32_t led_code, const char *fmt, ...);  /* noreturn */

#endif
```

- [ ] **Step 2：写 ssbl_config.h（编译期常量集中地）**

```c
/* boot/ssbl/include/ssbl_config.h */
#ifndef SSBL_CONFIG_H
#define SSBL_CONFIG_H

/* 日志等级（编译期过滤，spec §10.2）*/
#define SSBL_LOG_LEVEL_INFO     3   /* 默认 INFO；DEBUG=4 */

/* OCM 标记地址（spec §10.4：0xFFFF0000 起保留区）*/
#define BOOT_OCM_MARKER_ADDR    0xFFFF0000u
#define BOOT_MAGIC_ATTEMPT      0x4154544Du   /* "ATTM" */
#define BOOT_MAGIC_OK           0x4F4B4F4Bu   /* "OKOK" */

/* boot.log 配置 */
#define BOOT_LOG_PATH           "boot.log"
#define BOOT_LOG_MAX_ENTRIES    8

/* Tick（来自 ThreadX port，§8.2 = 10ms）*/
#define SSBL_TICK_HZ            100

#endif
```

- [ ] **Step 3：写 ssbl_log.h（spec §10.2，含 tx_time_get cast）**

```c
/* boot/ssbl/include/ssbl_log.h */
#ifndef SSBL_LOG_H
#define SSBL_LOG_H

#include "ssbl_config.h"
#include "tx_api.h"
#include "xil_printf.h"

typedef enum {
    SSBL_LOG_FATAL = 0,
    SSBL_LOG_ERROR,
    SSBL_LOG_WARN,
    SSBL_LOG_INFO,
    SSBL_LOG_DEBUG
} ssbl_log_level_t;

#define SSBL_LOG(level, fmt, ...) \
    do { \
        if ((level) <= SSBL_LOG_LEVEL) { \
            xil_printf("[%5lu] [" #level "] " fmt "\r\n", \
                       (unsigned long)tx_time_get(), ##__VA_ARGS__); \
        } \
    } while (0)

#define LOG_FATAL(fmt, ...)  SSBL_LOG(SSBL_LOG_FATAL, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)  SSBL_LOG(SSBL_LOG_ERROR, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)   SSBL_LOG(SSBL_LOG_WARN,  fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)   SSBL_LOG(SSBL_LOG_INFO,  fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...)  SSBL_LOG(SSBL_LOG_DEBUG, fmt, ##__VA_ARGS__)

#endif
```

- [ ] **Step 4：提交**

```bash
git add boot/ssbl/include/ssbl_error.h boot/ssbl/include/ssbl_config.h \
        boot/ssbl/include/ssbl_log.h
git commit -m "Phase 10.1: error levels, config constants, log macros (spec §10.1/§10.2)"
```

---

### Task 10.2：写 ssbl_fatal 与 LED 闪烁

**Files:**
- Create: `boot/ssbl/src/ssbl_error.c`

- [ ] **Step 1：写 ssbl_error.c**

```c
/* boot/ssbl/src/ssbl_error.c — FATAL 处理：LED 闪码 + 串口 + 死循环
 * spec §10.1 + §10.3。
 */

#include "ssbl_error.h"
#include "ssbl_log.h"
#include "includes.h"
#include <stdarg.h>
#include <stdio.h>

static struct device *s_led0 = NULL;

static void led_init_once(void)
{
    if (!s_led0) s_led0 = device_find("led0");
}

static void led_set(uint8_t v)
{
    led_init_once();
    if (s_led0) device_write(s_led0, &v, 1);
}

static void blink_code(uint32_t code)
/* code=0xFF 持续快闪；否则闪 code 次后长暗 */
{
    if (code == LED_CODE_PANIC) {
        while (1) {
            led_set(1); tx_thread_sleep(10);
            led_set(0); tx_thread_sleep(10);
        }
    }
    while (1) {
        for (uint32_t i = 0; i < code; i++) {
            led_set(1); tx_thread_sleep(15);   /* 150ms on */
            led_set(0); tx_thread_sleep(15);   /* 150ms off */
        }
        led_set(0); tx_thread_sleep(100);      /* 1s gap */
    }
}

void ssbl_fatal(uint32_t led_code, const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    xil_printf("\r\n*** FATAL: %s (LED code=%u) ***\r\n", buf, led_code);
    blink_code(led_code);   /* noreturn */
}
```

- [ ] **Step 2：在 board_init / storage 失败处用 ssbl_fatal 替代 idle**

修改 `main.c`：

```c
#include "ssbl_error.h"

int main(void)
{
    xil_printf("\r\n[SSBL] Hello from SSBL @0x100000\r\n");

    if (bsp_init() != 0) {
        ssbl_fatal(LED_CODE_BOARD_INIT, "bsp_init failed");
    }
    if (board_init() != 0) {
        ssbl_fatal(LED_CODE_BOARD_INIT, "board_init failed");
    }

    fx_system_initialize();
    sd_port_init();
    if (storage_media_open() != STORAGE_OK) {
        ssbl_fatal(LED_CODE_SD_MOUNT, "SD mount failed");
    }

    tx_kernel_enter();
    while (1);
}
```

- [ ] **Step 3：Build + 验证（人为触发 FATAL）**

拔 SD 卡上电，期望：
```
*** FATAL: SD mount failed (LED code=2) ***
```
LED0 闪 2 次 → 暗 1s → 循环。

- [ ] **Step 4：提交**

```bash
git add boot/ssbl/src/ssbl_error.c boot/ssbl/src/main.c
git commit -m "Phase 10.2: ssbl_fatal with LED blink code (spec §10.1 + §10.3)"
```

---

### Task 10.3：写 boot_log + OCM magic 状态机（spec §10.4 修订版）

> **★ 当前阶段裁剪（spec §10.4 / §5.5）**：本 Task 实现 **OCM magic 状态机部分**（写 ATTEMPT/OK magic、下次启动读回），但 **boot.log 的 FAT 文件持久化部分推迟**。具体：
> - **本次实现**：OCM 双状态机（`boot_log_mark_attempt` / `boot_log_check_previous`）+ 串口打印上次状态
> - **本次跳过**：下方 Step 中所有"写 boot.log 文件 / 环形覆盖 8 条"的 FAT 写逻辑（标注为 `/* 推迟 */` 或 `#if 0` 包起来）——这些是每次启动的 FAT 写，是 §5.5.1 变砖风险最大来源，在隔离充分验证前不启用
> - **后期 reintroduce**：恢复 FAT 写时必须确认写入落 P2（spec §10.4 末尾约束）

**Files:**
- Create: `boot/ssbl/src/boot_log.c`
- Create: `boot/ssbl/src/boot_log.h`

- [ ] **Step 1：写 boot_log.h**

```c
/* boot/ssbl/src/boot_log.h — spec §10.4 修订版（OCM magic，无硬件看门狗）*/
#ifndef SSBL_BOOT_LOG_H
#define SSBL_BOOT_LOG_H

#include <stdint.h>
#include "boot_config.h"

/* 启动极早期调（main 开头）：读 OCM 残留标记，回填上次 boot.log。
 * 必须在 tx_kernel_enter / 跳转前调，否则 OCM 在 jump 后被 app 重写。
 */
void boot_log_check_previous(void);

/* 跳转 app 前调：写 OCM = BOOT_MAGIC_ATTEMPT + 本次 app 名。 */
void boot_log_mark_attempt(const boot_cfg_t *cfg);

/* 跳转 app 后由 app 喂狗（写 OCM = BOOT_MAGIC_OK）。SSBL 提供 API 给 app 工程。*/
void boot_log_app_mark_ok(void);

/* 在 CLI 的 status 命令里调用：返回上次 boot.log 最后一条文本（便于打印）*/
int boot_log_last_entry(char *buf, int maxlen);

#endif
```

- [ ] **Step 2：写 boot_log.c（spec §10.4 修订版逻辑）**

```c
/* boot/ssbl/src/boot_log.c
 *
 * 状态机（修订后，spec §10.4）：
 *   - SSBL 跳 app 前：写 OCM = ATTEMPT + app 名；不写 boot.log（"本次写时
 *     还不知道 app 会不会活"的悖论）。
 *   - app 启动期：单寄存器写 OCM = OK。
 *   - SSBL 下次启动早期：读 OCM：
 *       ATTEMPT  → 上次 app 没喂狗 → 写 boot.log 一条 FAIL
 *       OK       → 写 boot.log 一条 OK
 *       其它     → 首启/POR，不写
 *
 * OCM 在软复位/POR 后内容丢失——可接受：硬复位无法追溯，等同首启。
 */

#include "boot_log.h"
#include "ssbl_config.h"
#include "ssbl_log.h"
#include "storage.h"
#include "xil_printf.h"
#include <string.h>
#include <stdio.h>

/* OCM marker 布局（4 字节 magic + 60 字节 app 名）*/
#define APP_NAME_LEN 60
typedef struct {
    uint32_t magic;
    char     app_name[APP_NAME_LEN];
} ocm_marker_t;

static volatile ocm_marker_t *const s_marker =
    (volatile ocm_marker_t *)BOOT_OCM_MARKER_ADDR;

/* === 启动早期：回填上次状态 === */
void boot_log_check_previous(void)
{
    uint32_t magic = s_marker->magic;
    char app[APP_NAME_LEN + 1];

    if (magic == BOOT_MAGIC_ATTEMPT || magic == BOOT_MAGIC_OK) {
        strncpy(app, (const char *)s_marker->app_name, APP_NAME_LEN);
        app[APP_NAME_LEN] = '\0';

        const char *result = (magic == BOOT_MAGIC_OK) ? "OK" : "FAIL:app_not_started";

        /* 追加一条到 boot.log（环形覆盖 8 条，spec §10.4）*/
        char line[128];
        ULONG now = 0;   /* 早期还没 tx_time_get，用 0；正式写时换成 RTC 或 uptime */
        snprintf(line, sizeof(line),
                 "[%lu] boot %s: app=%s\r\n",
                 (unsigned long)now, result, app);

        /* 追加（FileX：file_open_for_write + seek end + write）*/
        /* 简化：覆盖写最后 8 条 = boot.log 固定 8 行 × 64 字节，环形。
         * 完整实现见 Task 10.3 Step 3 的 ring buffer 优化。 */
        int rc = storage_file_open(BOOT_LOG_PATH, STORAGE_OPEN_WRITE);
        if (rc == STORAGE_OK) {
            storage_file_write(line, (uint32_t)strlen(line));
            storage_file_close();
        } else {
            xil_printf("[blog] open %s failed (%d)\r\n", BOOT_LOG_PATH, rc);
        }
    } else {
        xil_printf("[blog] first boot or POR (magic=0x%08X)\r\n", magic);
    }

    /* 清 OCM 准备本次尝试 */
    s_marker->magic = 0;
    memset((void *)s_marker->app_name, 0, APP_NAME_LEN);
}

/* === 跳 app 前：写 ATTEMPT === */
void boot_log_mark_attempt(const boot_cfg_t *cfg)
{
    s_marker->magic = BOOT_MAGIC_ATTEMPT;
    strncpy((char *)s_marker->app_name, cfg->app_name, APP_NAME_LEN - 1);
    s_marker->app_name[APP_NAME_LEN - 1] = '\0';
}

/* === app 喂狗：写 OK（app 工程独立编译进自己的初始化）=== */
void boot_log_app_mark_ok(void)
{
    /* 直接写 OCM：app 不依赖 SSBL 的任何符号 */
    volatile uint32_t *p = (volatile uint32_t *)BOOT_OCM_MARKER_ADDR;
    *p = BOOT_MAGIC_OK;
}

/* === status 命令读最后一条 === */
int boot_log_last_entry(char *buf, int maxlen)
{
    int rc = storage_file_open(BOOT_LOG_PATH, STORAGE_OPEN_READ);
    if (rc != STORAGE_OK) return -1;
    /* 简化：读最后 128 字节 */
    char tmp[1024];
    int n = storage_file_read(tmp, sizeof(tmp) - 1);
    storage_file_close();
    if (n <= 0) return -1;
    tmp[n] = '\0';
    /* 找最后一行 */
    char *p = strrchr(tmp, '\n');
    if (!p) { strncpy(buf, tmp, maxlen); return 0; }
    while (*p == '\n' || *p == '\r') p--;
    /* 找倒数第二行结尾 */
    while (p >= tmp && *p != '\n') p--;
    strncpy(buf, p + 1, maxlen);
    buf[maxlen - 1] = '\0';
    return 0;
}
```

- [ ] **Step 3：环形 buffer 优化（spec §10.4 "最多保留最近 8 次"）**

上 Step 2 是简化版（append-only，不截断）。完整的环形版需在 boot.log 开头维护一个"指针字段"。简化版满足"能看到上次状态"的核心需求；完整环形版作为 Phase 10 末尾的"打磨任务"，本计划只描述方向：

```c
/* boot.log 格式（8 × 64B 固定槽 + 8B header）：
 *   offset 0:    uint32_t magic = 0x4C4F4748 ("LOGH")
 *   offset 4:    uint32_t next_slot (0..7)
 *   offset 8..8+8*64: 8 个固定 64B 槽
 *
 * 写入：write to slot[next_slot], next_slot = (next_slot + 1) % 8
 * 读取：从 next_slot 起逆序遍历
 *
 * 实现略（标准环形数组）；本 Phase 末打磨。
 */
```

- [ ] **Step 4：把 boot_log 接入主流程**

修改 `main.c`：

```c
extern void boot_log_check_previous(void);

int main(void)
{
    /* … bsp_init/board_init/storage_media_open 同 Phase 10.2 … */

    boot_log_check_previous();   /* spec §10.4：极早调，趁 OCM 还在 */

    tx_kernel_enter();
}
```

修改 `boot_selector.c`，在 `jump_to_app` 前调 mark_attempt：

```c
extern void boot_log_mark_attempt(const boot_cfg_t *);

int boot_selector_run(void)
{
    boot_cfg_t cfg;
    int rc = boot_config_load(&cfg);
    if (rc != 0) return rc;

    uint32_t load_addr = 0;
    rc = boot_selector_load_only(cfg.app_name, cfg.bit_path, &load_addr);
    if (rc != STORAGE_OK) return rc;

    boot_log_mark_attempt(&cfg);    /* ★ 跳转前写 ATTEMPT */

    xil_printf("[boot] handoff to app @0x%08X\r\n", load_addr);
    jump_to_app(load_addr);
}
```

- [ ] **Step 5：在 app_template 暴露喂狗 API（spec §10.4 "app 唯一配合"）**

修改 `boot/app_template/src/main.c`，在 `bsp_init()` 之后立即：

```c
extern void boot_log_app_mark_ok(void);   /* 来自 SSBL 的 boot_log.c？
                                           * 不！app 独立，不能 link SSBL。
                                           * → app 模板内自带一份 1 行函数 */

void app_boot_log_mark_ok(void)   /* app 自己定义，不依赖 SSBL 符号 */
{
    volatile uint32_t *p = (volatile uint32_t *)0xFFFF0000u;
    *p = 0x4F4B4F4Bu;   /* "OKOK"，与 ssbl_config.h BOOT_MAGIC_OK 一致 */
}

int main(void)
{
    xil_printf("\r\n[APP] Hello from app @0x1000000\r\n");
    bsp_init();
    board_init();
    app_boot_log_mark_ok();    /* 唯一对 SSBL 的"配合"，一个寄存器写 */
    tx_kernel_enter();
    /* … */
}
```

并在 `app_template/README.md` 加一节：

```markdown
## app 对 SSBL 的唯一配合：启动期喂狗

app 的 main() 必须在 bsp_init 之后**立即**写一次 OCM 高地址 `0xFFFF0000` 为 `0x4F4B4F4B`（"OKOK"）。这是 SSBL 判定"app 启动成功"的唯一判据（spec §10.4）。

不喂狗的后果：下次启动 SSBL 会把本次记为 `FAIL: app_not_started`，并可能触发 A/B 回退（未来）。

模板已默认包含 `app_boot_log_mark_ok()`，复制改造时保留即可。
```

- [ ] **Step 6：验证状态机（spec §10.4）**

| 场景 | 操作 | 期望 |
|---|---|---|
| 首启 | 烧全新 SD，上电 | `[blog] first boot or POR`；不写 boot.log |
| 正常启动 | app_template 启动并喂狗 | 下次启动写 `boot OK: app=app_current.bin` |
| app 不喂狗 | 把 app 改成不调 mark_ok | 下次启动写 `boot FAIL: app_not_started: app=...` |
| 软复位 | CLI `reset` | 下次启动 OCM 是 ATTEMPT（reset 前刚 mark）→ 写 FAIL（因 app 没机会喂狗）；这是已知限制 |

- [ ] **Step 7：提交**

```bash
git add boot/ssbl/src/boot_log.c boot/ssbl/src/boot_log.h \
        boot/ssbl/src/main.c boot/ssbl/src/boot_selector.c \
        boot/app_template/src/main.c boot/app_template/README.md
git commit -m "Phase 10.3: OCM magic state machine + boot.log ringback (spec §10.4 revised)"
```

---

### Task 10.4：CLI `status` 命令接 boot.log

> **★ 随 Task 10.3 同步推迟（spec §10.4）**：`status` 命令依赖 `boot_log_last_entry`，而后者依赖 FAT 持久化（当前推迟）。本 Task **当前阶段跳过**——`status` 命令可临时改为"从 OCM magic 读上次状态并串口打印"（只读 OCM，无 FAT 写），作为占位。完整版（读 boot.log 历史 8 条）待 Task 10.3 的 FAT 持久化 reintroduce 后再补。

**Files:**
- Modify: `boot/ssbl/cli/cli_commands.c`

- [ ] **Step 1：cli_cmd_status 改读 boot_log_last_entry**

```c
#include "boot_log.h"

int cli_cmd_status(int argc, char **argv)
{
    (void)argc; (void)argv;
    char line[128];
    if (boot_log_last_entry(line, sizeof(line)) == 0) {
        xil_printf("last boot: %s\r\n", line);
    } else {
        xil_printf("no boot.log yet\r\n");
    }
    return 0;
}
```

- [ ] **Step 2：验证**

```
ssbl> status
last boot: [0] boot OK: app=app_current.bin
```

- [ ] **Step 3：提交**

```bash
git add boot/ssbl/cli/cli_commands.c
git commit -m "Phase 10.4: status command reads boot.log"
```

---
