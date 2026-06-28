# SSBL 线程任务架构设计

| 项 | 值 |
|---|---|
| 项目 | SSBL 线程任务架构重设计 |
| 日期 | 2026-06-26 |
| 基础 | 现有 SSBL 代码（基于 ThreadX + FileX） |
| 新增 | NetX Duo 网络栈 + HTTP 管理界面 |

---

## 1. 设计目标

- 每个线程职责明确、边界清晰
- HTTP 与 CLI 并存，共享 FileX 资源
- 启动流程统一（trigger 机制控制所有交互入口）
- 文件系统操作和网络初始化解耦到各自任务内

---

## 2. 内存布局（DDR）

```
0x00100000 ┌───────────────────────┐  ← SSBL 起点
           │ SSBL 代码+数据+栈     │  1MB
0x00200000 ├───────────────────────┤
           │ SSBL FileX 缓冲/堆    │  512KB
0x00280000 ├───────────────────────┤
           │ NetX Duo 内存池       │  256KB（数据包池 + TCP 窗口）
0x002C0000 ├───────────────────────┤
           │ (预留)                │  ~212KB
0x01000000 ├───────────────────────┤  ← app 加载区
           │ 主应用加载区          │  ≤7MB
0x02000000 ├───────────────────────┤  ← YMODEM 暂存区
           │ 镜像暂存区 / HTTP 缓冲│  10MB
           └───────────────────────┘
```

---

## 3. 目录结构

```
ssbl/src/
├── main.c                        入口：main() → bsp_init → board_init → tx_kernel_enter
│
├── task_start/                   ← AppTaskStart 的家（优先级 2）
│   ├── start.c                    AppTaskStart() 入口
│   │                               ├─ fx_system_initialize / nx_system_initialize
│   │                               ├─ AppObjCreate()（IPC 资源）
│   │                               ├─ AppTaskCreate()（创建其他任务）
│   │                               ├─ storage_media_open()（挂载文件系统）
│   │                               ├─ boot_config_load()（读 boot.cfg）
│   │                               └─ 倒计时循环（监听 trigger / 超时 boot）
│   └── start.h
│
├── task_trigger/                 ← AppTaskTrigger 的家（优先级 11）
│   ├── trigger.c                   GPIO 按键消抖检测 + UART 魔数滑窗匹配
│   │                               触发 → tx_event_flags_set → 自杀
│   └── trigger.h
│
├── task_cli/                     ← AppTaskCli 的家（优先级 10，DONT_START）
│   ├── cli_port.c                  Letter shell 对接（write/read/tick）
│   ├── cli_shell.c                 AppTaskCli() 入口 + shellTask 循环
│   ├── cli_commands.c              命令表集中注册
│   ├── cli_cmd_fs.c                ls/cat/rm/mv 文件操作
│   ├── cli_cmd_cfg.c               cfg show/set/save
│   ├── cli_cmd_boot.c              boot <app> [bit] / reset
│   ├── cli_cmd_http.c              http status / restart
│   ├── cli_cmd_sys.c               mem / info / help
│   └── letter_shell/               Letter shell 源码拷贝
│
├── task_httpd/                   ← AppTaskHttpd 的家（优先级 8，DONT_START）
│   ├── httpd_nx.c                  NetX Duo 初始化（nx_ip_create / ARP / TCP）
│   ├── httpd_server.c              HTTP 服务器主循环
│   ├── httpd_route.c               URL 路由表注册 + 请求分发
│   ├── httpd_page_files.c          GET  / → 文件列表 HTML
│   ├── httpd_page_cfg.c            GET/POST /boot.cfg → 查看/编辑
│   ├── httpd_upload.c              POST /upload → 文件上传（multipart）
│   ├── httpd_delete.c              POST /delete → 文件删除
│   ├── httpd_boot.c                POST /boot → 触发启动
│   ├── httpd_assets.c              内嵌 CSS/JS 资源
│   └── httpd.h
│
├── boot/                         ← 启动选择器（被多个任务调用）
│   ├── boot_selector.c             boot_selector_run() → 加载 bit + app + handoff
│   ├── boot_config.c               boot.cfg INI 解析/保存
│   ├── image_loader.c              app.bin 裸拷 DDR（无自定义 header）
│   ├── bitstream_loader.c          .bit → PCAP 下载到 PL
│   └── crc32.c                     CRC32 校验（bitstream loader 用，app 不再需要）
│
├── utils/                        工具组件
│   ├── log/                        EasyLogger（或自写分级日志）
│   └── ...
│
├── bsp/                          硬件驱动 + 存储
│   ├── storage/                    storage_media_open（读 boot mode → 选 FileX driver）
│   │   ├── fx_zynq_sdio_driver.c    SD 卡 FileX port
│   │   └── fx_zynq_qspi_flash_driver.c  QSPI FileX port（后期）
│   └── uart / led / key / emac     外设驱动
│
├── handoff/                      SSBL → App 跳转
│   ├── handoff.c                   jump_to_app()：停止线程/GIC/定时器 → handoff_exit
│   └── handoff_exit.S              clean cache → 关 MMU → bx 跳转
│
└── ThreadX/ FileX/ NetX/         vendor 拷贝（不动）
```

