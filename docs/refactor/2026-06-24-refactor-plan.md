# SSBL / FSBL 重构计划

> 起始时间：2026-06-24，Phase 9 基本完成后启动，Phase 8（YMODEM）暂跳过。
>
> 目标：从零重敲所有代码。FSBL 极简、SSBL 充分发挥 ThreadX、整体精简易读可维护；
> 引入开源组件（EasyLogger 日志、开源 CLI 等）。
>
> 本文档为**讨论结论的逐条沉淀**，每个决策点定稿一条。讨论顺序 = 地基 → 上层。

---

## 0. 重构总原则

1. **单层抽象优先**：不为本就有抽象能力的层再包一层（FileX 已是文件系统，不再套文件操作虚表）。
2. **介质无关靠编译期宏**，不靠运行期多态：切 SD/QSPI 只改宏，不动工程源文件列表。
3. **充分发挥 ThreadX**：用足线程/信号量/事件标志/互斥/内存池/软件定时器，不闲置。
4. **FSBL 与 SSBL 职责分明**：FSBL 裸机极简只搬运，SSBL 才是业务主体。

---

## 1. 存储层（已定稿）

### 1.1 结论

删除手写的 `storage_ops_t` 文件操作虚表层。FileX 本身即文件系统，上层**直接调 FileX 原生 API**。
SD / QSPI 两个介质 port 文件**始终都在工程里、都参与编译**，运行期用哪个由宏决定——
宏控制传给 `fx_media_open` 的 driver 入口参数（一个函数指针），而非挑哪个文件参与编译。

切介质 = 改一个 `USE_STORAGE_*` 宏重新定义 `FX_MEDIA_DRIVER`，工程文件一个都不用动。

### 1.2 架构

```
应用层（cli / loader / config / boot_selector）
    │ 直接调 FileX 原生 API
    ▼
    fx_media_open / fx_media_format
    fx_file_open / fx_file_read / fx_file_write / fx_file_close
    fx_directory_first_full_entry_find ... 等等
─────────────────────────────────────────────
FileX（唯一文件系统层，vendor 拷贝，不改）
    │ fx_media_open 注册的 I/O driver 回调
    ▼
运行期读 boot mode 选 driver（两个 port 文件都编译）：

  boot_mode = Xil_In32(BOOT_MODE_REG) & BOOT_MODES_MASK;   // SLCR +0x25C
  if      (boot_mode == SD_MODE)   driver = fx_zynq_sdio_driver;
  else if (boot_mode == QSPI_MODE) driver = fx_zynq_qspi_flash_driver;

  fx_zynq_sdio_driver.c        → 调 XSdPs      （始终编译）
  fx_zynq_qspi_flash_driver.c  → 调 XQspiPs    （始终编译）
─────────────────────────────────────────────
SD 硬件 / QSPI 硬件
```

> **与 FSBL 机制一致**：FSBL 用 `MoveImage = SDAccess/QspiAccess` 运行期选介质，
> SSBL 用 `fx_media_open` 的 driver 参数运行期选 FileX 后端。两者都读同一个
> boot mode 寄存器，机制统一。boot mode 寄存器（SLCR `+0x25C`）在 FSBL handoff
> 后状态保留，SSBL 能读到。

### 1.3 删除清单

| 文件 / 符号 | 处置 |
|---|---|
| `storage/storage.h` | 删除（`storage_ops_t` / `STORAGE_ERR_*` / `storage_*` 宏全删） |
| `storage/sd_port.c` | 删除 |
| `storage/storage_fx_glue.c` | 删除 |
| `BSP/driver/sd_driver.c`（device_core 里的块设备驱动） | 删除 |
| 应用层所有 `storage_file_read()` 等调用 | 改为 `fx_file_read()` |
| 错误码 | 用 FileX 原生 `FX_*` 返回码，不再翻译 |

### 1.4 待办（重写时核对）

