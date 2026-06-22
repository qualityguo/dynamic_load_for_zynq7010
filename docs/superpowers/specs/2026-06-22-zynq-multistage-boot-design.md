# Zynq-7000 多级启动代码 — 设计文档

| 项 | 值 |
|---|---|
| 项目 | 基于 Zynq-7000 的多级启动代码 |
| 日期 | 2026-06-22 |
| 目标芯片 | Zynq-7010（Cortex-A9 单核 + Artix-7 PL） |
| 基础资产 | 用户现有 `threadx_for_zynq7010` 工程（ThreadX 6.4.1 + FileX 6.4.1 + 设备驱动框架已移植完成） |
| 项目位置 | `E:\桌面\docs\Code`（独立项目 + vendor 拷贝现有移植） |

---

## 1. 项目目标与启动链

### 1.1 目标

做一个"启动选择器"SSBL（Second Stage Boot Loader），根据 SD 卡（后期 QSPI Flash）上的文本配置文件 `boot.cfg`，动态选择加载哪个 PL bitstream 和哪个应用。提供串口 CLI 做 YMODEM 现场升级与镜像管理。

### 1.2 启动链

```
[Stage 0] BootROM（片上 ROM，不可改）
          采样 boot mode pins → SD/QSPI → 加载 FSBL 到 OCM → 跳转
   ↓
[Stage 1] 精简 FSBL（OCM 0x0，~192KB）
          ps7_init（PLL/MIO/DDR）→ 加载唯一 CPU partition（SSBL）到 DDR
          → FsblHandoffExit → 跳转 SSBL 入口
   ↓
[Stage 2] 自定义 SSBL（DDR 0x100000，ThreadX + FileX）
          读 boot.cfg → 加载 app.bin + .bit → 破坏性清理 → 跳转
   ↓
[Stage 3] 主应用（DDR 0x1000000，自带 boot.S 的 ThreadX/裸机实例）
```

### 1.3 关键架构决策

| 决策 | 理由 |
|---|---|
| FSBL **不**加载 bitstream | 让 SSBL 能动态选 bit（"启动选择器"的核心承诺） |
| SSBL 跑 DDR 而非 OCM | ThreadX + FileX + BSP + GIC glue > 100KB，OCM 256KB 容不下 |
| app 固定加载到 `0x01000000` | 简化跳转逻辑，应用工程模板默认值 |
| 跳转纯破坏性清理 + app 自带 boot.S | "接近 reset"语义，app 完全独立（裸机/ThreadX/其他 RTOS 都能跳） |

---

## 2. DDR 内存布局

```
0x00000000 ┌───────────────────────┐
           │ FSBL (OCM 区，~192KB) │  ← Stage 1
0x00040000 ├───────────────────────┤
           │ (OCM 保留/缓冲)       │
0xFFFFFFFF └───────────────────────┘

0x00100000 ┌───────────────────────┐  ← Stage 2 起点
           │ SSBL 代码+数据+栈     │  1MB（ssbl.ld LENGTH=0x100000）
0x00200000 ├───────────────────────┤
           │ SSBL FileX 缓冲/堆    │  512KB
0x00280000 ├───────────────────────┤
           │ (预留：PL DMA /        │
           │  framebuffer / 未来    │  ~214MB，未分配
           │  Linux 等)             │
0x01000000 ┌───────────────────────┐  ← Stage 3 起点
           │ 主应用加载区          │
           │  (从 .bin 拷入)       │  ≤7MB（留余量给暂存区）
0x01700000 ├───────────────────────┤
           │ 应用堆/栈             │  ~9MB
0x02000000 ┌───────────────────────┐  ← YMODEM 暂存区（CLI 模式独占）
           │ 镜像暂存区            │  10MB (STAGING_AREA_SIZE)
           │  (升级用，校验+确认   │  （含 YMODEM 分块对齐开销，
           │   后才提交到存储介质) │   容纳上限 7MB 的 app 镜像）
0x02A00000 ├───────────────────────┤
           │ ...                   │
           └───────────────────────┘
```

---

## 3. 目录结构

### 3.1 顶层目录

```
Code/
├── README.md                       项目总览、构建步骤
├── docs/
│   └── superpowers/specs/
│       └── 2026-06-22-zynq-multistage-boot-design.md   本文档
│
├── boot/
│   ├── fsbl/                       Stage 1: Vitis 创建的标准 FSBL（OCM 0x0）
│   ├── ssbl/                       Stage 2: 自定义 SSBL（DDR 0x100000）
│   └── app_template/               Stage 3: 主应用模板（DDR 0x1000000）
│
├── external/                       第三方源码（vendor 或 submodule）
│   ├── threadx/                    Eclipse ThreadX（含 cortex-a9 port）
│   ├── filex/                      Eclipse FileX
│   └── levelx/                     Eclipse LevelX（后期 QSPI 才用）
│
├── bif/
│   ├── boot.bif                    bootgen 镜像描述（fsbl + ssbl）
│   └── README.md
│
└── scripts/
    ├── pack_app.py                 app.raw → app.bin（加 32 字节 header）
    ├── unpack_app.py               app.bin → app.raw（CI 校验/调试）
    ├── build_boot_bin.sh           bootgen 一键打包 BOOT.bin
    └── make_sd_card.sh             制作可启动 SD 卡
```

### 3.2 `boot/ssbl/` 内部结构（项目核心）

```
boot/ssbl/
├── ssbl.ld                         [新] 基于 hello_threadx/lscript.ld, ORIGIN=0x100000
├── README.md                       [新] 移植修正点、构建步骤、踩坑笔记
│
├── src/                            SSBL 主逻辑
│   ├── main.c                      [新] board_init + tx_kernel_enter
│   ├── tx_application_define.c     [新] 创建 countdown/trigger/[cli/boot] 线程
│   └── boot_selector.c             [新] 读 cfg → 加载 → handoff 主流程
│
├── loader/                         镜像加载与校验
│   ├── image_loader.c              [新] FileX 读 app.bin + header 校验 + 拷到 DDR
│   ├── bitstream_loader.c          [新] FileX 读 .bit + XDcfg_Transfer 到 PL
│   └── crc32.c                     [新] 轻量 CRC32
│
├── storage/                        介质抽象层（关键）
│   ├── storage.h                   [新] storage_ops_t 接口 + storage_* 宏
│   ├── storage_fx_glue.c           [新] FileX I/O driver 通用回调
│   ├── sd_port.c                   [改] 基于 fx_zynq_sdio_driver.c + storage 接口
│   └── qspi_port.c                 [后期] FileX + LevelX NOR
│
├── config/                         boot.cfg 解析
│   ├── boot_config.c               [新] INI 风格解析 → 结构体
│   └── boot_config.h
│
├── cli/                            CLI 子系统
│   ├── cli.c                       [新] 命令分发器（readline + argv 解析）
│   ├── cli_commands.c              [新] 所有命令实现
│   ├── cli_uart.c                  [新] UART 中断接收 + 环形缓冲
│   ├── cli_ymodem.c                [新] YMODEM 接收状态机（流式写入 DDR 暂存区）
│   ├── cli_staging.c               [新] DDR 暂存区管理 + 两阶段提交（校验+确认+原子写）
│   ├── cli_trigger.c               [新] GPIO 按键 + UART 魔数检测
│   ├── readline.c                  [新] 行编辑（退格/方向键/历史）
│   └── include/
│       ├── cli.h
│       └── cli_command_table.h     [新] 命令注册宏表
│
├── handoff/                        SSBL → App 跳转
│   ├── handoff.c                   [新] C 接口：jump_to_app(addr)
│   └── handoff_exit.S              [新] 汇编：纯破坏性清理 + 跳转
│
├── include/
│   ├── ssbl_config.h               [新] 编译期常量（地址/缓冲/tick/log level）
│   ├── ssbl_error.h                [新] 错误码定义
│   ├── ssbl_log.h                  [新] 日志宏（基于 xil_printf + tx_time_get）
│   └── image_header.h              [新] 镜像 header 结构体
│
├── ThreadX/                        [vendor 拷贝] 整目录复用现有移植
├── FileX/                          [vendor 拷贝] 整目录复用现有移植
├── BSP/                            [vendor 拷贝] LED/KEY/UART 驱动
└── utils/                          [vendor 拷贝] 打印/环形缓冲等
```