---

## 3a. App 镜像格式（简化版）

### 3a.1 格式

去掉自定义 32 字节 header，app 直接是裸 binary。SSBL 从 FileX 读到什么就原样拷到 `LOAD_ADDR`。

```
app.bin（= app.elf → objcopy -O binary 的产物，无任何额外包装）
         │
         ├─ [0x0000] _vector_table[0] = reset 向量（SP_init）
         ├─ [0x0004] _vector_table[1] = undefined_handler
         ├─ ...
         ├─ [0x0020] _boot（Xilinx OKToRun 序列：关 MMU/cache → 设栈 → 开 MMU/cache → 跳 main）
         ├─ [0x????] main() + 业务代码
         └─ [0x????] .bss/.data/.rodata
```

### 3a.2 打包流程

```
arm-none-eabi-gcc -T app.ld app.c ... -o app.elf
arm-none-eabi-objcopy -O binary app.elf app.bin    ← 唯一一步，无 header 包装
cp app.bin <SD 卡 P2>/app.bin                       ← 直接拷到 FAT 分区
```

### 3a.3 SSBL 加载逻辑

```c
/* image_loader.c — 简化后只有 30 行 */
int image_loader_load(const char *path, uint32_t load_addr)
{
    UINT actual;
    ULONG file_size;

    tx_mutex_get(&g_storage_mutex, TX_WAIT_FOREVER);

    if (fx_file_open(&g_fx_media, &g_fx_file, path, FX_OPEN_FOR_READ) != FX_SUCCESS)
        { tx_mutex_put(&g_storage_mutex); return -1; }

    fx_file_size(&g_fx_file, &file_size);

    if (fx_file_read(&g_fx_file, (void *)load_addr, file_size, &actual) != FX_SUCCESS)
        { fx_file_close(&g_fx_file); tx_mutex_put(&g_storage_mutex); return -1; }

    fx_file_close(&g_fx_file);
    tx_mutex_put(&g_storage_mutex);

    elog_i("image_loader: loaded %s (%lu bytes) @ 0x%08lX", path, file_size, load_addr);
    return 0;
}
```

### 3a.4 删除清单

| 文件/模块 | 处置 |
|---|---|
| `image_header.h` | 删除（不再有自定义 header 结构体） |
| `pack_app.py` / `unpack_app.py` | 删除（不再需要 PC 端打包） |
| `crc32.c` | 保留（bitstream loader 侧可能仍需 CRC32，但 app 侧不再依赖） |
| `image_loader.c` 原 60 行 header 校验逻辑 | 替换为上述裸拷逻辑 |

### 3a.5 可靠性说明

SSBL 场景不再校验 app payload CRC，理由分层：

| 层 | 已有保障 | 靠什么 |
|---|---|---|
| 存储介质 | SD 卡硬件 ECC + FileX FAT 校验 | 硬件 + FileX 内置 |
| 加载过程 | `fx_file_read` 返回 `actual == file_size` 确保读完整 | FileX |
| 跳转前 | handoff 不做任何校验，交给 app 自己的启动序列 | app boot.S |

如果后期有安全需求（防篡改），可在 app 自身 `.bss` 前加签名或 hash，那是 app 自己的责任，不在 SSBL 层做。

---

## 4. 线程任务清单

### 4.1 AppTaskStart（优先级 2）

**文件**：`task_start/start.c`

**创建方式**：`tx_application_define` 中自动启动

**生命周期**：常驻（从 SSBL 启动到 handoff 跳转前）

**主流程**：