- [ ] 应用层（cli_commands / boot_config / image_loader / bitstream_loader）的 `storage_*` 调用逐处改 `fx_*`
- [ ] 在 SSBL 启动流程（线程上下文）里读 boot mode → 选 driver → fx_media_open
- [ ] 确认两个 port 都纳入工程编译、都被链接（接受体积略大）
- [ ] BOOT_MODE_REG 等常量来源：FSBL 的 fsbl_config.h 是 FSBL 私有，SSBL 需在自己
      的 config 头里定义同样的 boot mode 常量（或抽一个共享头，但 FSBL/SSBL 是独立
      工程，暂建议各自定义）

### 1.5 后续（非存储层决策，留空待补）

- 字符外设（uart / led / key）框架去留：**留到写应用代码时再定**，本阶段不动。
- device_core 框架：暂不引入存储侧，字符外设是否沿用框架**待定**。

---

## 2. FSBL 瘦身（已定稿）

### 2.1 背景

BOOT.BIN 永远只有 **FSBL + SSBL 两个 partition**，bitstream 和 app 都交给 SSBL
下载。SSBL 是普通 PS ELF，SD/QSPI 启动、不加密不签名。这条组合下，
image_mover 原有的大半分支都是死代码。

### 2.2 结论

**重写 `image_mover.c` 为干净版，删除 `pcap.c`，保留 `sd.c` + `qspi.c`（双介质）。**

FSBL 重写后的核心流程：

```
ps7_init → DDR 测试 → 读 boot mode 寄存器 → 设 MoveImage 指针
   → LoadBootImage:
       读 boot header → 取 partition[1] → 校验 header checksum
       → MoveImage(源偏移, DDR加载地址, 长度) → 返回 ExecAddr
   → FsblHandoff(ExecAddr) → 跳转
```

### 2.3 文件处置

| 文件 | 现状 | 重写后 |
|---|---|---|
| `image_mover.c` (871行) | 已改成只加载 partition[1]，但带一大堆死分支 | 重写为干净版，~150 行核心 |
| `pcap.c` (703行) | 被 main 的 `InitPcap()` 调用，但单分区+PS ELF 路径走不到 PCAP | **整个文件删除**；bitstream 交给 SSBL 阶段的 `bitstream_loader`（它自己用 devcfg） |
| `pcap.h` | 同上 | 删除 |
| `qspi.c` (859行) | `#ifdef XPAR_PS7_QSPI_LINEAR_0_SAXI_BASEADDR` 关着 | **保留**，参与编译（FSBL 要支持 QSPI 启动） |
| `sd.c` (142行) | 在用 | 保留，参与编译 |
| `main.c` (275行) | `InitPcap()` 调用、MoveImage 分支 | 删 `InitPcap` 调用，保留 boot mode 分支逻辑 |

### 2.4 介质抽象（FSBL 侧，与 SSBL 侧不同）

FSBL 读 BOOT.BIN 时**没有文件系统**——BOOT.BIN 是 bootgen 生成的裸镜像，
FSBL 按扇区/地址读原始字节。介质抽象靠**一个全局函数指针 `MoveImage`**，
不用虚表：

```c
/* sd.c / qspi.c 各提供一个 */
u32 SDAccess  (u32 SrcAddr, u32 DestAddr, u32 Length);  /* XSdPs   读扇区 */
u32 QspiAccess(u32 SrcAddr, u32 DestAddr, u32 Length);  /* XQspiPs / 线性映射 memcpy */

/* main.c 运行期读 boot mode 决定走哪个 */
extern ImageMoverType MoveImage;   /* typedef u32 (*ImageMoverType)(u32,u32,u32); */

if (BootModeRegister == SD_MODE)        MoveImage = SDAccess;
else if (BootModeRegister == QSPI_MODE) MoveImage = QspiAccess;
```

- SD 与 QSPI **都参与编译**，运行期按 boot mode 寄存器选路径。
- 同一份 BOOT.BIN 镜像烧到 SD 或 QSPI 都能启动。

### 2.5 SSBL vs FSBL 介质抽象对照（避免混淆）