### 3.3 模块依赖（单向，避免环）

```
main.c
  └→ tx_application_define
       ├→ boot_selector ──→ boot_config (解析 cfg)
       │                 └→ storage (FileX 抽象)
       │                      └→ sd_port / qspi_port
       │                 └→ image_loader ─→ crc32
       │                 └→ bitstream_loader
       │
       └→ handoff ──→ handoff_exit.S
```

---

## 4. 镜像格式

### 4.1 app 镜像 header（32 字节定长，双 CRC）

```
偏移    长度    字段名          说明
─────────────────────────────────────────────────────────
0x00    4       magic           固定 0x5A424F4F（little-endian 字节序读作 "OOBZ"）
0x04    4       version         header 版本号（当前 = 1）
0x08    4       image_type      1=ThreadX app, 2=bare-metal, 3=reserved
0x0C    4       load_addr       加载地址（应 = 0x01000000）
0x10    4       image_size      payload 字节数（不含 header）
0x14    4       crc32           payload 的 CRC32
0x18    4       header_crc32    header[0x00..0x17] 的 CRC32
0x20    N       payload         实际镜像数据（payload[0] = _vector_table）
```

**设计要点**：
- Header 总长 32 字节，对齐良好，C struct 直接映射
- 双 CRC：先校验 header_crc32（header 本身没坏），再校验 crc32（payload 完整）
- `image_type` 留扩展空间，当前仅用 type=1（ThreadX app）
- **入口地址**固定 = `load_addr`（payload 第 0 字节就是 app 的 reset 向量 `_vector_table[0]`；header 在 SSBL 搬运时已被剥离、不进 DDR，故无需 `+0x20`）
- Header 由 PC 端 `pack_app.py` 打包，**不在嵌入式端生成**
- `.bit` 文件**不加**自定义 header（Xilinx 原生格式由 devcfg 直接消费）

### 4.2 bitstream 文件

`.bit` 文件保持 Xilinx 原生格式（2 字节长度前缀 + 字段），SSBL 不解析其内容，只负责搬到 DDR 触发 PCAP。

---

## 5. boot.cfg 文本格式

### 5.1 格式（INI 风格，人艰可读 PC 可编辑）

```ini
# Zynq SSBL 启动配置
# 编辑后保存即可，SSBL 下次启动自动读取

# === 启动镜像选择 ===
app = app_current.bin           # 要加载的应用文件名
bit = pl_current.bit            # 要下载的 PL bitstream 文件名
                                # bit = (空) 表示不下载 bitstream

# === 启动行为 ===
boot_delay = 3                  # CLI 触发窗口（秒）；auto_boot=no 时此字段被忽略
auto_boot = yes                 # yes=超时启动 no=永久 CLI（忽略 boot_delay，不创建 countdown_thread）

# === 保留字段（后期 A/B 升级用）===
# last_good_app = app_a.bin
# last_good_bit = pl_a.bit
# boot_attempts = 0
```

### 5.2 解析规则

- 行首/行尾空白忽略；`=` 两侧空白忽略；行内 `#` 之后视为注释
- 大小写敏感（key 全小写）
- 未识别的 key 忽略（前向兼容）
- 必填字段：`app`、`boot_delay`、`auto_boot`
- `bit` 留空表示跳过 PL 配置（适合无 PL 的纯 PS 应用场景）
- **`bit` 空值的合法写法**（解析器四种等价处理，统一识别为"不下载 bitstream"）：
  1. `bit =`（等号后为空字符串）
  2. `bit = ""`（显式空串）
  3. `bit = none`（关键字 `none`，大小写敏感必须小写）
  4. 整行注释掉或省略 `bit` 键
  - boot_config 解析后统一映射为内部值 `bit_path == NULL`
- **时间单位**：`boot_delay` 单位为秒；SSBL 内部按 ThreadX tick = 10ms 换算（`boot_delay=3` → `tx_thread_sleep(300)`）

### 5.3 容错与默认行为

| 场景 | SSBL 行为 |
|---|--- |
| boot.cfg 不存在 | 走默认配置（app=default.bin, boot_delay=3）+ 串口告警 |
| boot.cfg 解析失败 | 同上 |
| `app` 指定文件不存在 | 进 CLI（不 boot）+ 报错 |
| `app` 文件 header magic 错 | 进 CLI + 报错 |
| `app` payload CRC 错 | 进 CLI + 报错 |
| `bit` 文件不存在但 `app` OK | 告警但继续启动（PL 可能用旧 bit） |
| auto_boot=no | boot_delay 字段被忽略，countdown_thread 不创建，上电直接进 CLI |
| auto_boot=no 且超时 | 同上（auto_boot=no 时不读 boot_delay） |
| CLI 中 `boot` 但镜像坏 | 回到 CLI 提示符 |

**设计原则**：**任何启动决策歧义都退回 CLI，而不是硬启动一个坏镜像**——只要 SSBL 还活着，串口总是一个逃生通道。

### 5.4 SD 卡目录约定

```
SD 卡根目录 (FAT32):
├── BOOT.bin                    ← fsbl + ssbl（bootgen 打包）
├── boot.cfg                    ← 启动配置
├── boot.log                    ← 启动日志（环形 4KB）
├── app_current.bin             ← 当前应用（含自定义 header）
├── app_a.bin                   ← A 槽（备份/升级目标）
├── app_b.bin                   ← B 槽（备份）
├── pl_current.bit              ← 当前 PL 设计
├── pl_a.bit
├── pl_b.bit
└── *.tmp                       ← 提交瞬间的临时文件（仅 YMODEM 提交时短暂存在）
```

**注意**：升级时 YMODEM 接收的数据**不落 SD 卡临时区**，而是暂存在 DDR 高地址（`0x02000000`），校验+用户确认后才提交到 SD（走 `.tmp + rename` 原子写）。所以 SD 卡上正常情况下看不到 `.tmp` 文件，只有在"提交瞬间断电"的极端场景才会留下半写的 `.tmp`（下次启动 SSBL 可识别并清理）。

文件名即 manifest——文件系统本身就是最好的镜像清单，无需额外的清单文件。后期切 QSPI 时目录结构保持不变，只是底层 `storage` 从 `sd_port` 换成 `qspi_port`（FileX + LevelX NOR）。

---

## 6. CLI 子系统

### 6.1 触发与启动时序

```
SSBL tx_kernel_enter
  ↓
tx_application_define 创建：
  ├─ countdown_thread          (优先级高, 周期短)
  ├─ trigger_monitor_thread    (检测 GPIO + UART 魔数)
  └─ [条件成立才创建] cli_thread
  ↓
boot_delay 窗口内（默认 3s, boot.cfg 可配）
  ├─ GPIO 按键按下  ─┐
  ├─ UART 收到 0xDEADBEEF ─┤→ 任一触发 → 创建 cli_thread，跳过 app 加载
  └─ 超时无触发     ─┘
                          ↘ 超时 → 进 boot_selector 流程
```