```
AppTaskStart(ULONG thread_input)
│
├─ fx_system_initialize()          ← FileX 库初始化（一次）
├─ nx_system_initialize()          ← NetX Duo 库初始化（一次）
│
├─ AppObjCreate()                   ← 创建所有 IPC 原语
│   ├─ tx_mutex_create(&g_storage_mutex, "storage_mutex", TX_INHERIT)
│   ├─ tx_mutex_create(&g_printf_mutex, "printf_mutex", TX_NO_INHERIT)
│   └─ tx_event_flags_create(&g_cli_trigger_flags, "cli_trigger_flags")
│
├─ AppTaskCreate()                  ← 创建所有从任务（DONT_START）
│   ├─ tx_thread_create(AppTaskTriggerTCB, "App Trigger", trigger_entry, ...,
│   │                    TX_DONT_START)
│   ├─ tx_thread_create(AppTaskCliTCB, "App Cli", cli_entry, ...,
│   │                    TX_DONT_START)
│   └─ tx_thread_create(AppTaskHttpdTCB, "App Httpd", httpd_entry, ...,
│                        TX_DONT_START)
│
├─ storage_media_open()            ← 读 boot mode → 选 FileX driver → fx_media_open
│   （往 g_fx_media 写入初始化的 FX_MEDIA 实例，后续任务直接用）
│
├─ boot_config_load(&g_runtime_cfg)  ← 从 boot.cfg 读配置
│   （若失败：填充默认值 + 串口告警，不进 CLI——等倒计时阶段决定）
│
├─ /* ========= 倒计时阶段 ========= */
│   boot_delay_ticks = g_runtime_cfg.boot_delay * HZ;
│   while (boot_delay_ticks > 0) {
│       status = tx_event_flags_get(&g_cli_trigger_flags,
│                                    CLI_TRIGGER_FLAG_ANY,
│                                    TX_OR_CLEAR, &flags, 100 ticks);
│       if (status == TX_SUCCESS) {          ← 触发事件到达
│           if (g_runtime_cfg.auto_boot) {
│               tx_thread_resume(AppTaskHttpdTCB);    ← 激活 HTTPd
│           }
│           tx_thread_resume(AppTaskCliTCB);          ← 激活 CLI
│           break;                                     ← 退出倒计时，进休眠
│       }
│       boot_delay_ticks--;
│   }
│
│   if (boot_delay_ticks == 0 && g_runtime_cfg.auto_boot) {
│       /* ========= 超时 → 自动启动 ========= */
│       boot_selector_run(&g_runtime_cfg);  ← 加载 bit + app + jump
│       /* 不返回 */
│   }
│
├─ /* ========= 触发后休眠（CLI/HTTPd 在跑） ========= */
│   while (1) {
│       tx_thread_sleep(1);
│   }
```

**关键设计点**：
- `fx_system_initialize` / `nx_system_initialize` 是库级初始化，只需一次，在 AppTaskStart 做
- **具体介质挂载（`fx_media_open`）也在 AppTaskStart 完成**，CLI/HTTPd 直接使用 `g_fx_media`
- `boot_config_load` 在 AppTaskStart 做——失败时不影响 trigger 机制，用户仍可进 CLI 修复
- 倒计时以 100 tick（~1秒）为步长检查 event flags，兼顾 tick 精度和响应速度
- `auto_boot=no` 时：倒计时循环直接 break，`boot_delay_ticks > 0` 但主动退出→进休眠，CLI/HTTPd 由 trigger 激活
- CLI 和 HTTPd 都用 `tx_thread_resume` 激活（因为 DONT_START 创建后处于 suspended 状态）

### 4.2 AppTaskTrigger（优先级 11）

**文件**：`task_trigger/trigger.c`

**创建**：`TX_DONT_START`，AppTaskStart 在倒计时开始时 `tx_thread_resume`

**生命周期**：启动后检测到触发条件 → 设置 event flag → 进入 `while(1) tx_thread_sleep` 休眠

**主流程**：