| 维度 | FSBL | SSBL |
|---|---|---|
| 有无文件系统 | 无（裸镜像 BOOT.BIN） | 有（FileX，读写 boot.cfg / app.bin） |
| 介质抽象手段 | 一个函数指针 `MoveImage` | FileX driver 回调（宏二选一） |
| SD/QSPI 切换 | 运行期读 boot mode | 编译期 `USE_STORAGE_*` 宏 |
| 为何不同 | BOOT.BIN 必须两种介质同镜像通用 | SSBL 一般固定一种介质烧录 |

### 2.6 待办（重写时核对）

- [ ] image_mover.c 重写：保留 `ValidatePartitionHeaderChecksum`（~30行，防 BOOT.BIN 损坏，免费保险）
- [ ] 删除 pcap.c / pcap.h，main.c 里删 `InitPcap()` 调用
- [ ] 确认 QSPI 分支的 `#ifdef` 是否还需保留（既然 QSPI 要支持，应该去掉 `#ifdef` 关闭，让它常开）
- [ ] fsbl_handoff.S 跳转逻辑保持不变（cache/MMU 清理顺序）
- [ ] PS7 启动异常处理保留（RegisterHandlers + OutputStatus + FsblFallback）

### 2.7 文件拆分（已定稿）

按职责拆分，`fsbl.h` 大杂烩拆掉（尤其去掉对 `pcap.h` 的依赖）。

```
src/
├── main.c              仅入口：main() 顺序编排各阶段，每步失败→OutputStatus+Fallback
├── image_mover.c/.h    boot header 解析 + 校验 checksum + MoveImage 搬运到 DDR
├── sd.c/.h             SDAccess()：XSdPs 读扇区；InitSD() 初始化
├── qspi.c/.h           QspiAccess()：XQspiPs 读；InitQspi() 初始化
├── handoff.c/.h        ClearFSBLIn() + FsblHandoff()：跳转前收尾
├── error.c/.h          OutputStatus() + FsblFallback() + 6 个异常 handler + RegisterHandlers()
├── fsbl_config.h       所有纯常量集中（寄存器/boot mode/错误码/boot header 偏移）
├── fsbl_debug.h        **删除**（fsbl_printf 宏全删，FSBL 全程只用 xil_printf）
├── fsbl_handoff.S      （保留）跳转汇编
└── ps7_init.c/.h       （保留，Vivado 生成，不动）
```

**main.c 编排流程（~50 行，读起来像流程图）：**

```
main()
  ├─ ps7_init()                  // 生成,不动
  ├─ SlcrUnlock()
  ├─ Xil_DCacheFlush / Disable
  ├─ RegisterHandlers()          // error.c
  ├─ DDRInitCheck()              // 留在 main.c（仅 20 行,不够单独成文件）
  ├─ boot_mode = Xil_In32(BOOT_MODE_REG) & BOOT_MODES_MASK
  ├─ if SD:    InitSD();   MoveImage = SDAccess;
  │  elif QSPI: InitQspi(); MoveImage = QspiAccess;
  ├─ ExecAddr = LoadBootImage()  // image_mover.c，内部用全局 MoveImage
  └─ FsblHandoff(ExecAddr)       // handoff.c → fsbl_handoff.S
```

**image_mover 边界（MoveImage 全局函数指针，vendor 风格）：**

```c
/* image_mover.h */
typedef u32 (*ImageMoverType)(u32 Src, u32 Dest, u32 Len);
extern ImageMoverType MoveImage;   /* image_mover.c 定义，main.c 运行期赋值 */
u32 LoadBootImage(void);           /* 唯一对外函数，返回 ExecAddr */
```

- boot mode 只在 main 读一次、设置一次 `MoveImage`。
- image_mover 只管"用 MoveImage 解析 boot header 并搬运"，不碰介质选择。
- 初始化（InitSD/InitQspi）也在 main 显式调用，职责清楚。

**fsbl_config.h（单文件集中所有常量）：**