### 6.2 命令集

| 分类 | 命令 | 作用 |
|---|---|---|
| 状态查看 | `help` | 列出所有命令 |
| | `info` | 显示 SSBL 版本、当前介质、内存布局 |
| | `status` | 上次 boot 失败原因（读 boot.log） |
| | `ls [dir]` | 列 SD/QSPI 目录（FileX） |
| | `cat <file>` | 查看文本文件（如 boot.cfg） |
| 镜像管理 | `ymodem rx <file>` | 接收 YMODEM 到 DDR 暂存区，校验+确认后提交到文件 |
| | `rm <file>` | 删除文件 |
| | `mv <old> <new>` | 重命名 |
| 配置 | `cfg show` | 显示 boot.cfg 解析后的当前选择 |
| | `cfg set <key>=<val>` | 修改字段（app/bit/boot_delay/auto_boot） |
| | `cfg save` | 把改动写回 boot.cfg |
| 启动控制 | `boot` | 立即按当前 boot.cfg 启动 |
| | `boot <app> [bit]` | 一次性覆盖启动（不改 cfg） |
| 调试 | `mem <addr> [n]` | 内存显示 |
| | `test bitstream <file>` | 仅下载 bit 不启动，验证 PL |
| | `test app <file>` | 仅校验 header + CRC，不跳转 |
| | `reset` | 软复位（写 SLCR RESET_CTRL） |

### 6.3 关键设计

1. **CLI 与 boot_selector 互斥不并发**：同一时刻只跑其中一个。CLI 通过销毁自身线程切换到 boot 流程，避免并发 FileX 访问冲突
2. **`boot <app> [bit]` 一次性覆盖启动**：现场调试常用，命令直接调用 boot_selector 的底层 API，不走 cfg 文件
   - **实现路径**：`boot` 命令在 cli_thread 内**内联执行** boot_selector 的加载+handoff 流程，不切换线程、不创建新线程
   - 不调 `tx_thread_terminate(cli_thread)`（会自杀死锁），而是先停其他非 cli 线程，cli_thread 自己一路走到底，最后调 `jump_to_app` 跳走（cli_thread 自然消亡）
   - 跳走前的清理（cache/MMU/GIC）由 `handoff_exit` 统一做，与自动启动路径共用
3. **YMODEM 两阶段升级（DDR 暂存 → 校验 → 确认 → 提交）**：升级分两阶段，避免直接污染存储介质
   - **阶段 1：流式接收到 DDR 暂存区**（`0x02000000` 起 10MB）
     - YMODEM 边收边写到 DDR，**不写存储介质**（避免无谓磨损、避免校验前污染）
     - 接收完成立即在 DDR 内校验：app 校验 header magic + header_crc32 + payload crc32；bit 校验 Xilinx 头合法性
     - 传输失败 10 次重试放弃，DDR 暂存区清零
   - **阶段 2：用户确认后才提交到存储介质**
     - 串口显示：`Received app.bin (1234567 bytes), CRC OK. Commit to SD? [y/N]`
     - 用户输 `y` 才写存储，输 `N` 或超时（10s）则丢弃暂存、不写任何东西
     - 提交仍走 `<name>.tmp + rename` 保证原子性（断电安全）
   - **校验失败不提交**：CRC 错直接报告 + 丢弃暂存，存储介质零写入
   - **DDR 暂存区地址**：`#define STAGING_AREA_ADDR 0x02000000`，`STAGING_AREA_SIZE (10<<20)`
   - **容量约束**：app 镜像 ≤ 7MB（留余量给 YMODEM 分块对齐），暂存区 10MB 足够覆盖
4. **`cli_trigger.c` 两路检测**：
   - GPIO：上电立即采样 MIO 引脚，10ms 周期消抖
   - UART 魔数：UART 接收中断里维护 4 字节滑窗，匹配到 `0xDEADBEEF` 立即置标志
   - 任一标志触发 → `tx_event_flags_set` 通知 countdown 线程切换到 CLI
5. **命令注册用宏表**：`cli_command_table.h` 里 `CLI_CMD("ls", cli_cmd_ls, "list directory")` 一行一命令
6. **boot.cfg 原子写**：`cfg save` 同样走 `.tmp + rename` 模式
7. **`rm` 安全边界**（防误删致砖）：
   - 拒绝删除 `BOOT.bin`（BootROM 启动镜像，删了只能 JTAG 救）——返回错误并提示
   - 删除 `boot.cfg` 需二次确认（与 `cfg save` 同等敏感）
   - 删除当前 boot.cfg 指向的 `app_current.bin` / `pl_current.bit` 也需二次确认
   - 通配符一律不支持（避免 `rm *.bin` 误伤）

---

## 7. 精简 FSBL（Stage 1）

### 7.1 定位与现有 FSBL 分析