```
trigger_entry(ULONG thread_input)
│
├─ /* 双路触发检测循环 */
│   while (1) {
│       Events = TX_NULL;
│       /* GPIO 按键检测 */
│       if (key_driver_read(KEY0) == KEY_PRESSED) {
│           Events |= CLI_TRIGGER_FLAG_GPIO;
│           while (key_driver_read(KEY0) == KEY_PRESSED) tx_thread_sleep(1);
│       }
│       /* UART 魔数检测 */
│       if (uart_magic_detected()) {
│           Events |= CLI_TRIGGER_FLAG_UART;
│       }
│       if (Events) {
│           tx_event_flags_set(&g_cli_trigger_flags, Events, TX_OR);
│           break;
│       }
│       tx_thread_sleep(1);          ← 10ms 周期
│   }
│
│   while (1) {                      ← 触发后休眠（不自杀，便于 reset 恢复）
│       tx_thread_sleep(1000);
│   }
```

**关键设计点**：
- 不再像旧代码中 `tx_thread_terminate` 自杀，改为休眠（避免 `terminate` 后 TCB 状态异常）
- 10ms 周期轮询，消抖在按键读逻辑内处理
- 魔数检测用 UART 环形缓冲区的滑窗匹配（`cli_uart.c` 已有）

### 4.3 AppTaskCli（优先级 10）

**文件**：`task_cli/cli_shell.c`

**创建**：`TX_DONT_START`，AppTaskStart 在收到 trigger 后 `tx_thread_resume`

**生命周期**：resume 后一直运行，直到 `boot` 命令触发 `jump_to_app` 跳转（此时整个系统消亡）

**主流程**：

```
cli_entry(ULONG thread_input)
│
├─ shell_port_init()                ← 对接 Letter shell：register uart putc/getc
├─ shell_register_commands(cmd_table);  ← 注册命令表
├─ shell_init()                     ← Letter shell 初始化
│
└─ shellTask()                      ← Letter shell 主循环（阻塞读 + 命令分发）
```

**命令表**：

| 命令 | 处理函数 | 文件 | 说明 |
|---|---|---|---|
| `ls [path]` | `cmd_ls()` | `cli_cmd_fs.c` | 列出文件目录 |
| `cat <file>` | `cmd_cat()` | `cli_cmd_fs.c` | 查看文件内容 |
| `rm <file>` | `cmd_rm()` | `cli_cmd_fs.c` | 删除文件 |
| `mv <old> <new>` | `cmd_mv()` | `cli_cmd_fs.c` | 重命名文件 |
| `cfg show` | `cmd_cfg_show()` | `cli_cmd_cfg.c` | 显示 boot.cfg 内容 |
| `cfg set <k>=<v>` | `cmd_cfg_set()` | `cli_cmd_cfg.c` | 修改配置项 |
| `cfg save` | `cmd_cfg_save()` | `cli_cmd_cfg.c` | 保存配置到文件 |
| `boot [app] [bit]` | `cmd_boot()` | `cli_cmd_boot.c` | 立即启动 |
| `reset` | `cmd_reset()` | `cli_cmd_boot.c` | 软复位 |
| `http status` | `cmd_http_status()` | `cli_cmd_http.c` | 查看 HTTP 服务状态 |
| `http restart` | `cmd_http_restart()` | `cli_cmd_http.c` | 重启 HTTP 服务 |
| `mem <addr> [n]` | `cmd_mem()` | `cli_cmd_sys.c` | 内存显示 |
| `info` | `cmd_info()` | `cli_cmd_sys.c` | 系统信息 |
| `help` | Letter shell 内置 | — | 命令列表 |

**FileX 访问规约**：

```c
/* 所有文件操作命令，访问 FileX 前必须加锁 */
static void cmd_ls(int argc, char **argv)
{
    tx_mutex_get(&g_storage_mutex, TX_WAIT_FOREVER);
    /* fx_directory_first_full_entry_find / fx_directory_next_full_entry_find */
    tx_mutex_put(&g_storage_mutex);
}
```

### 4.4 AppTaskHttpd（优先级 8）

**文件**：`task_httpd/httpd_server.c`

**创建**：`TX_DONT_START`，AppTaskStart 在收到 trigger 后 `tx_thread_resume`

**生命周期**：resume 后一直运行，直到系统 handoff

**主流程**：