| 类别 | 例子 | 使用者 |
|---|---|---|
| 错误码 | `DDR_INIT_FAIL`、`INVALID_HEADER_FAIL`、`EXCEPTION_ID_*` | error.c / main / image_mover |
| 寄存器地址 | `BOOT_MODE_REG`、`REBOOT_STATUS_REG` | main / handoff |
| boot mode | `SD_MODE`、`QSPI_MODE`、`BOOT_MODES_MASK` | main |
| boot header 偏移 | `IMAGE_PHDR_OFFSET`、`IMAGE_BYTE_LEN_OFFSET` | image_mover |
| SLCR 宏 | `SlcrUnlock()`、`SlcrLock()` | main / handoff |
| DDR 地址 | `DDR_START_ADDR`、`DDR_TEST_PATTERN` | main(DDRInitCheck) |
| 分区头结构 | `PartHeader`、`MAX_PARTITION_NUMBER` | image_mover |

**删除清单：**
- `pcap.c` / `pcap.h`（解掉 fsbl.h 对 pcap.h 的依赖）
- `fsbl_debug.h` / `fsbl_printf` 宏（FSBL 全程只用 `xil_printf`，无调试分级；
  原来的 `fsbl_printf(DEBUG_GENERAL, ...)` / `fsbl_printf(DEBUG_INFO, ...)` 全部
  改为直接 `xil_printf(...)` 或按需删除）
- `fsbl_hooks.c` / `fsbl_hooks.h`（4 个空 hook，删 pcap 后 BeforeBitstream/AfterBitstream 永不调用，BeforeHandoff/Fallback 逻辑内联进 handoff.c）
- `fsbl.h` 大杂烩头（拆成各模块的 .h + fsbl_config.h）
- `strcpy_rom()`（没用上）

---

## 3. SSBL 骨架 + ThreadX 深度使用（已定稿）

### 3.1 目录结构（按线程归属划分）

**原则：每个线程一个 `task_*` 文件夹，工具组件进 `utils/`，纯硬件进 `bsp/`，
`main.c` 在最外层作为编排中枢。** 目录结构本身即线程架构的投影。

```
ssbl/src/
├── main.c                 AppTaskStart + AppTaskCreate + AppObjCreate（最外层编排中枢）
├── task_boot/             ← AppTaskAutoBoot 的家
│   ├── auto_boot.c         AppTaskAutoBoot() 入口 + 自动启动决策（等窗口/被打断进CLI/超时auto-boot）
│   ├── boot_selector.c     auto-boot 加载逻辑（被 auto_boot 调）
│   ├── boot_config.c       读写 boot.cfg
│   └── handoff.c/.S        jump_to_app（boot 流程终点）
├── task_cli/              ← AppTaskCli 的家
│   ├── cli.c               AppTaskCli() 入口 + 命令分发
│   └── (命令表 / 行编辑 等)
├── task_trigger/          ← AppTaskTrigger 的家
│   └── cli_trigger.c       AppTaskTrigger() 入口 + 触发监听
├── utils/                 可复用的业务/工具组件（非线程专属）
│   ├── loader/             image_loader / bitstream_loader / crc32 / image_header
│   ├── log/                EasyLogger
│   └── (其他组件)
└── bsp/                   纯硬件 + 外设驱动
    ├── storage/            storage_init（读 boot mode → 选 FileX driver → fx_media_open）
    ├── uart / led / key    字符外设（框架去留留到应用阶段再定）
    └── ...
```

**三层划分：**
- `task_*` = 线程的家（每个并发单元一个目录，目录名 = 线程职责）
- `utils/` = 可复用业务/工具组件（loader、log，被多个线程调用）
- `bsp/` = 硬件相关（外设驱动、存储初始化）
- `main.c` 顶层 = 编排中枢（OS 资源清单 + 创建）

> ThreadX / FileX vendor 拷贝不动。fx_zynq_sdio_driver.c / fx_zynq_qspi_flash_driver.c
> 两个 port 放 bsp/storage/（或 vendor 目录，重写时定）。

### 3.2 ThreadX 风格样板

**严格参考** `D:\RTOS_Study\threadx\vitis\zynq7000\hello_filex\src\main.c`
（安富莱 ThreadX 教程风格）。命名、变量定义位置、创建结构都照搬。

### 3.3 main.c 结构（核心）

main.c 是整个 SSBL 的"资源清单 + 创建中枢"，**所有 OS 资源定义和创建都在此**。
除 `AppTaskStart` 外，其他任务函数**只通过 extern 声明**，函数体在各 `task_*` 模块。