精简 FSBL 不从零设计，而是**裁剪用户现有的 `E:\桌面\docs\threadx_for_zynq7010\zynq7010\zynq_fsbl\`**。Xilinx 官方 FSBL 是为通用场景设计的庞然大物，而我们的 SSBL 接管了大部分职责（bitstream 加载、多镜像选择、跳转、看门狗、容错），FSBL 只需做**最小职责子集**。

现有 FSBL `main.c` 的完整流程：
```
ps7_init() → SlcrUnlock → DCacheFlush+Disable → RegisterHandlers
→ 打印 banner → DDRInitCheck → InitPcap → GetSiliconVersion
→ [可选] WDT init → 读 boot mode → 选 MoveImage（QSPI/NAND/NOR/SD/MMC/JTAG 六分支）
→ LoadBootImage()（image_mover.c 解析多 partition）→ FsblHandoff → FsblHandoffExit
```

现有 `fsbl_handoff.S` 的 `FsblHandoffExit` 跳转序列（精简版的黄金参考）：
```asm
mov  lr, r0              ; 目标地址放 LR
mcr  p15,0,r0,cr7,cr5,0  ; invalidate I-cache
mcr  p15,0,r0,cr7,cr5,6  ; invalidate branch predictor
dsb; isb
ldr  r4, =0
mcr  p15,0,r4,cr1,cr0,0  ; 写 SCTLR=0，一次性关 ICache+DCache+MMU
isb
bx   lr                  ; 跳转
```
**关键观察**：FSBL handoff **不做 D-cache flush**——因为 main.c 在 handoff 前 `Xil_DCacheFlush()` 已做；且 FSBL 直接把 SCTLR 写常数 0（一次性关 ICache+DCache+MMU），不分步。
**SSBL 的 `handoff_exit.S`（§13.4）不等同于此序列**：SSBL→app 跨 ThreadX 实例，cache 状态更复杂，必须先 clean D-cache 再分步清 cache 位和 M 位（顺序错会丢脏数据）。两者结构不同——FSBL 版本是"main.c 已 flush 后的简化收尾"，SSBL 版本是"自带完整 clean+disable 的彻底清理"，不要互相套用。

### 7.2 精简 FSBL 的最小职责

| 职责 | 保留/删除 | 理由 |
|---|---|---|
| ps7_init（PLL/MIO/DDR） | ✅ **保留** | 只有它能初始化 DDR，SSBL 依赖 DDR |
| SlcrUnlock / 异常处理注册 | ✅ 保留 | 基础环境 |
| DDRInitCheck（DDR 读写测试） | ✅ 保留 | 早期发现 DDR 故障 |
| InitPcap（devcfg 初始化） | ⚠️ **可选保留** | 若 SSBL 要用 PCAP 下 bit，FSBL 需初始化 devcfg；若 SSBL 自己初始化则 FSBL 可省 |
| 读 boot mode + 初始化介质 | ✅ 保留（仅 SD/QSPI 分支） | 删除 NAND/NOR/MMC/JTAG 分支 |
| LoadBootImage（image_mover.c） | ✅ 简化 | 只加载**1 个** partition（SSBL），不做多 partition 遍历 |
| FsblHandoff → FsblHandoffExit | ✅ 保留 | 复用现有 `fsbl_handoff.S` |
| bitstream 下载 | ❌ **删除** | 由 SSBL 动态做（核心承诺） |
| RSA / 安全启动 | ❌ 删除 | 开发期关闭，后期需要再加 |
| MMC / NAND / NOR boot mode | ❌ 删除 | 只支持 SD（前期）+ QSPI（后期） |
| Fallback / Multiboot / Golden Image 搜索 | ❌ 删除 | 容错由 SSBL boot.cfg + 看门狗做（§10.4） |
| WDT（看门狗） | ❌ 删除（FSBL 阶段） | 由 SSBL 启动看门狗，FSBL 跑得快不需要 |
| `fsbl_printf` 详细调试 | ⚠️ 精简 | 保留 banner + 错误码，删除 DEBUG_INFO 级别 |
| CheckWDTReset | ❌ 删除 | 无 WDT 则无需检查 |
| `image_mover.c` 多 partition 解析 | ⚠️ 简化 | FSBL 只需顺序读 1 个 partition |

### 7.3 精简 FSBL 的源文件清单

基于现有 `zynq_fsbl/` 目录裁剪：

```
boot/fsbl/
├── README.md                    [新] 说明裁剪要点、与原版差异
├── lscript.ld                   [复用] OCM 布局不变（代码 0x0，栈 0xFFFF0000）
├── main.c                       [裁剪] 删除 NAND/NOR/MMC/JTAG 分支 + WDT + RSA
├── fsbl.h                       [裁剪] 删除 NAND/NOR/MMC/JTAG 相关宏
├── fsbl_debug.h                 [复用] 日志级别宏
├── fsbl_hooks.h / fsbl_hooks.c  [复用] 用户钩子（空实现）
├── fsbl_handoff.S               [复用] 跳转汇编，零改动（黄金参考）
├── image_mover.c                [简化] 只加载第一个 CPU partition
├── sd.c / sd.h                  [复用] SD 卡初始化 + 读
├── qspi.c / qspi.h              [复用] QSPI 初始化（后期启用）
├── pcap.c / pcap.h              [复用] devcfg 驱动（SSBL 复用其 API）
├── ps7_init.c / ps7_init.h      [复用] Vivado 导出的 PS 初始化
└── zynq_fsbl_bsp/               [复用] 整个 BSP（standalone + xilffs 等）
```

**删除的源文件**（原 FSBL 有但我们不需要）：
- `nand.c/h`、`nor.c/h`：不支持这两种介质
- `rsa.c/h`：开发期不用安全启动
- `md5.c/h`：FSBL 内置校验，我们用 SSBL 的 CRC32

### 7.4 精简 FSBL 的 main.c 流程（裁剪后）

```c
int main(void)
{
    u32 BootModeRegister;
    u32 HandoffAddress;
    u32 Status;

    /* 1. PS 初始化（时钟/MIO/DDR）— 必须 */
    Status = ps7_init();
    if (Status != FSBL_PS7_INIT_SUCCESS) {
        OutputStatus(PS7_INIT_FAIL);
        FsblHookFallback();   /* 死循环 */
    }

    SlcrUnlock();

    /* 2. 清 D-cache（handoff 前必须） */
    Xil_DCacheFlush();
    Xil_DCacheDisable();

    /* 3. 注册异常处理 */
    RegisterHandlers();

    /* 4. 精简 banner */
    xil_printf("\n\rFSBL (slim) %s %s\r\n", __DATE__, __TIME__);

    /* 5. DDR 读写测试 */
    Status = DDRInitCheck();
    if (Status != XST_SUCCESS) {
        OutputStatus(DDR_INIT_FAIL);
        FsblHookFallback();
    }

    /* 6. devcfg 初始化（SSBL 下 bit 需要） */
    Status = InitPcap();
    if (Status != XST_SUCCESS) {
        OutputStatus(PCAP_INIT_FAIL);
        FsblHookFallback();
    }

    /* 7. 读 boot mode，只支持 SD 和 QSPI */
    BootModeRegister = Xil_In32(BOOT_MODE_REG) & BOOT_MODES_MASK;

    if (BootModeRegister == SD_MODE) {
        Status = InitSD("BOOT.BIN");
        MoveImage = SDAccess;
    }
#ifdef XPAR_PS7_QSPI_LINEAR_0_S_AXI_BASEADDR
    else if (BootModeRegister == QSPI_MODE) {
        InitQspi();
        MoveImage = QspiAccess;
    }
#endif
    else {
        OutputStatus(ILLEGAL_BOOT_MODE);
        FsblHookFallback();
    }

    /* 8. 加载 SSBL（image_mover 简化版：只加载第一个 a9_0 partition） */
    HandoffAddress = LoadBootImage();

    /* 9. handoff 到 SSBL */
    FsblHandoff(HandoffAddress);

    return XST_SUCCESS;
}
```

**裁剪效果**：
- 代码体积从 ~100KB 降到 ~30-40KB（OCM 192KB 绰绰有余）
- 启动时间从数百毫秒降到几十毫秒
- 删除了 ~60% 的条件编译分支，可读性大幅提升

### 7.5 FSBL 与 SSBL 的职责边界（明确划分）

| 阶段 | 职责 | 不做 |
|---|---|---|
| **FSBL** | ps7_init、DDR 测试、devcfg 初始化、读介质、加载 1 个 partition（SSBL）、handoff | bitstream、多 partition、看门狗、容错、RSA |
| **SSBL** | ThreadX 启动、FileX 挂载、读 boot.cfg、选 app/bit、加载 app、下 bit、看门狗、handoff 到 app | PS 初始化（FSBL 做过了）、DDR 初始化（FSBL 做过了） |

**关键约束**：
- FSBL handoff 后**不保留任何状态**（OCM 内容 SSBL 不能依赖，因为 SSBL 跑在 DDR）
- FSBL 初始化的 devcfg 寄存器状态 SSBL 可继承（避免重复初始化 PCAP）
- FSBL 的 `FlashReadBaseAddress` 全局变量在 handoff 时丢失（SSBL 自己重新挂载介质）

### 7.6 image_mover.c 的简化策略

原 `image_mover.c` 的 `LoadImage` 遍历 Image Header Table 的所有 partition，对每个 partition 调 `MovePartition`。简化版只需：

```c
u32 LoadBootImage(void)
{
    /* 1. 读 Boot Header（偏移 0x0），确认 XLNX magic */
    if (ImageCheckID(0) != XST_SUCCESS) return 0;

    /* 2. 读 Image Header Table → 第一个 Image Header */
    /* 3. 遍历该 Image 的 partition 列表，找 destination_cpu=a9_0 的第一个 */
    /* 4. MovePartition: 把 SSBL.bin 从介质拷到 DDR_LOAD_ADDR（0x100000） */
    /* 5. 返回 partition 的 ExecAddr 作为 handoff 地址 */
    return ExecAddr;
}
```

不需要：
- 多 Image 遍历
- PL partition（bitstream）分支
- checksum 校验（简化版可省，由 SSBL 自己校验自己的镜像）
- RSA 认证

### 7.7 FSBL 工程的构建方式

**方式 A（推荐前期）**：基于 Vitis "Create Zynq Bootloader" 模板创建标准 FSBL，然后**手工裁剪**——删除 `nand.c/nor.c/rsa.c`，修改 `main.c` 删除对应分支。优点：与 Vivado HDF 集成顺畅，ps7_init.c 自动生成。

**方式 B（后期稳定后）**：把裁剪后的 FSBL 源码从 `threadx_for_zynq7010/zynq_fsbl/` 拷到 `boot/fsbl/`，用 `arm-none-eabi-gcc` 命令行构建，脱离 Vitis。优点：构建可脚本化、CI 友好。

两种方式 `lscript.ld` 都复用现有的（OCM 布局），`fsbl_handoff.S` 零改动（黄金参考）。

---

## 8. ThreadX 集成（直接复用现有移植）

### 8.1 工程资产复用映射

```
现有工程（vendor 拷贝源）             →  多级启动项目中的角色
─────────────────────────────────────────────────────────────
hello_threadx/src/ThreadX/           →  boot/ssbl/ThreadX/  (整目录拷贝, 含所有移植修正)
                                     →  boot/app_template/ThreadX/  (同上)