```
httpd_entry(ULONG thread_input)
│
├─ nx_ip_create(&g_ip, "SSBL IP",
│               IP_ADDRESS(192,168,1,100),
│               SUBNET_MASK(255,255,255,0),
│               &g_pool, nx_emac_driver,
│               NULL, NULL, /* 栈指针 */, 2048);
├─ nx_arp_enable(&g_ip, NULL, 0);           ← ARP 使能
├─ nx_tcp_enable(&g_ip);                    ← TCP 使能
│
├─ httpd_route_init();                      ← 注册 URL→handler 路由表
│
├─ /* HTTP 服务器主循环 */
│   while (1) {
│       nx_tcp_server_socket_listen(...);
│       nx_tcp_server_socket_accept(...);
│       nx_tcp_socket_receive(...)           ← 接收 HTTP 请求
│       httpd_route_dispatch(req);           ← 路由分发
│       nx_tcp_socket_send(...)              ← 返回 HTTP 响应
│       nx_tcp_server_socket_unaccept(...);
│       nx_tcp_server_socket_relisten(...);
│   }
```

**URL 路由表**：

| 方法 | URL | 处理函数 | 说明 |
|---|---|---|---|
| GET | `/` | `httpd_page_files()` | 文件列表 HTML 页面 |
| GET | `/boot.cfg` | `httpd_page_cfg_get()` | 显示 boot.cfg 内容 |
| POST | `/boot.cfg` | `httpd_page_cfg_post()` | 保存 boot.cfg |
| POST | `/upload` | `httpd_upload()` | multipart 文件上传 |
| POST | `/delete` | `httpd_delete()` | 删除文件 |
| POST | `/boot` | `httpd_boot()` | 触发启动 |

**FileX 访问规约**（同 CLI）：

```c
/* 每个 URL handler 在访问 FileX 前加锁 */
static void httpd_page_files(NX_TCP_SOCKET *sock, HTTP_REQUEST *req)
{
    tx_mutex_get(&g_storage_mutex, TX_WAIT_FOREVER);
    /* fx_directory_first_full_entry_find / fx_directory_next_full_entry_find */
    tx_mutex_put(&g_storage_mutex);
    /* 拼接 HTML → nx_tcp_socket_send */
}
```

**Web 管理页面外观**（文字描述，非 UI 设计阶段）：
- 单页 HTML，标题栏显示 "SSBL Web Manager"
- 主体是文件列表（带文件大小、日期、操链接）
- 每个文件行提供 "删除" 按钮
- boot.cfg 提供 "查看"链接→编辑页面（textarea 直接编辑内容）
- 底部有文件上传表单
- 醒目的 "Boot Now" 按钮

---

## 5. IPC 通信矩阵

### 5.1 原语清单

| 原语 | 类型 | 创建者 | 用途 |
|---|---|---|---|
| `g_cli_trigger_flags` | `TX_EVENT_FLAGS_GROUP` | AppObjCreate | Trigger → Start 事件通知 |
| `g_storage_mutex` | `TX_MUTEX (INHERIT)` | AppObjCreate | CLI/HTTPd 串行化 FileX 访问 |
| `g_printf_mutex` | `TX_MUTEX (NO_INHERIT)` | AppObjCreate | 多线程 xil_printf 串行化 |

### 5.2 交互流程

```
                    event_flags_set
  AppTaskTrigger ──────────────────→ AppTaskStart
  (GPIO/UART 触发)                    (倒计时循环中 tx_event_flags_get)

                                          tx_thread_resume
                                     ┌──────────────────→ AppTaskCli
                                     │  (激活 CLI shell)
  AppTaskStart                       │
  (收到 trigger) ───────────────────┤
                                     │
                                     └──────────────────→ AppTaskHttpd
                                        (激活 HTTP 服务器)

  AppTaskCli / AppTaskHttpd               g_storage_mutex
  (文件操作)          ←────────────────── (互斥访问 FileX)
```

### 5.3 事件标志定义

```c
/* task_trigger/trigger.h */
#define CLI_TRIGGER_FLAG_GPIO     0x01u   /* Bit 0: GPIO 按键触发 */
#define CLI_TRIGGER_FLAG_UART     0x02u   /* Bit 1: UART 魔数触发 */
#define CLI_TRIGGER_FLAG_ANY      0x03u   /* Bit 0|1: 任意触发 */
```

### 5.4 实现定约

- `g_storage_mutex` 使用 `TX_INHERIT` 防优先级反转（HTTPd prio 8 > CLI prio 10，若 CLI 持锁时 HTTPd 等锁，优先级会提升到 CLI 防止低优先级长占）
- `g_printf_mutex` 使用 `TX_NO_INHERIT`（printf 操作极短，无需继承）
- 所有 FileX 操作必须遵循：`get mutex → 操作 → put mutex`，不允许持锁跨越多次用户交互（如 CLI 的 `cat` 要读完整文件，先在锁内读完放到临时缓冲，再释放锁输出）