```c
/* ── 宏区 ── */
#define  APP_CFG_TASK_START_PRIO       2u
#define  APP_CFG_TASK_CLI_PRIO         10u
#define  APP_CFG_TASK_TRIGGER_PRIO     11u
#define  APP_CFG_TASK_AUTO_BOOT_PRIO   5u
#define  APP_CFG_TASK_START_STK_SIZE   4096u
#define  APP_CFG_TASK_CLI_STK_SIZE     4096u
/* ... */

/* ── 静态全局变量：所有 OS 控制块 + 栈 ── */
static  TX_THREAD   AppTaskStartTCB;
static  uint64_t    AppTaskStartStk[APP_CFG_TASK_START_STK_SIZE/8];
static  TX_THREAD   AppTaskCliTCB;
static  uint64_t    AppTaskCliStk[APP_CFG_TASK_CLI_STK_SIZE/8];
static  TX_THREAD   AppTaskTriggerTCB;
static  uint64_t    AppTaskTriggerStk[...];
static  TX_THREAD   AppTaskAutoBootTCB;
static  uint64_t    AppTaskAutoBootStk[...];

/* ── 通信原语（mutex / event / semaphore / timer / queue）── */
static  TX_MUTEX            AppPrintfSemp;          /* printf 互斥 */
static  TX_EVENT_FLAGS_GROUP cli_trigger_flags;     /* CLI 触发事件 */
static  TX_MUTEX             g_storage_mutex;       /* FileX 访问串行化 */
/* ... 视需要加 semaphore / timer / queue ... */

/* ── 函数声明 ── */
static  void  AppTaskStart   (ULONG thread_input);
static  void  AppTaskCreate  (void);
static  void  AppObjCreate   (void);
extern  void  AppTaskCli       (ULONG thread_input);   /* task_cli/cli.c */
extern  void  AppTaskTrigger   (ULONG thread_input);   /* task_trigger/cli_trigger.c */
extern  void  AppTaskAutoBoot  (ULONG thread_input);   /* task_boot/auto_boot.c */

int main(void)
{
    xil_printf("[SSBL] ...\r\n");
    bsp_init();
    board_init();
    tx_kernel_enter();
    while(1);
}

void tx_application_define(void *first_unused_memory)
{
    /* 只创建 AppTaskStart，其他什么都不做 */
    tx_thread_create(&AppTaskStartTCB, "App Task Start", AppTaskStart, 0,
                     &AppTaskStartStk[0], APP_CFG_TASK_START_STK_SIZE,
                     APP_CFG_TASK_START_PRIO, APP_CFG_TASK_START_PRIO,
                     TX_NO_TIME_SLICE, TX_AUTO_START);
}

static void AppTaskStart(ULONG thread_input)
{
    /* 1. 硬件/FileX/cfg 初始化（线程上下文，可阻塞）*/
    fx_system_initialize();
    storage_media_open();          /* 读 boot mode → 选 driver → fx_media_open */
    boot_config_load(&g_runtime_cfg);

    /* 2. 创建其他任务 + 通信原语 */
    AppTaskCreate();
    AppObjCreate();

    /* 3. AppTaskStart 不自杀，长期存活（可做统计/监控，或单纯 sleep）*/
    while (1) {
        tx_thread_sleep(1);
    }
}

static void AppTaskCreate(void)
{
    /* 创建 cli / trigger / auto_boot 线程，入口函数直接传模块里的 extern 函数 */
    tx_thread_create(&AppTaskCliTCB, "App Task Cli", AppTaskCli, 0,
                     &AppTaskCliStk[0], APP_CFG_TASK_CLI_STK_SIZE,
                     APP_CFG_TASK_CLI_PRIO, APP_CFG_TASK_CLI_PRIO,
                     TX_NO_TIME_SLICE, TX_DONT_START);   /* CLI 等 activate */
    /* ... trigger / auto_boot 同理 ... */
}

static void AppObjCreate(void)
{
    tx_mutex_create(&AppPrintfSemp, "AppPrintfSemp", TX_NO_INHERIT);
    tx_event_flags_create(&cli_trigger_flags, "cli_trigger_flags");
    tx_mutex_create(&g_storage_mutex, "storage_mutex", TX_INHERIT);
    /* ... */
}
```