hello_threadx/src/BSP/               →  boot/ssbl/BSP/  (拷贝, 按需精简)
                                     →  boot/app_template/BSP/  (拷贝)
hello_filex/src/FileX/               →  boot/ssbl/FileX/  (整目录拷贝)
hello_filex/.../fx_zynq_sdio_driver.c →  boot/ssbl/storage/sd_port.c  (复用核心逻辑)
现有 lscript.ld（DDR 0x100000）      →  boot/ssbl/ssbl.ld  (改 ORIGIN=0x100000, LENGTH=0x100000, 即 1MB 上限)
                                     →  boot/app_template/app.ld  (改 ORIGIN=0x1000000, LENGTH=0x800000, 即 8MB 上限)
现有 boot.S / reset.S                →  SSBL 和 app 都复用（"接近 reset"语义的基石）
```

### 8.2 已趟过的移植修正点（写进 SSBL README 避免重犯）

| 修正点 | 位置 | 内容 |
|---|---|---|
| tick 重载值 | `tx_initialize_low_level.S` | `LDR r0, =3333333`（不是 `0xF0000`）→ 准确 10ms |
| 优先级屏蔽 | `tx_initialize_low_level.S` | `MOV r0, #0xF0`（不是 `0x1F`）|
| lr 寄存器保存 | `tx_initialize_low_level.S` | `MOV r12, lr` + `PUSH {r12}` |
| 空闲节能 | `tx_thread_schedule.s` | 加 `DSB; WFI; ISB`（无任务时睡眠） |
| 向量表对齐 | `reset.S` | `__vectors` 放 `.vectors` 段紧跟 `_vector_table` |
| 中断分发 | `tx_initialize_low_level.S` | `by_pass_timer_interrupt` 调 C 函数 `tx_irq_dispatch` |
| GIC 复用 | `bsp_init.c` | `tx_irq_dispatch` 复用 Xilinx `XScuGic` 的 `HandlerTable` |
| 栈不重设 | `tx_initialize_low_level.S` | 沿用 boot.S 的栈设计，不重定位 |

### 8.3 SSBL 初始化序列（基于现有移植，零新 port 代码）

```
Xilinx BSP boot.S（由 FSBL handoff 触发）
  └─ _boot → OKToRun:
       ├─ 设 VBAR = _vector_table
       ├─ 清 cache/TLB、关 MMU
       ├─ 设各模式栈（SYS/IRQ/SVC/ABT/FIQ/UND）
       ├─ 使能 SCU/MMU/L2/FPU/分支预测
       └─ 跳 start → main()
            ↓
main.c（SSBL 自己写）
  └─ board_init()            ← 复用现有，注册 LED/KEY/UART/SD 驱动
  └─ tx_kernel_enter()       ← 不返回
       └─ tx_application_define()
            ├─ 创建 countdown_thread
            ├─ 创建 trigger_monitor_thread
            └─ [按需] cli_thread 或 boot_selector 流程
```

---

## 9. 程序跳转交接（核心）

### 9.1 "接近 reset"语义

**SSBL 跳转前只做破坏性清理**（关 cache/MMU/GIC/Timer），**不设 VBAR 不设 SP**。app 自带 Xilinx BSP `boot.S` 走完整 `OKToRun` 序列自己重建环境。这样 app 完全独立——裸机、ThreadX、其他 RTOS 都能跳。

### 9.2 跳转序列（纯破坏性清理）

```
handoff.c: jump_to_app(app_load_addr)
  /* app 入口 = load_addr（payload[0] 即 reset 向量，header 不进 DDR） */

步骤                                       关键 API / 指令
─────────────────────────────────────────────────────────────────────
1. tx_thread_terminate(所有非自身线程)      停应用线程
2. tx_thread_relinquish()                  让调度器跑一轮

3. CPSR |= I|F|A                           cpsid ifa（关 IRQ/FIQ/Async）

4. 停 Private Timer                         清控制寄存器 enable 位
                                            清挂起（write 1 to ISR）

5. 关 GIC                                   XScuGic_Stop():
                                              GICD_CTLR = 0
                                              GICC_CTLR = 0

6. Flush D-cache（clean + invalidate）       Xil_DCacheFlush()
                                            ★ Xilinx 的 Flush = ARM 术语的 clean+invalidate
                                            ★ 关键：不能单独用 Invalidate（会丢脏数据）

7. Flush L2 (PL310)                          Xil_L2CacheFlush()
                                            ★ Xilinx 的 L2 Flush = clean + invalidate，一次即可
                                            ★ 无需再单独 Invalidate（§13.4 一致）

8. Invalidate I-cache + BTB + TLB           Xil_ICacheInvalidate()
                                            mcr p15,0,r0,c7,c5,0  ; ICIALLU
                                            mcr p15,0,r0,c8,c7,0  ; TLBIALL

9. Disable I-cache / D-cache                SCTLR 清 I(C12) 位和 C(C2) 位

10. Disable MMU                              Xil_DisableMMU()（汇编）
                                              ★ 必须在 cache 操作之后

11. invalidate TLB 再一次                    防关 MMU 后残留

12. 跳转到 app 入口（app 的 _vector_table[0] = reset，即 load_addr 本身）
                                            bx <addr>
```

**铁律**：clean cache → disable cache → disable MMU（颠倒丢数据）。汇编隔离在 `handoff_exit.S`，便于按社区经验反复调试，不污染主逻辑。

**为什么这么繁琐**（每行都对应一个真实 bug 来源）：
- 步骤 6 用 **Flush（clean+invalidate）**：单独用 Invalidate 会丢弃脏数据，跳转后 app 从 DDR 读到 SSBL cache 里的旧值。Xilinx `Xil_DCacheFlush` = clean+invalidate，正合此意
- 步骤 10 顺序：关 MMU 前必须保证 cache 已 clean，否则 dirty line 永远丢失
- 步骤 5 关 GIC：避免跳转瞬间残留中断打进旧向量

### 9.3 跳转后的硬件状态（app 见到的环境）

| 资源 | 状态 | app boot.S 该做的 |
|---|---|---|
| DDR 控制器 | ✅ 已初始化（FSBL 做的） | 不重做 DDRC init；直接用 |
| DDR 内容 | ✅ 有效（含 app 镜像） | 直接取指执行 |
| CPU 模式 | SVC | OKToRun 会重设各模式栈 |
| VBAR | 未定义（SSBL 已清） | OKToRun 重设 = `_vector_table` |
| Cache (I/D) | disabled | OKToRun 重新 invalidate + enable |
| L2 | disabled/invalidated | OKToRun 重新 init + enable |
| MMU | disabled | OKToRun 重建页表 + enable |
| GIC | stopped | app 按需 XScuGic_Initialize |
| Private Timer | stopped | ThreadX port 重配 |
| SCU | enabled（FSBL 留下） | OKToRun 已处理 |
| FPU/VFP | disabled | OKToRun 重新 enable |