---

## 6. 优先级总表

| 优先级 | 任务 | TCB 变量 | 栈大小 | 启动方式 | 说明 |
|---|---|---|---|---|---|
| 0 | Timer Thread | (ThreadX 内部) | 1024 | AUTO_START | 系统定时器 |
| **2** | **AppTaskStart** | `AppTaskStartTCB` | 4096 | **AUTO_START** | 初始化 + 倒计时 + 启动决策 |
| **8** | **AppTaskHttpd** | `AppTaskHttpdTCB` | 8192 | **DONT_START** | HTTP 服务器 |
| **10** | **AppTaskCli** | `AppTaskCliTCB` | 8192 | **DONT_START** | CLI shell |
| **11** | **AppTaskTrigger** | `AppTaskTriggerTCB` | 2048 | **DONT_START** | GPIO/UART 触发检测 |

---

## 7. 启动时序图

```
Time ──────────────────────────────────────────────────────────────►
        main()          tx_kernel_enter()     AppTaskStart (prio 2)
          │                   │                     │
          ├─ bsp_init()       │                     │
          ├─ board_init()     │                     ├─ fx_system_initialize()
          └─ tx_kernel_enter()│                     ├─ nx_system_initialize()
                               │                    ├─ AppObjCreate()
                               │                    ├─ AppTaskCreate()
                               │                    ├─ storage_media_open()
                               │                    ├─ boot_config_load()
                               │                    │
                               │                    ├─ tx_thread_resume(trigger)
                               │                    │
                               │              ┌─────┴───── trigger (prio 11)
                               │              │       轮询 GPIO/UART
                               │              │
                               │              ├─ 倒计时循环 (每1秒)
                               │              │   ├─ tx_event_flags_get(100tick)
                               │              │   └─ sleep(100)
                               │              │
          ┌─── 触发事件 ────────┼──────────────┘
          │                     │              │
          │                     │              ├─ tx_thread_resume(cli)
          │                     │              ├─ tx_thread_resume(httpd)
          │                     │              └─ while(1) sleep(1)
          │                     │
          │               ┌─────┴───── CLI (prio 10)
          │               │     shellTask() 循环
          │               │     交互: ls/cat/cfg/boot/http
          │               │
          │               └─────┴───── HTTPd (prio 8)
          │                         HTTP 主循环
          │                         文件管理 / boot.cfg 编辑 / 上传
          │
          └─────── CLI "boot" 或 HTTP POST /boot ────────┘
                                                         │
                                                    boot_selector_run()
                                                         │
                                                    jump_to_app()
                                                         │
                                                    handoff_exit.S
                                                         │
                                                    运行 app
```

---

## 8. 关键常量（`ssbl_config.h`）

```c
/* ── 线程优先级 ── */
#define APP_CFG_TASK_START_PRIO       2u
#define APP_CFG_TASK_HTTPD_PRIO       8u
#define APP_CFG_TASK_CLI_PRIO        10u
#define APP_CFG_TASK_TRIGGER_PRIO    11u

/* ── 线程栈大小 ── */
#define APP_CFG_TASK_START_STK_SIZE   4096u
#define APP_CFG_TASK_HTTPD_STK_SIZE   8192u
#define APP_CFG_TASK_CLI_STK_SIZE     8192u
#define APP_CFG_TASK_TRIGGER_STK_SIZE 2048u

/* ── 默认网络配置 ── */
#define HTTPD_DEFAULT_IP              IP_ADDRESS(192,168,1,100)
#define HTTPD_DEFAULT_NETMASK         IP_ADDRESS(255,255,255,0)
#define HTTPD_DEFAULT_PORT            80

/* ── 倒计时 ──*/
#define COUNTDOWN_TICK_STEP           100u     /* 每次检查 event flag 的 tick 数 */
#define TICKS_PER_SECOND              100u     /* ThreadX tick 频率（10ms） */

/* ── 触发事件标志 ── */
#define CLI_TRIGGER_FLAG_GPIO         0x01u
#define CLI_TRIGGER_FLAG_UART         0x02u
#define CLI_TRIGGER_FLAG_ANY          (CLI_TRIGGER_FLAG_GPIO | CLI_TRIGGER_FLAG_UART)
```

---

## 9. main.c 样板