### 3.4 任务/资源清单（SSBL 业务）

| 任务/原语 | TCB/变量名 | 优先级 | 入口函数（定义位置） | 说明 |
|---|---|---|---|---|
| AppTaskStart | `AppTaskStartTCB` | 2 | main.c (static) | 初始化编排 + 长期存活 |
| AppTaskCli | `AppTaskCliTCB` | 10 | `AppTaskCli` @ task_cli/cli.c | CLI 主循环，DONT_START 等 activate |
| AppTaskTrigger | `AppTaskTriggerTCB` | 11 | `AppTaskTrigger` @ task_trigger/cli_trigger.c | 监听按键/魔数触发 CLI |
| AppTaskAutoBoot | `AppTaskAutoBootTCB` | 5 | `AppTaskAutoBoot` @ task_boot/auto_boot.c | 自动启动决策（等窗口/被打断进CLI/超时auto-boot） |
| printf 互斥 | `AppPrintfSemp` | — | — | 多线程 printf 串行化 |
| CLI 触发事件 | `cli_trigger_flags` | — | — | trigger → auto_boot 的事件传递 |
| FileX 互斥 | `g_storage_mutex` | — | — | CLI/网络并发访问 FileX 串行化 |

### 3.5 关键决策记录

1. **auto_boot 保留为线程**（不用 tx_timer）：tx_timer 回调运行在 timer thread、
   不能阻塞，而 boot_selector 要读 SD（阻塞 IO）。保留线程可阻塞、逻辑集中。
2. **OS 控制块 + 栈全在 main.c**（static），不分散到模块。main.c = 资源清单。
3. **业务函数体在各模块**，main.c 只 extern 声明，`tx_thread_create` 直接传模块函数。
4. **AppTaskStart 不自杀**，`while(1) tx_thread_sleep(1)` 长期存活（可挂监控/统计）。
5. `tx_application_define` 只创建 AppTaskStart，其他在 AppTaskStart 里创建
   （FileX 初始化必须线程上下文，故不能放 tx_application_define）。

### 3.6 待办（重写时核对）

- [ ] 确认 cli/trigger/auto_boot 三个入口函数在各模块的非 static 声明
- [ ] 确认通信原语清单（event_flags 肯定要，mutex 视网络/并发加，semaphore 视 UART 驱动加）
- [ ] AppTaskStart 里 FileX 挂载失败 → 进 CLI 的容错路径
- [ ] handoff.c 的 jump_to_app 收尾：终止 cli/trigger/auto_boot 线程 + 停 GIC/timer

---

## 4. 日志：EasyLogger（已定稿）

### 4.1 结论

**引入 EasyLogger，但要精简**——裁掉用不上的扩展功能，只保留核心日志能力。
放置位置：`utils/log/`。

### 4.2 精简原则（重写时核对）

| 模块 | 处置 | 理由 |
|---|---|---|
| 核心日志（elog_init / elog_d/i/w/e/assert） | **保留** | 日志分级 + 格式化输出的核心价值 |
| 日志输出后端 port（elog_port_output） | **保留** | 接 UART 输出 |
| 异步输出（elog 异步 flush 线程） | **视情况保留/精简** | SSBL 有 ThreadX 可用异步；FSBL 无 RTOS 用同步 |
| 日志落盘（elog 文件后端） | **砍掉** | boot.log 落盘需求未定，不预先引入 |
| hexdump（elog_raw / 16 进制转储） | **砍掉** | 调试用，SSBL 无需 |
| 颜色/插件/扩展 | **砍掉** | 精简代码量 |

> 取代现状的手搓 `app_print.c`（397 行自写 printf）。EasyLogger 提供分级
> （ELOG_LVL_ERROR/WARN/INFO/DEBUG）+ 时间戳 + 模块标签，远比手搓的强。