**唯一与真 reset 的差别**：DDR 已初始化且内容有效。这是不可避免的（否则 app 无处加载），也是有益的（app 不必再带 DDRC init 代码）。

### 9.4 app 工程模板（直接基于 hello_threadx）

```
boot/app_template/
├── app.ld                    ← 基于 hello_threadx 的 lscript.ld
│                                ORIGIN = 0x1000000（不是 0x100000！）
│                                其余完全一致
├── src/
│   ├── BSP/                  ← 从 hello_threadx 拷贝
│   │   ├── board_init.c      ← 注册 app 需要的驱动
│   │   ├── bsp_init.c
│   │   └── driver/
│   │       ├── device_core/
│   │       ├── led_driver/      ← 示例：app 业务用
│   │       └── ...
│   ├── ThreadX/              ← 完整拷贝（含所有移植修正）
│   │   ├── Source/
│   │   ├── Port/             ← reset.S, tx_initialize_low_level.S, v7.S
│   │   ├── tx_port.h
│   │   └── tx_user.h
│   ├── utils/
│   ├── main.c                ← app 业务入口（tx_kernel_enter → 用户线程）
│   └── includes.h
└── README.md                 ← "复制此目录，改 main.c 即得新 app"
```

**关键差异**（与 hello_threadx）：
- `lscript.ld` 的 `ORIGIN = 0x1000000`（不是 `0x100000`）—— 避开 SSBL 区
- `main.c` 里 `tx_application_define` 创建的是**业务线程**（不是 SSBL 的 countdown/trigger/cli）
- 不需要 FileX/storage/handoff 模块（那些是 SSBL 专属）
- **可 JTAG 独立调试**：直接下载 app.elf 到 0x1000000 跑，不依赖 SSBL——这是验证 app 本身正确的最快路径

### 9.5 镜像打包流程

```
1. arm-none-eabi-gcc 编译 app_template → app.elf
   （ENTRY 是 _vector_table，符合 Xilinx 约定）

2. arm-none-eabi-objcopy -O binary app.elf app.raw

3. scripts/pack_app.py:
   header = struct.pack('<IIIIIII',
       0x5A424F4F,          # magic "ZBOO"
       1,                   # version
       IMAGE_TYPE,          # 1=ThreadX, 2=bare-metal, ...
       0x01000000,          # load_addr（与 app.ld ORIGIN 一致）
       len(app_raw),        # image_size
       crc32(app_raw),      # crc32
       crc32(header[:24]))  # header_crc32
   open('app.bin','wb').write(header + app_raw)

4. YMODEM 上传 app.bin 到 SD 卡
5. SSBL 读 app.bin → 校验 header（前 32 字节）→ 读 payload（app.bin[32:]）
   → 把 payload 整体拷到 0x01000000（payload[0] 即 reset 向量）
   → jump_to_app(0x01000000)  → bx 0x01000000
6. app 从 _vector_table[0]（reset 向量）开始，进 _boot → OKToRun
```

---

## 10. 错误处理与可观测性

### 10.1 错误分级

| 等级 | 含义 | 示例 | 处理策略 |
|---|---|---|---|
| **FATAL** | SSBL 无法继续 | board_init 失败、SD/FileX 挂载失败 | LED 闪烁错误码 + 串口打印 + 死循环 |
| **BOOT_REFUSED** | 启动条件不满足 | boot.cfg 缺失/损坏、app 文件不存在、CRC 错 | 进 CLI（不 boot）+ 串口告警 |
| **RECOVERABLE** | 部分功能受损 | bit 文件缺失（app OK）、SD 卡只读 | 告警但继续启动 |
| **INFO** | 正常运行信息 | 当前选择、加载进度、启动耗时 | 串口打印 |

只有 `board_init` 和 SD 挂载失败才 FATAL 死循环（这两个失败连 CLI 都跑不起来）。

### 10.2 统一日志接口（接入现有 utils/）

```c
/* boot/ssbl/include/ssbl_log.h */

typedef enum {
    SSBL_LOG_FATAL = 0,
    SSBL_LOG_ERROR,
    SSBL_LOG_WARN,
    SSBL_LOG_INFO,
    SSBL_LOG_DEBUG
} ssbl_log_level_t;

/* 编译期通过 ssbl_config.h 的 SSBL_LOG_LEVEL 静态过滤 */
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
```

**设计要点**：
- 用 `tx_time_get()` 加时间戳（tick 数），方便定位"卡在哪一步"
- 编译期过滤：发布版只编 `INFO`，调试版编 `DEBUG`，零运行时开销
- 复用 `xil_printf`（已用），不引入新打印库
- CLI 里可加 `log level <0-4>` 命令运行时调整

### 10.3 LED 错误码（无串口也能诊断）

复用现有 `led_driver`。FATAL 时用 LED 闪烁次数编码错误：

| 闪烁次数 | 含义 |
|---|---|
| 1 短闪 | board_init 失败 |
| 2 短闪 | SD/FileX 挂载失败 |
| 3 短闪 | FSBL handoff 后 DDR 校验失败 |
| 持续快闪 | SSBL 异常（不应到达） |

### 10.4 上次启动状态持久化（boot.log）

SD 卡维护一个 `boot.log`（环形覆盖，避免写磨损），最多保留最近 8 次记录：

```
[2026-06-22 10:23:11] boot OK: app=app_current.bin bit=pl_current.bit duration=412ms
[2026-06-22 10:21:05] boot FAIL: app_crc_mismatch file=app_a.bin
[2026-06-22 10:18:33] boot OK: app=app_a.bin bit=pl_a.bit duration=405ms
```

**实现要点**：
- **不要求 app 配合写日志**（保持 app 完全独立，不必知道 SSBL 存在——见 §9.1）
- **判活机制（唯一一种，不可二选一）**：SSBL 跳转前在 OCM 高地址（`0xFFFF0000` 起的保留区）写入一个 `BOOT_ATTEMPT_MAGIC` + 本次尝试的 app 名，**不**启动硬件看门狗。app 在自己的初始化里**写一次** OCM 同地址为 `BOOT_OK_MAGIC`（这是 app 唯一需要对 SSBL 做的"配合"，单个寄存器写）。
- **判据回填时机**：boot.log **本次不写**，必须由 SSBL **下次启动时**基于 OCM 残留标记回填——
  - SSBL 启动时读 OCM 同地址：
    - 是 `BOOT_OK_MAGIC` → 上次 boot 成功，写入一条 `OK` 记录到 boot.log
    - 是 `BOOT_ATTEMPT_MAGIC` → 上次 app 没喂狗就崩了，写入一条 `FAIL: app_not_started` 记录，可选触发 A/B 回退
    - 是其它值（首次启动 / POR 后 OCM 随机）→ 不写记录
  - 这避免了"本次写时还不知道 app 会不会活"的逻辑悖论