```c
/* ── includes ── */
#include "includes.h"

/* ── ThreadX 控制块 + 栈（所有 OS 资源集中）── */
static TX_THREAD      AppTaskStartTCB;
static uint64_t       AppTaskStartStk[APP_CFG_TASK_START_STK_SIZE / 8];

/* ── 全局 IPC 原语 ── */
TX_MUTEX              g_storage_mutex;
TX_MUTEX              g_printf_mutex;
TX_EVENT_FLAGS_GROUP  g_cli_trigger_flags;

/* ── 全局运行时配置 ── */
boot_cfg_t            g_runtime_cfg;

/* ── 全局 FileX 介质 ── */
FX_MEDIA              g_fx_media;

/* ── 全局 NetX IP 实例 ── */
NX_IP                 g_ip;
NX_PACKET_POOL        g_ip_pool;

/* ── 任务 TCB/栈（extern 供 handoff 线程终止用）── */
TX_THREAD             AppTaskTriggerTCB;
TX_THREAD             AppTaskCliTCB;
TX_THREAD             AppTaskHttpdTCB;
uint64_t              AppTaskTriggerStk[APP_CFG_TASK_TRIGGER_STK_SIZE / 8];
uint64_t              AppTaskCliStk[APP_CFG_TASK_CLI_STK_SIZE / 8];
uint64_t              AppTaskHttpdStk[APP_CFG_TASK_HTTPD_STK_SIZE / 8];

/* ── 函数声明 ── */
static void  AppTaskStart   (ULONG thread_input);
static void  AppTaskCreate  (void);
static void  AppObjCreate   (void);
extern void  trigger_entry   (ULONG thread_input);
extern void  cli_entry       (ULONG thread_input);
extern void  httpd_entry     (ULONG thread_input);

int main(void)
{
    bsp_init();              /* GIC, 定时器 */
    board_init();            /* GPIO, UART, SD, LED, KEY, EMAC */
    tx_kernel_enter();
    /* 不返回 */
    return 0;
}

void tx_application_define(void *first_unused_memory)
{
    tx_thread_create(&AppTaskStartTCB, "App Task Start",
                     AppTaskStart, 0,
                     &AppTaskStartStk[0], sizeof(AppTaskStartStk),
                     APP_CFG_TASK_START_PRIO, APP_CFG_TASK_START_PRIO,
                     TX_NO_TIME_SLICE, TX_AUTO_START);
}

static void AppTaskStart(ULONG thread_input)
{
    fx_system_initialize();
    nx_system_initialize();

    AppObjCreate();
    AppTaskCreate();

    storage_media_open();
    boot_config_load(&g_runtime_cfg);

    /* 激活 trigger */
    tx_thread_resume(&AppTaskTriggerTCB);

    /* 倒计时循环 */
    if (g_runtime_cfg.auto_boot) {
        ULONG ticks = g_runtime_cfg.boot_delay * TICKS_PER_SECOND;
        while (ticks > 0) {
            ULONG flags;
            if (tx_event_flags_get(&g_cli_trigger_flags,
                                   CLI_TRIGGER_FLAG_ANY,
                                   TX_OR_CLEAR, &flags,
                                   COUNTDOWN_TICK_STEP) == TX_SUCCESS) {
                /* trigger fired */
                tx_thread_resume(&AppTaskCliTCB);
                tx_thread_resume(&AppTaskHttpdTCB);
                break;
            }
            ticks -= COUNTDOWN_TICK_STEP;
        }
        if (ticks == 0) {
            /* 超时 → 自动启动 */
            boot_selector_run(&g_runtime_cfg);
            /* 不返回 */
        }
    } else {
        /* auto_boot=no: 直接永久等待 trigger */
        ULONG flags;
        tx_event_flags_get(&g_cli_trigger_flags,
                           CLI_TRIGGER_FLAG_ANY,
                           TX_OR_CLEAR, &flags,
                           TX_WAIT_FOREVER);
        tx_thread_resume(&AppTaskCliTCB);
        tx_thread_resume(&AppTaskHttpdTCB);
    }

    /* 触发后进入管理状态（CLI/HTTPd 在跑） */
    while (1) {
        tx_thread_sleep(1);
    }
}

static void AppTaskCreate(void)
{
    tx_thread_create(&AppTaskTriggerTCB, "App Task Trigger",
                     trigger_entry, 0,
                     &AppTaskTriggerStk[0], sizeof(AppTaskTriggerStk),
                     APP_CFG_TASK_TRIGGER_PRIO, APP_CFG_TASK_TRIGGER_PRIO,
                     TX_NO_TIME_SLICE, TX_DONT_START);

    tx_thread_create(&AppTaskCliTCB, "App Task Cli",
                     cli_entry, 0,
                     &AppTaskCliStk[0], sizeof(AppTaskCliStk),
                     APP_CFG_TASK_CLI_PRIO, APP_CFG_TASK_CLI_PRIO,
                     TX_NO_TIME_SLICE, TX_DONT_START);

    tx_thread_create(&AppTaskHttpdTCB, "App Task Httpd",
                     httpd_entry, 0,
                     &AppTaskHttpdStk[0], sizeof(AppTaskHttpdStk),
                     APP_CFG_TASK_HTTPD_PRIO, APP_CFG_TASK_HTTPD_PRIO,
                     TX_NO_TIME_SLICE, TX_DONT_START);
}

static void AppObjCreate(void)
{
    tx_mutex_create(&g_storage_mutex, "storage_mutex", TX_INHERIT);
    tx_mutex_create(&g_printf_mutex,  "printf_mutex",  TX_NO_INHERIT);
    tx_event_flags_create(&g_cli_trigger_flags, "cli_trigger_flags");
}
```