### 4.3 FSBL / SSBL 差异

- **FSBL（无 RTOS）**：EasyLogger 同步模式（elog_port_output 直接阻塞输出），
  或 FSBL 阶段干脆沿用 xil_printf、只 SSBL 上 EasyLogger。
- **SSBL（有 ThreadX）**：EasyLogger 异步模式（flush 线程），不阻塞业务线程。

> 具体到 FSBL 是否也接 EasyLogger，重写时按"统一日志格式"的价值再权衡，
  不在本轮讨论展开。

---

## 5. CLI 开源组件选型（已定稿）

### 5.1 结论

**引入 Letter shell，命令表集中注册，精简配置。** 取代现有手写 CLI
（`cli.c` + `readline.c` + `cli_command_table.h`，共 841 行）。
位置：`task_cli/`（`AppTaskCli` 的家）。

> 本地源码参考：`D:/RTOS_Study/phytium-threadx-sdk/third-party/letter-shell`
> （该 SDK 已有 ThreadX port 示例，移植有参考）。

### 5.2 移植要点（4 个对接点）

| 对接点 | Letter shell 接口 | 我们这边接什么 |
|---|---|---|
| 字符输出 | `shell_object.write` | UART putc |
| 字符读入 | `shell_object.read` | UART getchar（阻塞读） |
| 系统时基 | `SHELL_GET_TICK()` 宏 | `tx_time_get()` 或系统 tick |
| shell 任务 | `shellTask()` 放线程里跑 | `AppTaskCli` 线程 |

### 5.3 精简配置（shell_cfg.h）

| 配置项 | 值 | 理由 |
|---|---|---|
| `SHELL_USING_CMD_EXPORT` | **0**（关闭） | 用命令表集中注册，不依赖链接器段扫描，不改 .ld |
| `SHELL_USING_COMPANION` | 0 | 不用伴生对象（fs/log 扩展） |
| `SHELL_EXEC_UNDEF_FUNC` | 0 | 执行任意地址函数，危险无用 |
| 用户/权限系统 | 禁用 | bootloader 不需要登录鉴权 |
| `SHELL_TASK_WHILE` | 1 | OS 下 shell 任务用 while 循环 |
| 历史记录 / Tab 补全 | **保留** | Letter shell 的核心价值（比手写 CLI 强） |

### 5.4 命令表集中注册

```c
/* task_cli/cli_commands.c */
const ShellCommand cmd_table[] = {
    {"ls",   cmd_ls,   "list files"},
    {"cat",  cmd_cat,  "show file content"},
    {"boot", cmd_boot, "boot selected app"},
    {"cfg",  cmd_cfg,  "show/edit boot config"},
    {"test", cmd_test, "load app without jump"},
    /* 新增命令加到这里 */
};
```

- 取代现有 `cli_command_table.h` 的风格，迁移直观。
- 命令处理函数（cmd_ls 等）分散在各自需要的模块，命令表集中。

### 5.5 与现有 CLI 的关系

| 现有文件 | 处置 |
|---|---|
| `cli/cli.c` + `cli.h` | 删除（Letter shell 的 shell.c 取代） |
| `cli/readline.c/.h` | 删除（Letter shell 自带行编辑） |
| `cli/cli_command_table.h` | 删除（用 Letter shell 的命令表） |
| `cli/cli_commands.c` | 命令逻辑保留，改写为 Letter shell 命令表格式 |
| `cli/cli_trigger.c` | 保留，移到 `task_trigger/`（它仍是独立线程，不归 shell 管） |

### 5.6 待办（重写时核对）

- [ ] 从 `phytium-threadx-sdk` 拷 Letter shell 源码到 `task_cli/letter_shell/`
- [ ] 自写 `shell_port.c`：write/read 接 UART、SHELL_GET_TICK 接 tx_time_get
- [ ] 配置 `shell_cfg.h`（关闭 cmd_export/companion/exec_func/用户系统）
- [ ] 把现有命令（ls/cat/boot/cfg/test）迁移到 Letter shell 命令表
- [ ] `AppTaskCli` 入口里 `shellInit()` + `shellTask()` 主循环