- **OCM 内容在软复位（写 SLCR RESET_CTRL）后丢失**：所以"上次状态"必须在 SSBL 下次启动的**极早期**读出并回填到 SD 的 boot.log（在跳转本次 app 之前），OCM 只承担"本次尝试"的活体标记。**硬复位（POR，断电）也丢 OCM**——这是可接受的：硬复位后无法追溯上次状态，等同首次启动。
- **不引入硬件看门狗**：原设计的 TTC0 看门狗会让 boot_delay（3s）和"app 启动到喂狗"的时间窗冲突（app 自己要重建 MMU/cache/GIC/ThreadX，>5s 合理）。改为纯 OCM 标记 + 软复位检测，无超时压力。如后期需要"app 跑飞后自动复位"，再独立设计（基于 `RESET_REASON` 寄存器，与本机制解耦）。

---

## 11. 介质抽象层（为迁移铺路）

### 11.1 接口设计

```c
/* boot/ssbl/storage/storage.h */

typedef struct {
    int  (*open)   (void);
    int  (*close)  (void);
    int  (*file_open)   (const char *path, int mode);
    int  (*file_read)   (void *buf, uint32_t len);
    int  (*file_write)  (const void *buf, uint32_t len);
    int  (*file_close)  (void);
    int  (*file_size)   (const char *path, uint32_t *size);
    int  (*file_delete) (const char *path);
    int  (*file_rename) (const char *old, const char *new);
    int  (*dir_list)    (const char *path);
} storage_ops_t;

/* 链接时由 sd_port.c 或 qspi_port.c 提供实例 */
extern const storage_ops_t *g_storage;

/* 便捷包装宏 */
#define storage_open()          g_storage->open()
#define storage_file_open(p,m)  g_storage->file_open(p,m)
#define storage_file_read(b,l)  g_storage->file_read(b,l)
#define storage_file_write(b,l) g_storage->file_write(b,l)
/* ... */
```

**关键**：`image_loader`、`cli`、`boot_selector` 都只调 `storage_*` 宏，不直接碰 FileX。迁移时换 `g_storage` 指向 `qspi_port.c` 的实例即可。

### 11.2 迁移路径（SD → QSPI）

- **阶段 A（当前）**：SD 卡 + FileX FAT。`storage/sd_port.c` 复用 `fx_zynq_sdio_driver.c`
- **阶段 B（过渡）**：BOOT.bin 在 QSPI（产品出厂固化），app/bit 在 SD 卡（便于升级）。storage 层不变
- **阶段 C（最终）**：全 QSPI。新增 `storage/qspi_port.c` 基于 FileX + LevelX NOR
  - 需要：`lx_nor_flash_initialize` 对接 `xqspips` 驱动
  - FileX I/O driver 翻译扇区操作到 `lx_nor_flash_*`
  - QSPI 走 I/O 模式（不是线性 XIP 模式）
  - `fx_media_format` 格式化 NOR 为 FAT
  - **上层（image_loader/boot_selector/cli）：零改动**

---

## 12. 测试策略

### 12.1 分层测试（从易到难）

| 层次 | 测试方法 | 覆盖目标 |
|---|---|---|
| **PC 单元测试** | host GCC 编译纯逻辑模块（CRC32、boot.cfg 解析、YMODEM 状态机） | 算法正确性 |
| **pack_app.py 自测** | Python 打包→解包→校验 round-trip | 镜像工具链 |
| **JTAG 单板测试** | app.elf 直接下载到 0x01000000 跑，验证 app 独立可运行 | app 工程正确性 |
| **SSBL 集成测试** | 完整启动链：FSBL → SSBL → app，JTAG 调试 SSBL | 端到端 |
| **SD 卡现场测试** | 拔插 SD、改 boot.cfg、YMODEM 升级 | 现场可靠性 |

### 12.2 关键测试场景清单

**SSBL 核心逻辑**：
1. boot.cfg 正常 → 加载指定 app → 跳转成功
2. boot.cfg 缺失 → 用默认配置启动
3. boot.cfg 损坏（随机字节）→ 用默认配置 + 告警
4. app 文件不存在 → 回 CLI
5. app CRC 错（手工翻转一字节）→ 回 CLI
6. bit 文件不存在但 app OK → 告警继续启动
7. CLI 触发（GPIO 按键）→ 进 CLI
8. CLI 触发（UART 魔数 0xDEADBEEF）→ 进 CLI
9. 超时无触发 → 自动启动
10. `boot <app>` 一次性覆盖启动

**YMODEM 升级（两阶段流程）**：
11. 正常升级 app：YMODEM 接收 → DDR 校验 OK → 确认 → 提交 → 重启加载新 app
12. 升级传输中断电：DDR 暂存丢失，SD 卡原 app 完全未触碰（零修改）
13. 升级校验失败（CRC 错）：不弹确认，直接报错丢弃，SD 卡零写入
14. 升级用户拒绝（输 N）：DDR 暂存清零，SD 卡零写入
15. 提交阶段断电：原 app 完好（.tmp 未 rename 成功）+ 下次启动 SSBL 清理半写 .tmp

**handoff 跳转**：
14. 跳转后 app 正常跑 ThreadX（LED 闪烁、串口打印）
15. 跳转后 app 的中断正常（KEY/UART 中断响应）
16. 跳转后 app 的 GIC/Private Timer 干净（无残留中断）
17. 跳转后 app 的 cache/MMU 由 app 自己重建（验证 SSBL 确实关了）

**A/B 切换（基础）**：
18. 改 boot.cfg 的 app= 字段 → 重启加载另一个 app
19. `cfg set app=app_b.bin` + `cfg save` → 重启生效

---

## 13. 构建流程

### 13.1 工具链与依赖

| 工具 | 用途 | 来源 |
|---|---|---|
| **Vitis / XSCT** | 创建 FSBL 工程、生成 ps7_init.c | Xilinx Vivado 配套 |
| **arm-none-eabi-gcc** | 编译 SSBL/app | Vitis 自带 / LaunchPad |
| **bootgen** | 打包 BOOT.bin（fsbl + ssbl） | Vitis 自带 |
| **Python 3** | `pack_app.py` 打包 app.bin | PC 端 |
| **Tera Term / SecureCRT** | YMODEM 升级、CLI 交互 | PC 端 |

### 13.2 `bif/boot.bif`

```
the_ROM_image:
{
    [bootloader, destination_cpu = a9_0]   fsbl.elf
    [destination_cpu = a9_0]               ssbl.bin
}
```

**注意**：第二个 partition 是 SSBL，**不带 bitstream**（PL 由 SSBL 动态配）。`ssbl.bin` 由 `ssbl.elf` 经 `objcopy -O binary` 得到。

### 13.3 `scripts/pack_app.py`（核心骨架）

```python
#!/usr/bin/env python3
"""把 app.raw 加上 32 字节 header 得到 app.bin"""
import struct, zlib, sys

MAGIC = 0x5A424F4F  # little-endian 字节序为 0x4F 0x4F 0x42 0x5A，读作 "OOBZ"
                    # 与 §4.1 一致；注释别名 "ZBOO" 仅作 big-endian 助记
VERSION = 1
IMAGE_TYPE_THREADX = 1
DEFAULT_LOAD_ADDR = 0x01000000

def pack(raw_path, out_path, load_addr=DEFAULT_LOAD_ADDR,
         image_type=IMAGE_TYPE_THREADX):
    with open(raw_path, 'rb') as f:
        raw = f.read()
    size = len(raw)
    crc = zlib.crc32(raw) & 0xFFFFFFFF

    header_no_crc = struct.pack('<IIIIII',
        MAGIC, VERSION, image_type, load_addr, size, crc)
    header_crc = zlib.crc32(header_no_crc) & 0xFFFFFFFF
    header = header_no_crc + struct.pack('<I', header_crc)

    with open(out_path, 'wb') as f:
        f.write(header)
        f.write(raw)
    print(f"Packed: {out_path} (payload={size} bytes)")

if __name__ == '__main__':
    pack(sys.argv[1], sys.argv[2])
```