---

## 10. 错误处理

| 场景 | 处理 |
|---|---|
| `storage_media_open` 失败 | 打印错误，goto 倒计时（不进 boot，等用户进 CLI 修复）+ LED 错误码闪烁 |
| `boot_config_load` 失败 | 填充默认值 + 警告，继续倒计时/触发流程 |
| CLI 文件操作失败 | 命令返回错误信息，shell 不退出 |
| HTTPd NetX 初始化失败 | 打印错误，HTTPd 线程打印错误后进入休眠（不影响 CLI） |
| `boot_selector_run` 加载失败 | 进 CLI（不走 handoff），打印详细错误 |

**容错原则**：任何不影响 SSBL 继续运行的错误都不应导致死循环——只要 CLI/HTTPd 还能用，用户就有修复机会。

---

## 11. handoff 前的线程清理

```c
/* handoff.c - jump_to_app() */
void jump_to_app(uint32_t app_addr)
{
    /* 1. 暂停所有其他线程 */
    tx_thread_suspend(&AppTaskTriggerTCB);
    tx_thread_suspend(&AppTaskHttpdTCB);
    /* CLI 线程正在执行此函数，不需 suspend 自己 */

    /* 2. 释放 IPC 资源（非必须，但干净） */
    tx_mutex_delete(&g_storage_mutex);
    tx_mutex_delete(&g_printf_mutex);
    tx_event_flags_delete(&g_cli_trigger_flags);

    /* 3. 停止外设 */
    /* private timer stop */
    XScuTimer_Stop(&g_private_timer);

    /* 4. 关 GIC */
    XScuGic_Stop(&g_gic);

    /* 5. 汇编级 cache/MMU 清理 + 跳转 */
    handoff_exit(app_addr);
    /* 不返回 */
}
```

---

## 12. 迁移路径（从旧代码到新架构）

| 步骤 | 内容 | 涉及模块 |
|---|---|---|
| 1 | 创建 `task_start/start.c`，从 `main.c` 迁入 `AppTaskStart` | main.c → task_start/ |
| 2 | 创建 `task_trigger/trigger.c`，从 `cli/cli_trigger.c` 迁入 | cli/ → task_trigger/ |
| 3 | 创建 `task_cli/`，从 `cli/` 迁入，接入 Letter shell | cli/ → task_cli/ |
| 4 | 创建 `task_httpd/`，新写 HTTP 服务器 | 新模块 |
| 5 | 删除旧 `countdown.c`（逻辑并入 AppTaskStart） | 删除 |
| 6 | 全局变量的迁移（g_fx_media, g_runtime_cfg 等保持接口兼容） | 所有模块 |

---

## 13. 遗留问题（本文档不涵盖）

- HTTP 页面具体 UI 设计（CSS 框架 / 响应式）
- HTTP 安全性（CSRF / 会话管理 — 内网 bootloader 暂不需要）
- NetX Duo EMAC 驱动的移植细节（现有 `nx_stm32_*` 驱动需改写为 Zynq 版）
- DHCP 支持（当前静态 IP，后期可按需）
