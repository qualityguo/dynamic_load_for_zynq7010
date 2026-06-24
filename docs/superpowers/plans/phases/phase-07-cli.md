> **章节导航**：
  [← 上一章：boot.cfg 解析 + boot_selector 自动选 app](phase-06-boot-cfg.md) ｜ [↑ 返回总述](../2026-06-22-zynq-multistage-boot.md) ｜ [下一章：YMODEM 两阶段升级 →](phase-08-ymodem-upgrade.md)

> **Phase 元信息**
> - 对应 Spec：`§6`
> - 里程碑：`M7`
> - 交付物：CLI 子系统（触发 + 命令集）
> - 分章文件：`phases/phase-07-cli.md`

> **接口定位**：本章对其他 Phase 的依赖与对外暴露的符号，见
> [总述章 · 模块依赖矩阵](../2026-06-22-zynq-multistage-boot.md#3-模块依赖矩阵)。
> 新增功能时，先查该清单确认接口落点，再回本章扩展。

---
## Phase 7：CLI 子系统（M7）

**目标**：实现 spec §6 的触发机制（GPIO 按键 + UART 魔数 0xDEADBEEF）+ boot_delay 窗口 + 完整命令集。Phase 6 的"失败 idle"改为"失败回 CLI"。Phase 7 完成后，用户可通过串口交互现场操作 SSBL。

**Spec 引用**：§6.1（触发时序）、§6.2（命令集）、§6.3（关键设计：互斥/原子写/rm 安全边界/auto_boot 语义）。

### Task 7.1：写 cli.h 与命令分发器

**Files:**
- Create: `boot/ssbl/cli/include/cli.h`
- Create: `boot/ssbl/cli/include/cli_command_table.h`
- Create: `boot/ssbl/cli/cli.c`

- [ ] **Step 1：写 cli.h**

文件 `boot/ssbl/cli/include/cli.h`：

```c
/* boot/ssbl/cli/include/cli.h — CLI 子系统接口 */
#ifndef SSBL_CLI_H
#define SSBL_CLI_H

#include "tx_api.h"

/* CLI 模块对外只暴露线程创建与一个全局 cli_thread 指针（handoff 用）*/
extern TX_THREAD *cli_thread;

/* 在 tx_application_define 里调一次，创建并启动 CLI 主线程 */
void cli_create(void);

/* CLI 触发器（cli_trigger.c）设置标志后由 countdown 调本函数激活 cli_thread */
void cli_activate(void);

#endif
```

- [ ] **Step 2：写 cli_command_table.h（spec §6.3 #5 宏表）**

文件 `boot/ssbl/cli/include/cli_command_table.h`：

```c
/* boot/ssbl/cli/include/cli_command_table.h — 命令注册宏表
 * 一行一命令，spec §6.3 #5。
 * 实现函数签名：int cli_cmd_xxx(int argc, char **argv);
 *   返回 0=继续 CLI；非 0=退出 CLI（用于 boot 命令跳走）
 */
#ifndef SSBL_CLI_COMMAND_TABLE_H
#define SSBL_CLI_COMMAND_TABLE_H

typedef int (*cli_cmd_fn)(int argc, char **argv);

typedef struct {
    const char  *name;
    cli_cmd_fn   fn;
    const char  *help;
} cli_command_t;

/* 命令实现（cli_commands.c 提供）*/
int cli_cmd_help (int argc, char **argv);
int cli_cmd_info (int argc, char **argv);
int cli_cmd_status(int argc, char **argv);
int cli_cmd_ls   (int argc, char **argv);
int cli_cmd_cat  (int argc, char **argv);
int cli_cmd_ymodem(int argc, char **argv);   /* Phase 8 */
int cli_cmd_rm   (int argc, char **argv);
int cli_cmd_mv   (int argc, char **argv);
int cli_cmd_cfg_show(int argc, char **argv);
int cli_cmd_cfg_set (int argc, char **argv);
int cli_cmd_cfg_save(int argc, char **argv);
int cli_cmd_boot (int argc, char **argv);
int cli_cmd_mem  (int argc, char **argv);
int cli_cmd_test (int argc, char **argv);    /* test bitstream / test app */
int cli_cmd_reset(int argc, char **argv);

/* 宏表主体（cli.c 用 foreach 展开）*/
#define CLI_COMMAND_TABLE                          \
    { "help",     cli_cmd_help,     "list commands"           }, \
    { "info",     cli_cmd_info,     "show SSBL version/layout" }, \
    { "status",   cli_cmd_status,   "last boot status"        }, \
    { "ls",       cli_cmd_ls,       "ls [dir]"                }, \
    { "cat",      cli_cmd_cat,      "cat <file>"              }, \
    { "ymodem",   cli_cmd_ymodem,   "ymodem rx <file>"        }, \
    { "rm",       cli_cmd_rm,       "rm <file>"               }, \
    { "mv",       cli_cmd_mv,       "mv <old> <new>"          }, \
    { "cfg",      cli_cmd_cfg_show, "cfg show / set / save"   }, \
    { "boot",     cli_cmd_boot,     "boot [app] [bit]"        }, \
    { "mem",      cli_cmd_mem,      "mem <addr> [n]"          }, \
    { "test",     cli_cmd_test,     "test bitstream|app <f>"  }, \
    { "reset",    cli_cmd_reset,    "soft reset"              }

#endif
```

> **Note**：`cfg` 是多字命令（`cfg show` / `cfg set` / `cfg save`），在 `cli_cmd_cfg_show` 内部按 argv[1] 分派。表里只注册 `cfg` 入口指向 `cli_cmd_cfg_show`，由其内部 dispatch。

- [ ] **Step 3：写 cli.c（分发器 + readline 主循环）**

文件 `boot/ssbl/cli/cli.c`：

```c
/* boot/ssbl/cli/cli.c — CLI 主线程 + 命令分发器
 *
 * 主循环：readline → tokenize → 找命令 → 调 fn → 打结果/提示符。
 * spec §6.3 #1：CLI 与 boot_selector 互斥——cli_thread 存在期间不跑 selector。
 */

#include "cli.h"
#include "cli_command_table.h"
#include "readline.h"
#include "cli_uart.h"
#include "xil_printf.h"
#include <string.h>

static const cli_command_t g_commands[] = {
    CLI_COMMAND_TABLE
};
#define N_COMMANDS  (sizeof(g_commands) / sizeof(g_commands[0]))

static TX_THREAD s_cli_tcb;
static uint64_t  s_cli_stk[8192 / 8];
TX_THREAD *cli_thread = NULL;

static void print_prompt(void)
{
    xil_printf("\r\nssbl> ");
}

static const cli_command_t *find_cmd(const char *name)
{
    for (size_t i = 0; i < N_COMMANDS; i++) {
        if (strcmp(g_commands[i].name, name) == 0) {
            return &g_commands[i];
        }
    }
    return NULL;
}

static void cli_main(ULONG arg)
{
    (void)arg;
    char line[128];

    xil_printf("\r\n[CLI] enter. Type 'help' for commands.\r\n");
    print_prompt();

    while (1) {
        int n = readline(line, sizeof(line));
        if (n <= 0) {
            print_prompt();
            continue;
        }

        /* tokenize：空格分隔，最多 8 个 token */
        char *argv[8];
        int argc = 0;
        char *tok = strtok(line, " \t");
        while (tok && argc < 8) {
            argv[argc++] = tok;
            tok = strtok(NULL, " \t");
        }
        if (argc == 0) {
            print_prompt();
            continue;
        }

        const cli_command_t *cmd = find_cmd(argv[0]);
        if (!cmd) {
            xil_printf("unknown command: %s\r\n", argv[0]);
            print_prompt();
            continue;
        }

        int rc = cmd->fn(argc, argv);
        if (rc != 0) {
            /* 命令要求退出 CLI（boot 命令跳走）*/
            break;
        }
        print_prompt();
    }

    /* 退出 CLI：正常路径是 boot 命令已 jump_to_app，不会到这 */
    xil_printf("[CLI] exiting (should not reach)\r\n");
    tx_thread_suspend(tx_thread_identify());
}

void cli_create(void)
{
    UINT s = tx_thread_create(&s_cli_tcb, "cli", cli_main, 0,
                              &s_cli_stk[0], sizeof(s_cli_stk),
                              10, 10, TX_NO_TIME_SLICE, TX_DONT_START);
    if (s == TX_SUCCESS) {
        cli_thread = &s_cli_tcb;
    }
}

void cli_activate(void)
{
    if (cli_thread) {
        tx_thread_resume(cli_thread);
    }
}
```

- [ ] **Step 4：提交**

```bash
git add boot/ssbl/cli/
git commit -m "Phase 7.1: CLI dispatcher + command table macro (spec §6.2 + §6.3 #5)"
```

---

### Task 7.2：写 readline + cli_uart

**Files:**
- Create: `boot/ssbl/cli/readline.c`
- Create: `boot/ssbl/cli/include/readline.h`
- Create: `boot/ssbl/cli/cli_uart.c`
- Create: `boot/ssbl/cli/include/cli_uart.h`

- [ ] **Step 1：写 readline.h**

```c
/* boot/ssbl/cli/include/readline.h */
#ifndef SSBL_READLINE_H
#define SSBL_READLINE_H

/* 阻塞读一行（含简单退格处理），返回行长度（不含末尾 \0），≤0=EOF/错误 */
int readline(char *buf, int maxlen);

#endif
```

- [ ] **Step 2：写 readline.c（最小版：阻塞 getchar + 退格）**

```c
/* boot/ssbl/cli/readline.c — Phase 7 最小版
 * 退格、方向键、历史 spec §3.2 标 "[新]"，但本 Phase 先实现退格，方向键/历史 Phase 8 之后加。
 */

#include "readline.h"
#include "cli_uart.h"
#include "xil_printf.h"

int readline(char *buf, int maxlen)
{
    if (maxlen <= 0) return -1;
    int n = 0;
    while (n < maxlen - 1) {
        int c = cli_uart_getchar();   /* 阻塞，返回 0..255 或 -1 */
        if (c < 0) return -1;
        if (c == '\r' || c == '\n') {
            xil_printf("\r\n");
            buf[n] = '\0';
            return n;
        }
        if (c == 0x7F || c == 0x08) {   /* DEL 或 BS */
            if (n > 0) {
                n--;
                xil_printf("\b \b");
            }
            continue;
        }
        if (c < 0x20) continue;   /* 其它控制字符忽略 */
        buf[n++] = (char)c;
        xil_printf("%c", c);      /* 回显 */
    }
    buf[maxlen - 1] = '\0';
    return n;
}
```

- [ ] **Step 3：写 cli_uart.h**

```c
/* boot/ssbl/cli/include/cli_uart.h */
#ifndef SSBL_CLI_UART_H
#define SSBL_CLI_UART_H

#include <stdint.h>

/* 阻塞读 1 字节（0..255），返回 -1 = 无数据/错误。
 * Phase 7 用阻塞轮询；Phase 8 YMODEM 接收时改用 cli_uart_rx_chunk。 */
int  cli_uart_getchar(void);

/* YMODEM 用：阻塞读 N 字节到 buf，返回实际读到的字节数 */
int  cli_uart_rx_chunk(uint8_t *buf, int len);

/* 把魔数检测器挂到 UART RX（cli_trigger 用，spec §6.3 #4）*/
void cli_uart_attach_magic_detector(void (*on_magic)(void));

#endif
```

- [ ] **Step 4：写 cli_uart.c（基于 vendor uart_driver，含魔数滑窗）**

```c
/* boot/ssbl/cli/cli_uart.c — UART 收发 + 魔数 0xDEADBEEF 滑窗检测（spec §6.3 #4）
 * 复用 vendor BSP 的 uart1 device。
 */

#include "cli_uart.h"
#include "includes.h"
#include "xil_printf.h"

#define MAGIC_WORD  0xDEADBEEFu

static struct device *s_uart = NULL;
static void (*s_on_magic)(void) = NULL;

void cli_uart_init(void)
{
    s_uart = device_find("uart1");
}

int cli_uart_getchar(void)
{
    if (!s_uart) return -1;
    uint8_t c;
    int n = device_read(s_uart, &c, 1);
    if (n != 1) return -1;

    /* 魔数滑窗（4 字节）*/
    static uint32_t window = 0;
    window = (window << 8) | c;
    if ((window & 0xFFFFFFFFu) == MAGIC_WORD) {
        if (s_on_magic) s_on_magic();
    }
    return c;
}

int cli_uart_rx_chunk(uint8_t *buf, int len)
{
    if (!s_uart) return -1;
    int total = 0;
    while (total < len) {
        uint8_t c;
        int n = device_read(s_uart, &c, 1);
        if (n != 1) break;
        /* 不在 chunk 模式做魔数检测（YMODEM 流会含任意字节）*/
        buf[total++] = c;
    }
    return total;
}

void cli_uart_attach_magic_detector(void (*on_magic)(void))
{
    s_on_magic = on_magic;
}
```

- [ ] **Step 5：提交**

```bash
git add boot/ssbl/cli/readline.c boot/ssbl/cli/include/readline.h \
        boot/ssbl/cli/cli_uart.c boot/ssbl/cli/include/cli_uart.h
git commit -m "Phase 7.2: readline (minimal) + cli_uart with 0xDEADBEEF magic detector"
```

---

### Task 7.3：写 cli_trigger.c（GPIO + UART 魔数双路触发）

**Files:**
- Create: `boot/ssbl/cli/cli_trigger.c`
- Create: `boot/ssbl/cli/include/cli_trigger.h`

- [ ] **Step 1：写 cli_trigger.h**

```c
/* boot/ssbl/cli/include/cli_trigger.h */
#ifndef SSBL_CLI_TRIGGER_H
#define SSBL_CLI_TRIGGER_H

#include "tx_api.h"

/* 触发事件标志位 */
#define CLI_TRIGGER_FLAG_GPIO   (1u << 0)
#define CLI_TRIGGER_FLAG_UART   (1u << 1)
#define CLI_TRIGGER_FLAG_ANY    (CLI_TRIGGER_FLAG_GPIO | CLI_TRIGGER_FLAG_UART)

extern TX_EVENT_FLAGS_GROUP g_cli_trigger_flags;

/* 在 tx_application_define 调一次：初始化 GPIO/UART + 创建事件标志组 */
void cli_trigger_init(void);

/* trigger_monitor 线程入口（由 tx_application_define 创建并启动）*/
void cli_trigger_create(void);

#endif
```

- [ ] **Step 2：写 cli_trigger.c（spec §6.3 #4）**

```c
/* boot/ssbl/cli/cli_trigger.c — 双路触发：GPIO 按键 + UART 魔数
 *
 * - GPIO：10ms 周期采样 MIO 引脚，消抖后置 CLI_TRIGGER_FLAG_GPIO
 * - UART：cli_uart.c 的魔数滑窗命中回调，置 CLI_TRIGGER_FLAG_UART
 * - countdown 线程 tx_event_flags_get 等待 FLAG_ANY，超时则不进 CLI
 */

#include "cli_trigger.h"
#include "cli_uart.h"
#include "includes.h"
#include "xil_printf.h"

TX_EVENT_FLAGS_GROUP g_cli_trigger_flags;

#define GPIO_DEBOUNCE_TICKS  3    /* 3 ticks = 30ms（tick=10ms）*/
#define GPIO_SAMPLE_PRIO     11

static struct device *s_key = NULL;   /* spec §6.3 #4：MIO 按键 */

static TX_THREAD s_trigger_tcb;
static uint64_t  s_trigger_stk[4096 / 8];
TX_THREAD *trigger_thread = NULL;

static void on_uart_magic(void)
{
    tx_event_flags_set(&g_cli_trigger_flags, CLI_TRIGGER_FLAG_UART, TX_OR);
}

static void trigger_thread_entry(ULONG arg)
{
    (void)arg;
    uint8_t val = 0, prev = 0;
    uint8_t debounce = 0;

    /* GPIO 按键 device 名约定为 "key0"（与 vendor 一致）*/
    s_key = device_find("key0");

    /* UART 魔数挂到 cli_uart */
    cli_uart_attach_magic_detector(on_uart_magic);

    while (1) {
        if (s_key) {
            device_read(s_key, &val, 1);
            if (val != prev) {
                debounce = 0;
                prev = val;
            } else if (val == 1) {   /* 按下 */
                debounce++;
                if (debounce == GPIO_DEBOUNCE_TICKS) {
                    tx_event_flags_set(&g_cli_trigger_flags,
                                       CLI_TRIGGER_FLAG_GPIO, TX_OR);
                }
            }
        }
        tx_thread_sleep(1);   /* 1 tick = 10ms */
    }
}

void cli_trigger_init(void)
{
    tx_event_flags_create(&g_cli_trigger_flags, "cli_trigger_flags");
}

void cli_trigger_create(void)
{
    UINT s = tx_thread_create(&s_trigger_tcb, "trigger", trigger_thread_entry, 0,
                              &s_trigger_stk[0], sizeof(s_trigger_stk),
                              GPIO_SAMPLE_PRIO, GPIO_SAMPLE_PRIO,
                              TX_NO_TIME_SLICE, TX_AUTO_START);
    if (s == TX_SUCCESS) {
        trigger_thread = &s_trigger_tcb;
    }
}
```

- [ ] **Step 3：提交**

```bash
git add boot/ssbl/cli/cli_trigger.c boot/ssbl/cli/include/cli_trigger.h
git commit -m "Phase 7.3: dual-trigger GPIO+UART magic (spec §6.3 #4)"
```

---

### Task 7.4：写 countdown_thread（boot_delay 窗口）

**Files:**
- Create: `boot/ssbl/src/countdown.c`
- Create: `boot/ssbl/src/countdown.h`

- [ ] **Step 1：写 countdown.h**

```c
/* boot/ssbl/src/countdown.h */
#ifndef SSBL_COUNTDOWN_H
#define SSBL_COUNTDOWN_H

#include "tx_api.h"

extern TX_THREAD *countdown_thread;

/* 在 tx_application_create 调一次 */
void countdown_create(void);

#endif
```

- [ ] **Step 2：写 countdown.c（spec §6.1 + §5.2 boot_delay 秒/tick）**

```c
/* boot/ssbl/src/countdown.c — boot_delay 窗口管理（spec §6.1）
 *
 * 行为：
 *   - boot_cfg.auto_boot == 0：本线程不创建（spec §5.3 + §5.1）
 *   - auto_boot == 1：等 boot_delay_seconds 秒（tick=10ms → sleep(N*100)），
 *     期间监听 CLI_TRIGGER_FLAG_ANY：
 *       - 触发 → 激活 cli_thread，本线程结束
 *       - 超时 → 直接调 boot_selector_run（成功则 jump 走）
 */

#include "countdown.h"
#include "cli_trigger.h"
#include "cli.h"
#include "boot_config.h"
#include "boot_selector.h"
#include "xil_printf.h"

static TX_THREAD s_countdown_tcb;
static uint64_t  s_countdown_stk[4096 / 8];
TX_THREAD *countdown_thread = NULL;

extern boot_cfg_t g_runtime_cfg;   /* tx_application_define 写入 */

static void countdown_entry(ULONG arg)
{
    (void)arg;
    ULONG flags = 0;
    UINT s;

    uint32_t wait_ticks = g_runtime_cfg.boot_delay_seconds * 100u;  /* tick=10ms */

    s = tx_event_flags_get(&g_cli_trigger_flags, CLI_TRIGGER_FLAG_ANY,
                           TX_OR_CLEAR, &flags, wait_ticks);

    if (s == TX_SUCCESS) {
        /* 触发了，进 CLI */
        xil_printf("[ssbl] trigger 0x%X, entering CLI\r\n", flags);
        cli_activate();
    } else {
        /* 超时，自动启动 */
        xil_printf("[ssbl] no trigger, auto-boot\r\n");
        int rc = boot_selector_run();
        if (rc != 0) {
            /* 启动失败 → 回退到 CLI（spec §5.3）*/
            xil_printf("[ssbl] boot failed (%d), falling back to CLI\r\n", rc);
            cli_activate();
        }
        /* 成功路径已 jump 走，不到这 */
    }

    /* 本线程使命完成，自杀 */
    tx_thread_terminate(tx_thread_identify());
}

void countdown_create(void)
{
    /* spec §5.3：auto_boot=no 时不创建 countdown，直接进 CLI */
    if (g_runtime_cfg.auto_boot == 0) {
        xil_printf("[ssbl] auto_boot=no, skip countdown\r\n");
        cli_activate();
        return;
    }

    UINT s = tx_thread_create(&s_countdown_tcb, "countdown", countdown_entry, 0,
                              &s_countdown_stk[0], sizeof(s_countdown_stk),
                              5, 5, TX_NO_TIME_SLICE, TX_AUTO_START);
    if (s == TX_SUCCESS) {
        countdown_thread = &s_countdown_tcb;
    }
}
```

- [ ] **Step 3：提交**

```bash
git add boot/ssbl/src/countdown.c boot/ssbl/src/countdown.h
git commit -m "Phase 7.4: countdown thread with boot_delay window + auto_boot=no handling"
```

---

### Task 7.5：tx_application_define 整合 countdown + trigger + cli

**Files:**
- Modify: `boot/ssbl/src/main.c`

- [ ] **Step 1：重写 tx_application_define（spec §6.1 启动时序）**

```c
/* main.c 关键段：tx_application_define */
boot_cfg_t g_runtime_cfg;   /* 全局，countdown 与 cli 共享 */

void tx_application_define(void *first_unused_memory)
{
    (void)first_unused_memory;

    /* 1. 加载 cfg（先于线程创建，因 countdown 要读 auto_boot）*/
    boot_config_load(&g_runtime_cfg);

    /* 2. 初始化子系统 */
    cli_uart_init();
    cli_trigger_init();
    cli_create();   /* TX_DONT_START，等被 cli_activate 唤醒 */

    /* 3. 启动 trigger 监控（GPIO + UART 魔数）*/
    cli_trigger_create();

    /* 4. 启动 countdown（内部读 auto_boot 决定要不要等）*/
    countdown_create();

    /* tx_application_define 自身返回后调度器接管 */
}
```

并删除 Phase 6 的 `ssbl_task_start` 调用 boot_selector 那段（改由 countdown 内部调）。

- [ ] **Step 2：Build + 上电验证触发与超时两条路径**

SD 卡 cfg `auto_boot=yes, boot_delay=3`：

| 操作 | 期望 |
|---|---|
| 上电不动 | 3 秒后自动启动，进 app |
| 上电后 3 秒内按 key0 | 打印 `trigger 0x1`，进 `ssbl>` 提示符 |
| 上电后 3 秒内串口发 `0xDE 0xAD 0xBE 0xEF` | 打印 `trigger 0x2`，进 CLI |

- [ ] **Step 3：验证 auto_boot=no**

cfg 改 `auto_boot=no`：
- 上电**立刻**进 CLI（无 3 秒等待），打印 `[ssbl] auto_boot=no, skip countdown`

- [ ] **Step 4：提交**

```bash
git add boot/ssbl/src/main.c
git commit -m "Phase 7.5: tx_application_define wires cfg/trigger/cli/countdown (spec §6.1)"
```

---

### Task 7.6：写 cli_commands.c（命令实现）

**Files:**
- Create: `boot/ssbl/cli/cli_commands.c`

- [ ] **Step 1：写 cli_commands.c 全部命令（YMODEM 与 cfg save 的部分先 stub，Phase 8 填实）**

文件 `boot/ssbl/cli/cli_commands.c`：

```c
/* boot/ssbl/cli/cli_commands.c — 命令实现（spec §6.2）
 * Phase 7 实现：help/info/status/ls/cat/rm/mv/cfg/boot/mem/test/reset
 * Phase 8 实现：ymodem
 */

#include "cli_command_table.h"
#include "storage.h"
#include "boot_config.h"
#include "boot_selector.h"
#include "image_header.h"
#include "xil_printf.h"
#include <string.h>
#include <stdlib.h>

extern boot_cfg_t g_runtime_cfg;

/* ============================== help ============================== */
int cli_cmd_help(int argc, char **argv)
{
    (void)argc; (void)argv;
    extern const cli_command_t g_commands[];
    extern const int N_COMMANDS;   /* 若 cli.c 用 #define，改这里 */
    /* 简化：直接列举 */
    static const struct { const char *n; const char *h; } t[] = {
        CLI_COMMAND_TABLE /* 注意：这里宏展开是 {n,f,h} 三元组，简化版可直接列举 */
    };
    (void)t;
    xil_printf("Commands:\r\n");
    xil_printf("  help / info / status / ls / cat\r\n");
    xil_printf("  ymodem rx <file> / rm <file> / mv <old> <new>\r\n");
    xil_printf("  cfg show / cfg set key=val / cfg save\r\n");
    xil_printf("  boot [app] [bit] / mem <addr> [n] / test ...\r\n");
    xil_printf("  reset\r\n");
    return 0;
}

/* ============================== info ============================== */
int cli_cmd_info(int argc, char **argv)
{
    (void)argc; (void)argv;
    xil_printf("SSBL version: Phase 7\r\n");
    xil_printf("media:        %s\r\n", "SD (FileX FAT)");
    xil_printf("load addr:    app=0x01000000  ssbl=0x00100000\r\n");
    xil_printf("staging area: 0x02000000 size=10MB\r\n");
    return 0;
}

/* ============================== status ============================== */
int cli_cmd_status(int argc, char **argv)
{
    (void)argc; (void)argv;
    /* Phase 10 才有 boot.log；本 Phase 先打印 cfg */
    xil_printf("cfg: app=%s bit=%s delay=%us auto=%u\r\n",
               g_runtime_cfg.app_name,
               g_runtime_cfg.bit_path ? g_runtime_cfg.bit_path : "(none)",
               g_runtime_cfg.boot_delay_seconds, g_runtime_cfg.auto_boot);
    return 0;
}

/* ============================== ls ============================== */
int cli_cmd_ls(int argc, char **argv)
{
    const char *path = (argc >= 2) ? argv[1] : "/";
    int rc = storage_dir_list(path);
    if (rc != STORAGE_OK) {
        xil_printf("ls %s failed (%d)\r\n", path, rc);
    }
    return 0;
}

/* ============================== cat ============================== */
int cli_cmd_cat(int argc, char **argv)
{
    if (argc < 2) {
        xil_printf("usage: cat <file>\r\n");
        return 0;
    }
    int rc = storage_file_open(argv[1], STORAGE_OPEN_READ);
    if (rc != STORAGE_OK) {
        xil_printf("open %s failed (%d)\r\n", argv[1], rc);
        return 0;
    }
    char buf[512];
    int n;
    while ((n = storage_file_read(buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) xil_printf("%c", buf[i]);
    }
    storage_file_close();
    xil_printf("\r\n");
    return 0;
}

/* ============================== rm ============================== */
int cli_cmd_rm(int argc, char **argv)
{
    if (argc < 2) {
        xil_printf("usage: rm <file>\r\n");
        return 0;
    }

    /* spec §6.3 #7：安全边界 */
    if (strcmp(argv[1], "BOOT.bin") == 0 || strcmp(argv[1], "boot.bin") == 0) {
        xil_printf("ERROR: refuse to delete BOOT.bin\r\n");
        return 0;
    }
    /* 删 boot.cfg / 当前 app/bit 需二次确认 */
    int need_confirm = 0;
    if (strcmp(argv[1], "boot.cfg") == 0) need_confirm = 1;
    if (strcmp(argv[1], g_runtime_cfg.app_name) == 0) need_confirm = 1;
    if (g_runtime_cfg.bit_path && strcmp(argv[1], g_runtime_cfg.bit_path) == 0) {
        need_confirm = 1;
    }
    if (need_confirm) {
        xil_printf("Delete critical file %s? [y/N] ", argv[1]);
        extern int readline(char *, int);
        char ans[8];
        readline(ans, sizeof(ans));
        if (ans[0] != 'y' && ans[0] != 'Y') {
            xil_printf("cancelled\r\n");
            return 0;
        }
    }

    int rc = storage_file_delete(argv[1]);
    if (rc != STORAGE_OK) {
        xil_printf("rm %s failed (%d)\r\n", argv[1], rc);
    } else {
        xil_printf("deleted %s\r\n", argv[1]);
    }
    return 0;
}

/* ============================== mv ============================== */
int cli_cmd_mv(int argc, char **argv)
{
    if (argc < 3) {
        xil_printf("usage: mv <old> <new>\r\n");
        return 0;
    }
    int rc = storage_file_rename(argv[1], argv[2]);
    if (rc != STORAGE_OK) {
        xil_printf("mv failed (%d)\r\n", rc);
    }
    return 0;
}

/* ============================== cfg (show/set/save) ============================== */
int cli_cmd_cfg_show(int argc, char **argv)
{
    if (argc == 1 || strcmp(argv[1], "show") == 0) {
        xil_printf("app=%s\r\nbit=%s\r\nboot_delay=%u\r\nauto_boot=%s\r\n",
                   g_runtime_cfg.app_name,
                   g_runtime_cfg.bit_path ? g_runtime_cfg.bit_path : "",
                   g_runtime_cfg.boot_delay_seconds,
                   g_runtime_cfg.auto_boot ? "yes" : "no");
        return 0;
    }
    if (strcmp(argv[1], "set") == 0) {
        if (argc < 3) {
            xil_printf("usage: cfg set <key>=<val>\r\n");
            return 0;
        }
        /* 解析 argv[2] 形如 "app=app_b.bin" */
        char *eq = strchr(argv[2], '=');
        if (!eq) { xil_printf("bad format\r\n"); return 0; }
        *eq = '\0';
        char *k = argv[2], *v = eq + 1;
        if (strcmp(k, "app") == 0) {
            strncpy(g_runtime_cfg.app_name, v, BOOT_CFG_APP_NAME_MAX - 1);
        } else if (strcmp(k, "bit") == 0) {
            if (*v == '\0' || strcmp(v, "none") == 0) {
                g_runtime_cfg.bit_path = NULL;
            } else {
                strncpy(g_runtime_cfg.bit_path_storage, v,
                        BOOT_CFG_BIT_NAME_MAX - 1);
                g_runtime_cfg.bit_path = g_runtime_cfg.bit_path_storage;
            }
        } else if (strcmp(k, "boot_delay") == 0) {
            g_runtime_cfg.boot_delay_seconds = (uint32_t)strtoul(v, NULL, 10);
        } else if (strcmp(k, "auto_boot") == 0) {
            g_runtime_cfg.auto_boot = (strcmp(v, "yes") == 0) ? 1 : 0;
        } else {
            xil_printf("unknown key %s\r\n", k);
        }
        return 0;
    }
    if (strcmp(argv[1], "save") == 0) {
        int rc = boot_config_save(&g_runtime_cfg);
        xil_printf("cfg %s\r\n", rc == 0 ? "saved" : "save failed");
        return 0;
    }
    xil_printf("usage: cfg show / set / save\r\n");
    return 0;
}

/* 占位：cli_cmd_cfg_set/save 在表里指向 cli_cmd_cfg_show，内部 dispatch */
int cli_cmd_cfg_set (int argc, char **argv) { return cli_cmd_cfg_show(argc, argv); }
int cli_cmd_cfg_save(int argc, char **argv) { return cli_cmd_cfg_show(argc, argv); }

/* ============================== boot ============================== */
int cli_cmd_boot(int argc, char **argv)
{
    /* boot / boot <app> / boot <app> <bit> */
    const char *app = (argc >= 2) ? argv[1] : g_runtime_cfg.app_name;
    const char *bit = (argc >= 3) ? argv[2] : g_runtime_cfg.bit_path;

    uint32_t load_addr = 0;
    int rc = boot_selector_load_only(app, bit, &load_addr);
    if (rc != STORAGE_OK) {
        xil_printf("boot load failed (%d)\r\n", rc);
        return 0;   /* 继续 CLI */
    }

    /* spec §6.3 #2：boot 在 cli_thread 内联 jump_to_app
     * 不 terminate(cli_thread)，让 jump_to_app 直接跳走，线程自然终止 */
    xil_printf("[boot] handoff to app @0x%08X\r\n", load_addr);
    extern void jump_to_app(uint32_t) __attribute__((noreturn));
    jump_to_app(load_addr);   /* noreturn */
}

/* ============================== mem ============================== */
int cli_cmd_mem(int argc, char **argv)
{
    if (argc < 2) { xil_printf("usage: mem <addr> [n]\r\n"); return 0; }
    uint32_t addr = (uint32_t)strtoul(argv[1], NULL, 0);
    int n = (argc >= 3) ? (int)strtoul(argv[2], NULL, 0) : 16;
    volatile uint32_t *p = (volatile uint32_t *)(addr & ~3u);
    for (int i = 0; i < n; i++) {
        if ((i & 3) == 0) xil_printf("\r\n0x%08X: ", addr + i*4);
        xil_printf("%08X ", p[i]);
    }
    xil_printf("\r\n");
    return 0;
}

/* ============================== test ============================== */
int cli_cmd_test(int argc, char **argv)
{
    /* test bitstream <file> : Phase 9 才实现 */
    /* test app <file>       : 仅校验 header+CRC，不跳转 */
    if (argc < 3) {
        xil_printf("usage: test bitstream <file> | test app <file>\r\n");
        return 0;
    }
    if (strcmp(argv[1], "app") == 0) {
        uint32_t load_addr = 0;
        int rc = boot_selector_load_only(argv[2], NULL, &load_addr);
        xil_printf("test app %s: %s (load=0x%08X)\r\n",
                   argv[2], rc == 0 ? "OK" : "FAIL", load_addr);
        /* 注意：load_only 已把 app 拷到 DDR；不跳转 */
    } else if (strcmp(argv[1], "bitstream") == 0) {
        extern int bit_loader_download(const char *);
        int rc = bit_loader_download(argv[2]);
        xil_printf("test bitstream %s: %s\r\n", argv[2],
                   rc == 0 ? "OK" : "FAIL/Phase9");
    } else {
        xil_printf("unknown test target: %s\r\n", argv[1]);
    }
    return 0;
}

/* ============================== reset ============================== */
int cli_cmd_reset(int argc, char **argv)
{
    (void)argc; (void)argv;
    xil_printf("Resetting...\r\n");
    /* Zynq SLCR RESET_CTRL（spec §6.2 reset 命令）*/
    #define SLCR_PSS_RST_CTRL   (*(volatile uint32_t *)0xF8000200u)
    SLCR_PSS_RST_CTRL = 1;   /* 写 1 触发软复位，不返回 */
    while (1);
}

/* ============================== ymodem (Phase 8) ============================== */
int cli_cmd_ymodem(int argc, char **argv)
{
    (void)argc; (void)argv;
    xil_printf("ymodem: not implemented yet (Phase 8)\r\n");
    return 0;
}
```

> **Note**：上例的 `cli_cmd_help` 用了简化版打印。完整版应遍历 `g_commands[]`（cli.c 已定义），可通过把 `g_commands` 声明为非 static、`N_COMMANDS` 用 `sizeof/sizeof` 在 cli.c 暴露出来。本计划为简洁保留简化版，实现期可改。

- [ ] **Step 2：Build + 上电验证 CLI**

进 CLI（按键触发），逐条测试命令：

| 命令 | 期望输出 |
|---|---|
| `help` | 命令列表 |
| `info` | 版本/介质/地址 |
| `ls` | BOOT.bin / boot.cfg / app_current.bin |
| `cat boot.cfg` | cfg 文本 |
| `cfg show` | 解析后字段 |
| `cfg set app=app_b.bin` | 无输出（仅改内存） |
| `cfg save` | `cfg saved` |
| `ls` | 多了 boot.cfg.tmp？应该没有（已 rename） |
| `rm BOOT.bin` | `ERROR: refuse to delete BOOT.bin` |
| `rm boot.cfg` | 二次确认提示 |
| `mem 0x100000 4` | 16 字节 hex dump |
| `boot app_current.bin` | handoff 到 app |
| `reset` | 复位重启 |

- [ ] **Step 3：提交**

```bash
git add boot/ssbl/cli/cli_commands.c
git commit -m "Phase 7.6: full CLI command set (ymodem stub for Phase 8)"
```

---

### Task 7.7：Phase 7 验收（spec §12.2 场景 7-10）

- [ ] **Step 1：跑 spec §12.2 测试场景 7-10**

| # | 场景 | 操作 | 期望 |
|---|---|---|---|
| 7 | GPIO 触发 | boot_delay 内按 key0 | `trigger 0x1`，进 CLI |
| 8 | UART 魔数触发 | boot_delay 内发 `DE AD BE EF` | `trigger 0x2`，进 CLI |
| 9 | 超时启动 | boot_delay 内不触发 | 自动启动 |
| 10 | `boot <app>` 一次性覆盖 | CLI 内 `boot app_b.bin` | 立即 handoff（不改 cfg） |

- [ ] **Step 2：Phase 7 提交**

```bash
git commit --allow-empty -m "Phase 7 verified: trigger + CLI command set functional"
```

---