### 13.4 `handoff_exit.S`（核心汇编骨架）

```asm
/* SSBL → app 跳转：纯破坏性清理，不设 VBAR/SP（app 自带 boot.S） */
    .section .text.handoff_exit
    .arm
    .global handoff_exit
    .type   handoff_exit, %function

handoff_exit:
    /* r0 = app entry address (= load_addr；payload[0] 即 reset 向量，
     *    header 已在搬运时剥离、不在 DDR，故不加 0x20）。
     *    直接 bx r0，不经 lr（与 §7.1 FSBL 版本 mov lr,r0/bx lr 写法不同，
     *    但语义等效；这里因 C 调用约定 r0 即参数，更直接）。 */

    /* 1. 禁所有中断 */
    cpsid   ifa

    /* 2-5. 停 Private Timer + GIC 由 C 侧 handoff.c 提前完成
     *      （XScuGic_Stop + 写 Private Timer 控制寄存器）
     *      这里只做 cache/MMU 相关 */

    /* 6. Clean D-cache (写回 DDR) */
    bl      Xil_DCacheFlush

    /* 7. Clean + Invalidate L2 (PL310) */
    bl      Xil_L2CacheFlush

    /* 8. Invalidate I-cache + BTB + TLB */
    bl      Xil_ICacheInvalidate

    /* 9. Disable I-cache / D-cache */
    mrc     p15, 0, r1, c1, c0, 0      /* SCTLR */
    bic     r1, r1, #(1<<12)|(1<<2)    /* 清 I(C12) 和 C(C2) 位 */
    mcr     p15, 0, r1, c1, c0, 0
    isb

    /* 10. Disable MMU (必须在 cache 操作之后) */
    mrc     p15, 0, r1, c1, c0, 0      /* SCTLR */
    bic     r1, r1, #1                 /* 清 M 位 */
    mcr     p15, 0, r1, c1, c0, 0
    isb

    /* 11. invalidate TLB 再一次 */
    mov     r2, #0
    mcr     p15, 0, r2, c8, c7, 0      /* TLBIALL */
    dsb
    isb

    /* 12. 跳转到 app 入口（app 的 _vector_table[0] = reset） */
    bx      r0

    .size   handoff_exit, .-handoff_exit
```

**C 侧调用**：

```c
/* handoff.c */
extern void handoff_exit(uint32_t entry_addr) __attribute__((noreturn));

void jump_to_app(uint32_t app_load_addr)
{
    /* 停 ThreadX 调度相关 */
    tx_thread_terminate(cli_thread);
    tx_thread_terminate(countdown_thread);
    tx_thread_terminate(trigger_thread);
    tx_thread_relinquish();

    /* 停 Private Timer（CPU0 私有） */
    *(volatile uint32_t*)0xF8F00608 = 0;   /* PTIMER.Control: 清 enable */
    *(volatile uint32_t*)0xF8F0060C = 1;   /* PTIMER.ISR: 清挂起 */

    /* 停 GIC */
    XScuGic_Stop(&xInterruptController);

    /* 汇编做 cache/MMU 清理 + 跳转 */
    uint32_t entry = app_load_addr + 0x20;  /* 跳过 header */
    handoff_exit(entry);                     /* 不返回 */
}
```

---

## 14. 开发里程碑

| 里程碑 | 内容 | 验证标准 |
|---|---|---|
| **M1: 工程骨架** | 拷贝 hello_threadx → ssbl 工程，改 ORIGIN，能编译 | **JTAG 下载** ssbl.elf 到 0x100000 能跑 ThreadX LED 闪烁（此阶段 FSBL 未改，故只能 JTAG 加载） |
| **M2: 精简 FSBL** | 裁剪现有 zynq_fsbl：删 NAND/NOR/MMC/JTAG + RSA + WDT + 多 partition，简化 image_mover | fsbl.elf 单独下载能跑，打印 banner + DDR 测试通过 |
| **M3: FSBL→SSBL 链通** | boot.bif 打包 fsbl+ssbl，BOOT.bin 能启动到 SSBL | 串口打印 SSBL banner，LED 闪烁 |
| **M4: storage + FileX** | SD 卡挂载，能 ls/cat | CLI 输 `ls` 看到 BOOT.bin/boot.cfg |
| **M5: app 加载 + handoff** | app.bin 拷到 DDR + 跳转 | app 的 LED 闪烁，串口打印 app banner |
| **M6: boot.cfg + boot_selector** | 读配置自动选 app | 改 app= 字段重启生效 |
| **M7: CLI 完整功能** | 触发机制 + 命令集 | 按键/魔数进 CLI，所有命令可用 |
| **M8: YMODEM 升级** | DDR 暂存 → 校验 → 确认 → 提交（原子）| 升级 + 断电测试通过 |
| **M9: bitstream 动态加载** | FileX 读 .bit + PCAP | `test bitstream` 验证 PL |
| **M10: 错误处理 + boot.log** | 全部错误场景 + 日志 | 故障注入测试通过 |
| **M11: QSPI 迁移**（后期） | storage/qspi_port.c + LevelX | SD 测试用例在 QSPI 全过 |

---

## 15. 关键设计决策（最终汇总）

1. **最大化复用现有移植**：SSBL = hello_threadx 改造，app_template = hello_threadx 改 ORIGIN，零新 port 代码
2. **介质抽象层为迁移铺路**：上层只调 `storage_*`，SD→QSPI 换实现即可
3. **FSBL 不动 bitstream，PL 由 SSBL 动态配**：实现"动态选 bit"的核心承诺
4. **handoff 纯破坏性清理 + app 自带 boot.S** = "接近 reset"语义，app 完全独立
5. **YMODEM + .tmp + rename**：原子升级，断电安全
6. **错误退回 CLI**：避免变砖，串口始终是逃生通道
7. **app 独立可 JTAG 调试**：降低集成排查难度

---

## 附录 A：参考资源

### Zynq-7000 启动相关
- UG585 Zynq-7000 TRM — Handoff to FSBL / Changing Address Mapping / FSBL Image Fallback and Multiboot
  https://docs.amd.com/v/u/en-US/ug585-Zynq-7000-TRM
- UG821 Zynq-7000 SW Dev Guide — First Stage Bootloader
  https://docs.amd.com/v/u/en-US/ug821-zynq-7000-swdg
- UG1283 Bootgen User Guide — Zynq-7000 SoC Boot and Configuration
  https://docs.amd.com/v/u/en-US/ug1283-bootgen-user-guide
- Xilinx embeddedsw 仓库 — `zynq_fsbl/src/{main,image_mover,pcap}.c`
  https://github.com/Xilinx/embeddedsw/tree/master/lib/sw_apps/zynq_fsbl/src

### ThreadX / FileX 移植
- Eclipse ThreadX 主仓库（含 cortex-a9 port）：https://github.com/eclipse-threadx/threadx
- Eclipse FileX 仓库：https://github.com/eclipse-threadx/filex
- Eclipse LevelX 仓库（NOR/NAND 磨损均衡）：https://github.com/eclipse-threadx/levelx
- 用户现有工程：`E:\桌面\docs\threadx_for_zynq7010`（ThreadX 6.4.1 + FileX 6.4.1 已移植）

### 程序间跳转
- Zynq-7000 AP SoC Boot — Booting and Running Without External Memory Tech Tip
  https://xilinx-wiki.atlassian.net/wiki/spaces/A/pages/18842377/
- threadx issue #95 — Zedboard Cortex-A9 `tx_kernel_enter` 不跑的根因
