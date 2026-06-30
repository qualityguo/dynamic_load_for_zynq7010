# new_fsbl / new_ssbl 板级验证测试计划

> 版本：V1.0　日期：2026-06-30
>
> 范围：Zynq-7000 平台 `ssbl/new_fsbl`（一级引导，裸机）+ `ssbl/new_ssbl`（二级引导，ThreadX）。
>
> 测试类型：**板级手工验证**。所有用例均面向真实开发板，按"步骤 → 预期（含串口输出/错误码/提示串）→ 判据"给出可执行条目。
>
> 状态标识：`[ ]` 未测　`[P]` 通过　`[F]` 失败　`[B]` 阻塞（缺硬件/环境）。

---

## 1. 概述

### 1.1 测试目标

| 目标 | 说明 |
|---|---|
| 功能正确性 | FSBL/SSBL 每条代码路径（含错误分支）按设计输出且不死机/不误跳转 |
| 启动链完整性 | 上电 → FSBL → SSBL → app 全链路在 SD / QSPI 两种介质下均能跑通 |
| 错误处理鲁棒性 | 非法输入、损坏文件、掉电、超限等异常有明确提示并安全回退（多数回退到 CLI） |
| CLI 可用性 | 10 条 shell 命令、YMODEM 上下传、boot.cfg 读写均符合预期 |

### 1.2 关键地址常量（来自源码，用例可直接断言）

| 常量 | 值 | 来源 |
|---|---|---|
| `APP_LOAD_ADDR` | `0x01000000` | app_loader.h:29 |
| `BITSTREAM_DDR_ADDR` | `0x00800000`（staging 4MB） | bitstream_loader.h:31 |
| `YM_DDR_BASE` | `0x02000000`（8MB 上限） | shell_cmd.c:334 |
| SSBL 链接地址 | `0x00100000` | 见 info 命令输出 |
| `BOOT_MODE_REG` | `XPS_SYS_CTRL_BASEADDR + 0x25C` | fsbl.h:38 / ssbl.h:72 |
| `SD_MODE` / `QSPI_MODE` | `0x5` / `0x1` | fsbl.h:51/50 |
| `MAGIC_WORD`（UART 触发） | `0xDEADBEEF` | task_trigger.c:5 |
| `DDR_TEST_PATTERN` | `0xAA55AA55` | fsbl.h:74 |

---

## 2. 测试环境

### 2.1 硬件清单

| 设备 | 用途 | 必备 |
|---|---|---|
| Zynq-7010 开发板（PS+PL） | 被测对象 | 是 |
| microSD 卡（≥1GB，FAT32） | SD 启动介质 | 是 |
| QSPI Flash（板载） | QSPI 启动介质 | QSPI 用例必备 |
| USB-TTL 串口线 + UART1 接口 | 串口日志 + Shell + YMODEM | 是 |
| KEY0 按键（板载） | 触发测试 | 是 |
| PL 侧可观测外设（LED/数码管） | bitstream 烧写成功判据 | bit 用例必备 |
| JTAG 调试器 | 烧写 / 异常排查 | 调试用 |

### 2.2 软件工具

| 工具 | 用途 |
|---|---|
| Vitis / xsdb | 编译 new_fsbl.elf / new_ssbl.elf，下载调试 |
| bootgen（Xilinx） | 生成 BOOT.BIN（FSBL + SSBL 两分区） |
| 串口终端（支持 YMODEM） | 串口交互 + YMODEM 收发（如 teraterm / SecureCRT / minicom） |
| Win32DiskImager | 烧 SD 卡 / QSPI 镜像 |

### 2.3 BOOT.BIN 制作（FSBL+SSBL 双分区）

```bash
bootgen -image boot.bif -arch zynq -process_bitstream bin -o sd_card/BOOT.BIN
```

`boot.bif` 仅含 FSBL + SSBL 两个 partition（bitstream 与 app 由 SSBL 在线下载，不进 BOOT.BIN）。

### 2.4 测试介质文件准备

| 文件 | 说明 |
|---|---|
| `BOOT.BIN` | FSBL+SSBL 镜像 |
| `boot.cfg` | 启动配置（app/bit/boot_delay/auto_boot） |
| `app.bin` | 裸 app（flat bin，链接到 `0x01000000`，含 LED 闪烁等可观测现象） |
| `pl_a.bit` | Xilinx .bit（含头，烧后点亮 PL LED） |
| `bad.bit` | 故意篡改签名（非 `00 09 0F F0`）的 .bit |
| `empty.bin` | 0 字节文件 |
| `big.bin` | >4MB / >8MB 的越限文件 |

---

## 3. FSBL 测试项（new_fsbl）

> 入口：`new_fsbl/src/main.c`，流程 `ps7_init → DCacheFlush → RegisterHandlers → DDRInitCheck → 读 boot mode → InitSD/InitQspi → LoadBootImage → FsblHandoff`。
> 每阶段失败均 `while(1)` 卡死（裸机无回退），判据看串口最后一行。

### T-FSBL-01　PS 初始化（ps7_init）
- **目的**：验证 Vivado 生成的 ps7_init 正常完成时钟/MIO/DDR 配置。
- **前置**：板上电，SD 卡含合法 BOOT.BIN，boot mode 拨码 = SD。
- **步骤**：1) 上电；2) 打开串口（波特率匹配 lscript）。
- **预期**：串口打印 `[1/5] ps7_init() OK`。
- **通过判据**：出现 `[1/5] ps7_init() OK`，不卡死。
- **备注**：失败会打印 `PS7 initialization failed with status: 0x....` 后死循环；常见原因是 ps7_init.c 与硬件不匹配。

### T-FSBL-02　DDR 读写测试（DDRInitCheck）
- **目的**：验证 `0xAA55AA55` 在 `DDR_START_ADDR` 与 `+0x100000` 两处读写一致。
- **前置**：T-FSBL-01 通过。
- **步骤**：观察启动日志。
- **预期**：打印 `[3/5] DDR test OK (0x....)`（地址 = DDR_START_ADDR）。
- **通过判据**：出现 `[3/5] DDR test OK`。
- **备注**：失败打印 `DDR test failed at address ...: wrote ... read ...`。DDR 硬件故障时阻塞性失败。

### T-FSBL-03　异常处理注册（RegisterHandlers）
- **目的**：验证 6 个异常向量（Undef/SVC/PreFetch/Data/IRQ/FIQ）已注册。
- **前置**：T-FSBL-01 通过。
- **步骤**：观察启动日志。
- **预期**：打印 `[2/5] Exception handlers registered`。
- **通过判据**：日志中出现该行。
- **备注**：注册顺序在 DCache 之后、DDR 之前；顺序错乱说明 main.c 被改坏。

### T-FSBL-04　SD 启动模式正常流程
- **目的**：SD boot mode 下 FSBL 完整搬运 SSBL 并 handoff。
- **前置**：boot mode = SD（`0x5`），SD 卡含合法 BOOT.BIN。
- **步骤**：上电启动。
- **预期**：依次打印 `[4/5] Boot mode: SD` → `Partition 1 info:` → `Loading SSBL to DDR...` → `SSBL loaded (xxxx bytes)` → `[5/5] Handoff to 0x....` → `SUCCESSFUL_HANDOFF`。
- **通过判据**：出现 `SUCCESSFUL_HANDOFF`，随后串口出现 SSBL 日志。
- **备注**：Handoff 地址 = SSBL 的 ExecAddr（来自分区头），应落在 DDR 内。

### T-FSBL-05　QSPI 启动模式正常流程
- **目的**：QSPI boot mode 下 FSBL 完整搬运 SSBL 并 handoff。
- **前置**：boot mode = QSPI（`0x1`），QSPI 已烧合法 BOOT.BIN；`XPAR_PS7_QSPI_LINEAR_0_SAXI_BASEADDR` 已定义。
- **步骤**：拨码切 QSPI，上电。
- **预期**：打印 `[4/5] Boot mode: QSPI` → 后续同 T-FSBL-04。
- **通过判据**：出现 `SUCCESSFUL_HANDOFF`。
- **备注**：QSPI 分支由编译宏 `XPAR_PS7_QSPI_LINEAR_0_SAXI_BASEADDR` 控制；无 QSPI 硬件则标记 `[B]`。

### T-FSBL-06　非法 / 不支持的 boot mode
- **目的**：boot mode 既非 SD 也非 QSPI（如 JTAG `0x0`）时安全卡死。
- **前置**：boot mode 拨到 JTAG（`0x0`）。
- **步骤**：上电（经 ps7_init/DDR 后到 switch）。
- **预期**：打印 `Unsupported boot mode: 0x0000`，随后 `while(1)` 卡死。
- **通过判据**：打印对应 mode 值且不再有任何输出。
- **备注**：JTAG 模式下 FSBL 正常不应作为启动目标；本用例验证防御逻辑。

### T-FSBL-07　SD 初始化失败（无卡 / 无 BOOT.BIN）
- **目的**：SD 模式下介质不可读时安全卡死。
- **前置**：boot mode = SD，拔掉 SD 卡，或 SD 卡无 BOOT.BIN。
- **步骤**：上电。
- **预期**：打印 `[4/5] Boot mode: SD` → `SD initialization failed with status: 0x....`，卡死。
- **通过判据**：出现失败提示且不再继续。
- **备注**：状态码来自 InitSD（XSdPs 返回码）。

### T-FSBL-08　BOOT.BIN 分区头 checksum 损坏
- **目的**：`ValidatePartitionHeaderChecksum` 拦截被篡改的分区头。
- **前置**：手工篡改 BOOT.BIN 中分区 1 头的 checksum 字段。
- **步骤**：SD 启动。
- **预期**：打印 `PARTITION_HEADER_CORRUPTION` + `INVALID_HEADER_FAIL.`，卡死。
- **通过判据**：出现 `PARTITION_HEADER_CORRUPTION`。
- **备注**：checksum 校验是"免费保险"（重构计划 2.6 要求保留）；篡改偏移参考 fsbl.h 的 IMAGE_*_OFFSET。

### T-FSBL-09　BOOT.BIN 空分区表
- **目的**：分区头全 0 时被 `IsEmptyHeader` 拦截。
- **前置**：构造分区头全 0 的 BOOT.BIN。
- **步骤**：SD 启动。
- **预期**：打印 `IMAGE_HAS_NO_PARTITIONS` + `INVALID_HEADER_FAIL.`，卡死。
- **通过判据**：出现 `IMAGE_HAS_NO_PARTITIONS`。
- **备注**：必须有 SSBL 分区，空表非法。

### T-FSBL-10　加密分区检测
- **目的**：`DataWordLen != ImageWordLen` 判定为加密，拒绝加载。
- **前置**：构造分区头使两个字长字段不等。
- **步骤**：SD 启动。
- **预期**：打印 `Partition is encrypted.`，卡死。
- **通过判据**：出现该提示。
- **备注**：SSBL 是普通 PS ELF，不支持加密分区。

### T-FSBL-11　非 PS 镜像分区
- **目的**：分区属性不含 `ATTRIBUTE_PS_IMAGE_MASK` 时拒绝。
- **前置**：构造分区头属性为纯 bitstream。
- **步骤**：SD 启动。
- **预期**：打印 `Partition is not PS App.`，卡死。
- **通过判据**：出现该提示。
- **备注**：分区 1 必须是 SSBL（PS app）。

### T-FSBL-12　分区 LoadAddr 越界（非 DDR）
- **目的**：`LoadAddr` 落在 DDR 范围外时拒绝。
- **前置**：构造分区头 LoadAddr = OCM 地址（如 `0x00000000`）。
- **步骤**：SD 启动。
- **预期**：打印 `Partition is not valid region.`，卡死。
- **通过判据**：出现该提示。
- **备注**：SSBL 必须加载到 DDR（`0x00100000`）。

### T-FSBL-13　Handoff 跳转到 SSBL
- **目的**：`FsblHandoff` → `FsblHandoffExit`（fsbl_handoff.S）正确清 cache/MMU 并跳转。
- **前置**：合法 BOOT.BIN。
- **步骤**：观察启动末尾。
- **预期**：打印 `[5/5] Handoff to 0x....` 与 `SUCCESSFUL_HANDOFF`，控制权交 SSBL（出现 SSBL 日志）。
- **通过判据**：SSBL 接管，无 `Handoff failed - should not reach here`。
- **备注**：若出现后者说明跳转汇编失败（cache/MMU 清理顺序问题）。

### T-FSBL-14　异常注入（Undef / Data Abort）
- **目的**：触发未定义指令 / 数据访问异常时进入对应 handler。
- **前置**：JTAG 在线，临时 patch 一段非法指令或非法访存。
- **步骤**：单步执行到异常点。
- **预期**：打印 `UNDEFINED_HANDLER` 或 `DATA_ABORT_HANDLER`，卡死。
- **通过判据**：打印对应 handler 名且不跑飞。
- **备注**：回归用，确认 RegisterHandlers 真正生效；需要调试器配合。

---

## 4. SSBL 启动测试项（new_ssbl）

> 入口：`new_ssbl/src/main.c`。`bsp_init → tx_kernel_enter → tx_application_define`（只建 AppTaskStart）→ AppTaskStart 做 FileX/bitstream_init/AppTaskCreate/AppObjCreate + 倒计时 + 自动启动决策。

### T-SSBL-01　ThreadX 内核启动
- **目的**：`tx_kernel_enter` 成功调度 AppTaskStart。
- **前置**：FSBL 已 handoff。
- **步骤**：观察 SSBL 日志。
- **预期**：出现 AppTaskStart 的初始化日志（FileX 挂载等）。
- **通过判据**：进入 AppTaskStart，不死在 `main` 的 `while(1)`。
- **备注**：死在 while 说明 tx_kernel_enter 失败。

### T-SSBL-02　FileX SD 介质挂载
- **目的**：SD boot mode 下 `fx_media_open` 成功。
- **前置**：boot mode = SD，SD 卡 FAT32。
- **步骤**：观察启动日志。
- **预期**：打印 `fx_media_open sdio.` 与 `read boot.cfg`。
- **通过判据**：出现 sdio 挂载成功。
- **备注**：失败打印 `fx_media_open error.`，但流程继续（main.c 不阻断）。

### T-SSBL-03　FileX QSPI 介质挂载
- **目的**：QSPI boot mode 下 `fx_media_open` 成功。
- **前置**：boot mode = QSPI，QSPI 含 FAT 镜像。
- **步骤**：观察启动日志。
- **预期**：打印 `fx_media_open qspi flash.` 与 `read boot.cfg`。
- **通过判据**：出现 qspi 挂载成功。
- **备注**：无 QSPI 硬件标 `[B]`。

### T-SSBL-04　boot.cfg 正常读取
- **目的**：`boot_config_load` 解析合法 boot.cfg。
- **前置**：SD 根目录有合法 boot.cfg（含 app/bit/boot_delay/auto_boot 四键）。
- **步骤**：观察启动日志。
- **预期**：打印 `fx_file_read len = N` 与 `app=... bit=... delay=...s auto=...`。
- **通过判据**：四字段值与文件一致。
- **备注**：解析支持 `#` 注释、行首空白、行尾 `\r`；见 boot_config.c:parse_text。

### T-SSBL-05　boot.cfg 缺失回退 .bak
- **目的**：boot.cfg 不存在时回退 boot.cfg.bak。
- **前置**：删除 boot.cfg，保留 boot.cfg.bak（内容合法）。
- **步骤**：上电启动。
- **预期**：正常读到 .bak，打印配置值，不报 `fx_file_open error!`。
- **通过判据**：配置成功加载且 auto/boot 行为符合 .bak 内容。
- **备注**：这是 save 原子提交的掉电恢复机制（boot_config.c:30-35）。

### T-SSBL-06　boot.cfg 与 .bak 均缺失
- **目的**：两者都缺失时 `boot_config_load` 返回 -1，配置全 0。
- **前置**：删除 boot.cfg 与 boot.cfg.bak。
- **步骤**：上电启动。
- **预期**：打印 `fx_file_open error!`；g_runtime_cfg 全 0（app_name 空、auto_boot=0）。
- **通过判据**：打印错误但不死机，后续按 auto_boot=0 直进 CLI。
- **备注**：验证容错——配置缺失不应阻塞 Shell。

### T-SSBL-07　bitstream_init 成功
- **目的**：devcfg 实例 + PS→PL 电平转换器初始化（幂等）。
- **前置**：进入 AppTaskStart。
- **步骤**：观察日志。
- **预期**：打印 `devcfg + level shifter ready`。
- **通过判据**：出现该行，无 `BIT_ERR_INIT`。
- **备注**：重复调用幂等（s_ready 标志）；后续 bit 用例前置依赖此项。

### T-SSBL-08　触发 — KEY0 短按（消抖）
- **目的**：KEY0 有效按下经 20ms 消抖后置 PROCESSED flag。
- **前置**：auto_boot=1，boot_delay 足够长；进入倒计时窗口。
- **步骤**：在倒计时内短按 KEY0 一次。
- **预期**：打印 `Trigger: KEY0 verified.`；进 CLI（打印 `Trigger fired, entering CLI.`）。
- **通过判据**：Shell 被激活，倒计时中止。
- **备注**：AppTaskTrig 先 `tx_thread_sleep(2)`（≈20ms）再读键值确认。

### T-SSBL-09　触发 — KEY0 抖动忽略
- **目的**：按键抖动（短脉冲）不被误判为有效触发。
- **前置**：同 T-SSBL-08。
- **步骤**：快速轻触（产生 <20ms 抖动）。
- **预期**：消抖后 `key_Read` 读到 0，不打印 verified，倒计时继续。
- **通过判据**：未进 CLI，最终按超时路径走。
- **备注**：硬件按键特性相关；抖动过于接近判定阈值可能不稳定。

### T-SSBL-10　触发 — UART 魔数 0xDEADBEEF
- **目的**：串口收到 `DE AD BE EF` 字节序列后触发。
- **前置**：auto_boot=1，倒计时窗口内。
- **步骤**：从串口终端发送 4 字节 `0xDE 0xAD 0xBE 0xEF`。
- **预期**：打印 `Trigger: UART magic verified.` → 进 CLI。
- **通过判据**：魔数匹配后进 CLI。
- **备注**：用滑动窗口匹配，字节可嵌入任意数据流中（task_trigger.c:80-89）。

### T-SSBL-11　Shell 任务激活（信号量）
- **目的**：`shell_active_semaphore` 被 put 后 AppTaskShell 唤醒。
- **前置**：任意触发路径（或 auto_boot=0）。
- **步骤**：观察日志。
- **预期**：打印 `Shell task activated!` 并出现 shell 提示符。
- **通过判据**：出现提示符，可输入命令。
- **备注**：shell 优先级 20（最低），trigger 优先级 10。

### T-SSBL-12　自动启动 auto_boot=0 直接进 CLI
- **目的**：auto_boot_flag=0 时不倒计时，立即激活 Shell。
- **前置**：boot.cfg `auto_boot = no`。
- **步骤**：上电启动。
- **预期**：打印 `auto_boot=0, entering CLI.` → Shell 激活；不调用 boot_run。
- **通过判据**：无倒计时、无自动 boot 尝试。
- **备注**：main.c:154-159 分支。

### T-SSBL-13　自动启动 auto_boot=1 + trigger 打断进 CLI
- **目的**：倒计时窗口内 trigger 验证通过则进 CLI（不等满延时）。
- **前置**：auto_boot=1，boot_delay=10s。
- **步骤**：上电后 <10s 内按 KEY0 或发魔数。
- **预期**：`tx_event_flags_get` 提前返回 TX_SUCCESS → 打印 `Trigger fired, entering CLI.` → Shell 激活。
- **通过判据**：未执行 boot_run 即进 CLI，且耗时 < boot_delay。
- **备注**：事件等待用 `TX_OR_CLEAR` + 超时 = `boot_delay_seconds*100` tick。

### T-SSBL-14　自动启动 auto_boot=1 超时自动 boot 成功
- **目的**：倒计时到点未触发 → 自动 boot_run 成功跳转 app。
- **前置**：auto_boot=1，boot_delay=5s，app.bin/pl_a.bit 合法且可观测。
- **步骤**：上电后不操作，等满延时。
- **预期**：打印 `boot: bit ... programmed` → `handoff to app @0x01000000` → app 现象（如 LED 闪）。
- **通过判据**：SSBL 不再返回，app 可观测现象出现。
- **备注**：boot_run 成功不返回（jump_to_app）。

### T-SSBL-15　自动启动 auto_boot=1 超时 boot 失败回退 CLI
- **目的**：自动 boot_run 返回非 0 时回退 CLI。
- **前置**：auto_boot=1，boot_delay=5s，故意把 app_name 设为不存在文件。
- **步骤**：等满延时。
- **预期**：打印 `load app ... failed` → `Auto-boot failed (-3), falling back to CLI.` → Shell 激活。
- **通过判据**：返回 CLI 可操作，不死机。
- **备注**：main.c:145-151；boot_run 返回 -1/-2/-3 均回退。

---

## 5. SSBL Loader 测试项

### T-LOAD-01　boot_run 正常（app + bit）
- **目的**：`boot_run(app,bit)` 先配 PL 再加载 app 再跳转。
- **前置**：合法 pl_a.bit + app.bin。
- **步骤**：CLI `boot app.bin pl_a.bit`。
- **预期**：打印 bit programmed → app loaded → `handoff to app`，进入 app。
- **通过判据**：PL LED 亮 + app 现象出现。
- **备注**：成功不返回。

### T-LOAD-02　boot_run bit 加载失败（返回 -1）
- **目的**：bitstream_load_file 失败时 boot_run 返回 -1。
- **前置**：bit 文件不存在或越限。
- **步骤**：CLI `boot app.bin nothere.bit`。
- **预期**：打印 `load bit ... failed` → `boot failed (-1)`。
- **通过判据**：返回 -1，不跳转。
- **备注**：cmd_boot 收到非 0 返回打印 `boot failed (rc)`。

### T-LOAD-03　boot_run bit 烧写失败（返回 -2）
- **目的**：bitstream_program 失败时 boot_run 返回 -2。
- **前置**：提供签名错误或 payload 异常的 .bit。
- **步骤**：CLI `boot app.bin bad.bit`。
- **预期**：打印 `program bit ... failed` → `boot failed (-2)`。
- **通过判据**：返回 -2，PL 未配置。
- **备注**：对应 BIT_ERR_FORMAT / FABRIC / DMA / TIMEOUT / PL 等。

### T-LOAD-04　boot_run app 加载失败（返回 -3）
- **目的**：app_load_file 失败时 boot_run 返回 -3。
- **前置**：bit 合法但 app 不存在。
- **步骤**：CLI `boot nothere.bin pl_a.bit`。
- **预期**：bit 先 programmed → `load app ... failed` → `boot failed (-3)`。
- **通过判据**：返回 -3。
- **备注**：注意 bit 已烧写但 app 失败，PL 状态已改变。

### T-LOAD-05　boot_run 仅 app 无 bit
- **目的**：bit_path 为 NULL 或空时不配 PL，仅加载 app。
- **前置**：cfg 中 bit=none，或 `boot app.bin`（单参）。
- **步骤**：CLI `boot app.bin`。
- **预期**：跳过 bit 步骤，直接 `load app` → handoff。
- **通过判据**：仅加载 app 并跳转。
- **备注**：cmd_boot bit 缺省取 g_runtime_cfg.bit_name，为空则传 NULL。

### T-LOAD-06　app_loader 正常加载
- **目的**：app_load_file 把 bin 读到 `0x01000000`。
- **前置**：合法 app.bin。
- **步骤**：通过 boot 间接测，或临时调用。
- **预期**：打印 `<file> loaded: N bytes @0x01000000`。
- **通过判据**：字节数与文件一致，地址正确。
- **备注**：不解析/不校验/不跳转。

### T-LOAD-07　app_loader path 非法（APP_ERR_INVAL = -1）
- **目的**：path == NULL 或空串返回 INVAL。
- **前置**：—。
- **步骤**：构造空路径（CLI `boot ""` 触发）。
- **预期**：返回 -1，不打开文件。
- **通过判据**：无 FileX 调用，直接返回。
- **备注**：app_loader.c:42。

### T-LOAD-08　app_loader 文件不存在（APP_ERR_IO = -2）
- **目的**：fx_file_open 失败返回 IO。
- **前置**：app.bin 不存在。
- **步骤**：CLI `boot nothere.bin`。
- **预期**：打印 `fx_file_open ... failed` → 返回 -2。
- **通过判据**：返回 IO 错误码。
- **备注**：read 中途失败同样返回 IO。

### T-LOAD-09　app_loader 空文件（APP_ERR_EMPTY = -3）
- **目的**：读到 0 字节返回 EMPTY。
- **前置**：empty.bin（0 字节）。
- **步骤**：CLI `boot empty.bin`。
- **预期**：打印 `empty / unreadable` → 返回 -3。
- **通过判据**：返回 EMPTY。
- **备注**：total==0 判定。

### T-LOAD-10　app_loader 超过 4MB（APP_ERR_TOO_BIG = -4）
- **目的**：文件 > APP_MAX_SIZE(4MB) 返回 TOO_BIG。
- **前置**：big.bin（>4MB）。
- **步骤**：CLI `boot big.bin`。
- **预期**：打印 `too big (>4194304)` → 返回 -4。
- **通过判据**：返回 TOO_BIG。
- **备注**：读满上限仍未 EOF 即判越限。

### T-LOAD-11　bitstream_program 正常烧写
- **目的**：含头 .bit 经剥头/反序/PROG_B/PCAP/DONE 成功配置 PL。
- **前置**：bitstream_init 已成功；合法 pl_a.bit。
- **步骤**：通过 `boot app.bin pl_a.bit`。
- **预期**：打印 `PL configured OK (DONE asserted)`；PL LED 亮。
- **通过判据**：PL DONE 拉高 + 可观测现象。
- **备注**：字节反序错误会导致 DMA done 但 PL DONE 不拉高。

### T-LOAD-12　bitstream_program 非 .bit 签名（BIT_ERR_FORMAT = -3）
- **目的**：缺 `00 09 0F F0` 签名返回 FORMAT，不回退 raw。
- **前置**：bad.bit（签名被改）。
- **步骤**：烧写 bad.bit。
- **预期**：打印 `not a .bit file (bad signature)` → 返回 -3。
- **通过判据**：返回 FORMAT，PL 不变。
- **备注**：bitstream_loader.c:214-218 明确不回退 raw。

### T-LOAD-13　bitstream_program 未初始化（BIT_ERR_INIT = -1）
- **目的**：未调 bitstream_init 直接 program 返回 INIT。
- **前置**：构造跳过 init 的路径（调试场景）。
- **步骤**：在 s_ready=0 时调 program。
- **预期**：打印 `not initialized` → 返回 -1。
- **通过判据**：返回 INIT。
- **备注**：正常启动流不会触发；属防御性用例。

### T-LOAD-14　bitstream_program buf 未 4 字节对齐（BIT_ERR_INVAL = -2）
- **目的**：buf 地址非 4 字节对齐或 len<20 返回 INVAL。
- **前置**：构造未对齐地址。
- **步骤**：调试调用。
- **预期**：打印 `buf not 4-byte aligned` → 返回 -2。
- **通过判据**：返回 INVAL。
- **备注**：staging 区基址 `0x00800000` 已对齐；正常路径不触发。

### T-LOAD-15　bitstream_program 剥头后空（BIT_ERR_EMPTY = -4）
- **目的**：.bit 头之后 payload 为 0 字节返回 EMPTY。
- **前置**：构造只含头、payload 为空的 .bit。
- **步骤**：烧写。
- **预期**：打印 `empty payload after header` → 返回 -4。
- **通过判据**：返回 EMPTY。
- **备注**：payload<1 word 同样返回 EMPTY。

### T-LOAD-16　bitstream_load_file 正常
- **目的**：.bit 原始字节读到 staging `0x00800000`，不触发配置。
- **前置**：合法 pl_a.bit。
- **步骤**：boot_run 内部首步（可观测日志）。
- **预期**：打印 `<file> loaded: N bytes @0x00800000`。
- **通过判据**：字节数正确，PL 未配置（DONE 未拉高）。
- **备注**：load 与 program 分离设计。

### T-LOAD-17　bitstream_load_file 超过 4MB（BIT_ERR_TOO_BIG = -10）
- **目的**：.bit > BITSTREAM_DDR_SIZE(4MB) 返回 TOO_BIG。
- **前置**：big.bit（>4MB）。
- **步骤**：加载。
- **预期**：打印 `too big (>4194304)` → 返回 -10。
- **通过判据**：返回 TOO_BIG。
- **备注**：staging 区 4MB 上限。

### T-LOAD-18　bitstream_load_file 文件不存在（BIT_ERR_IO = -9）
- **目的**：fx_file_open 失败返回 IO。
- **前置**：文件不存在。
- **步骤**：加载。
- **预期**：打印 `fx_file_open ... failed` → 返回 -9。
- **通过判据**：返回 IO。
- **备注**：空文件也返回 IO（boot_config 区分，loader 统一 IO）。

### T-LOAD-19　boot_config save 原子提交（.tmp → .bak → cfg）
- **目的**：`cfg save` 经 .tmp → rename(cfg→.bak) → rename(tmp→cfg) 原子提交。
- **前置**：CLI 可用。
- **步骤**：`cfg set app=newapp.bin` → `cfg save` → 重启读 cfg。
- **预期**：打印 `cfg saved`；重启后 status 显示新值；存储中 boot.cfg 完整，残留 .bak 可被回退。
- **通过判据**：保存后配置持久化且可读。
- **备注**：先 fx_media_flush 落盘再 rename（boot_config.c:138）。

### T-LOAD-20　boot_config save 掉电恢复
- **目的**：rename 各阶段掉电后 load 仍能恢复一份完整配置。
- **前置**：可控制掉电时机。
- **步骤**：1) 在 rename(cfg→bak) 后掉电 → 重启应回退 .bak；2) 在 rename(tmp→cfg) 后掉电 → 重启读新 cfg。
- **预期**：两种情形 load 都拿到完整（非半截）配置。
- **通过判据**：无截断/损坏配置被使用。
- **备注**：核心容错机制，建议重点回归。

### T-LOAD-21　jump_to_app 跳转（cache 清理）
- **目的**：jump_to_app 跳前清 D-cache/L2，保证 app 镜像一致后跳转。
- **前置**：合法 app 已加载。
- **步骤**：boot_run 末步。
- **预期**：成功进入 app，不返回。
- **通过判据**：app 现象出现。
- **备注**：handoff.c / handoff_exit.S。

---

## 6. SSBL Shell 命令测试项

> 命令表见 shell_cmd.c:524-533。letter shell，argc/argv。

### T-SHELL-01　info
- **目的**：显示版本 / 介质 / 各区地址 / 当前 app/bit 大小。
- **步骤**：CLI `info`。
- **预期**：输出 `SSBL version: new_ssbl V1.0`、media 行、`load addr: app=0x01000000 ssbl=0x00100000`、`staging area: bit=0x00800000 size=4MB`、app/bit 行。
- **通过判据**：地址常量与 1.2 节一致。
- **备注**：app/bit 未配置时显示 `(none)`。

### T-SHELL-02　status
- **目的**：单行显示当前 cfg。
- **步骤**：CLI `status`。
- **预期**：`cfg: app=... bit=... delay=...s auto=...`。
- **通过判据**：四字段与 g_runtime_cfg 一致。
- **备注**：bit 为空显示 `(none)`。

### T-SHELL-03　ls 根目录
- **目的**：列出根目录条目。
- **步骤**：CLI `ls`。
- **预期**：逐行打印文件/目录名。
- **通过判据**：含 BOOT.BIN / boot.cfg 等已知文件。
- **备注**：无参默认 `/`。

### T-SHELL-04　ls 子目录
- **目的**：列出指定子目录。
- **步骤**：CLI `ls /sub`。
- **预期**：列出该目录内容或 `ls /sub failed (0x..)`。
- **通过判据**：成功列出或按错误码提示。
- **备注**：用 fx_directory_local_path_set。

### T-SHELL-05　ls 不存在目录
- **目的**：非法路径报错不崩溃。
- **步骤**：CLI `ls /nothere`。
- **预期**：`ls /nothere failed (0x..)`。
- **通过判据**：返回提示符，不死机。
- **备注**：—。

### T-SHELL-06　cat 正常
- **目的**：打印文件内容。
- **步骤**：CLI `cat boot.cfg`。
- **预期**：输出文件全文 + 末尾换行。
- **通过判据**：内容完整。
- **备注**：512 字节分块读。

### T-SHELL-07　cat 不存在文件
- **目的**：open 失败提示。
- **步骤**：CLI `cat nothere.txt`。
- **预期**：`cat: open nothere.txt failed (0x..)`。
- **通过判据**：返回提示符。
- **备注**：—。

### T-SHELL-08　cat 无参数（usage）
- **目的**：缺参数打印用法。
- **步骤**：CLI `cat`。
- **预期**：`usage: cat <file>`。
- **通过判据**：打印 usage，无文件操作。
- **备注**：—。

### T-SHELL-09　rm 普通文件
- **目的**：删除普通文件。
- **前置**：存在可删的临时文件。
- **步骤**：CLI `rm tmp.txt`。
- **预期**：`deleted tmp.txt`。
- **通过判据**：ls 不再出现该文件。
- **备注**：大小写不敏感比较文件名。

### T-SHELL-10　rm BOOT.BIN（拒绝）
- **目的**：安全边界——拒删 BOOT.BIN（含大小写变体）。
- **步骤**：CLI `rm BOOT.BIN` 与 `rm boot.bin`。
- **预期**：`ERROR: refuse to delete BOOT.BIN`。
- **通过判据**：文件未被删，ls 仍存在。
- **备注**：shell_cmd.c:198 name_eq_ci。

### T-SHELL-11　rm boot.cfg（二次确认 → y）
- **目的**：删关键文件需确认。
- **步骤**：CLI `rm boot.cfg` → 提示后输 `y`。
- **预期**：`Delete critical file, confirm? [y/N]` → `deleted boot.cfg`。
- **通过判据**：确认后才删。
- **备注**：当前 app / bit 同样需确认。

### T-SHELL-12　rm boot.cfg（二次确认 → n）
- **目的**：取消删除。
- **步骤**：CLI `rm boot.cfg` → 输 `n`（或直接回车）。
- **预期**：`cancelled`；文件保留。
- **通过判据**：文件未删。
- **备注**：回车/任意非 y 默认取消。

### T-SHELL-13　rm 当前 app（二次确认）
- **目的**：删 g_runtime_cfg.app_name 命中的文件需确认。
- **步骤**：CLI `rm <当前app名>` → y/n。
- **预期**：触发确认提示。
- **通过判据**：同 T-SHELL-11/12 逻辑。
- **备注**：bit_name 非空时同样判。

### T-SHELL-14　rm 不存在文件
- **目的**：删除失败提示。
- **步骤**：CLI `rm nothere.txt`。
- **预期**：`rm nothere.txt failed (0x..)`。
- **通过判据**：返回提示符。
- **备注**：—。

### T-SHELL-15　mv 正常
- **目的**：重命名文件。
- **前置**：存在 old.txt。
- **步骤**：CLI `mv old.txt new.txt`。
- **预期**：无错误；ls 出现 new.txt 消失 old.txt。
- **通过判据**：重命名生效。
- **备注**：fx_file_rename。

### T-SHELL-16　mv 无参数（usage）
- **目的**：缺参打印用法。
- **步骤**：CLI `mv`。
- **预期**：`usage: mv <old> <new>`。
- **通过判据**：打印 usage。
- **备注**：需 argc>=3。

### T-SHELL-17　cfg show
- **目的**：显示四键。
- **步骤**：CLI `cfg` 或 `cfg show`。
- **预期**：`app=...` / `bit=...` / `boot_delay=...` / `auto_boot=...`。
- **通过判据**：四行齐全。
- **备注**：—。

### T-SHELL-18　cfg set app=xxx
- **目的**：设置 app 名（仅内存，未 save）。
- **步骤**：CLI `cfg set app=newapp.bin`。
- **预期**：`OK`；status 显示新值。
- **通过判据**：内存 cfg 已改。
- **备注**：超长截断到 63 字节。

### T-SHELL-19　cfg set bit=none（清空）
- **目的**：bit 设为 none 或空串时清空。
- **步骤**：CLI `cfg set bit=none`。
- **预期**：`OK`；bit 显示空。
- **通过判据**：bit_name 清零。
- **备注**：shell_cmd.c:266 区分 none/空。

### T-SHELL-20　cfg set boot_delay=N
- **目的**：设置倒计时秒数。
- **步骤**：CLI `cfg set boot_delay=8`。
- **预期**：`OK`；status delay=8s。
- **通过判据**：strtoul 解析正确。
- **备注**：非数字按 strtoul 规则。

### T-SHELL-21　cfg set auto_boot=yes/no
- **目的**：切换自动启动。
- **步骤**：CLI `cfg set auto_boot=yes` / `=no`。
- **预期**：`OK`；status auto=yes/no。
- **通过判据**：仅 yes 为 1，其余为 0。
- **备注**：—。

### T-SHELL-22　cfg set 未知 key
- **目的**：非法 key 提示。
- **步骤**：CLI `cfg set foo=bar`。
- **预期**：`unknown key: foo` + `valid keys: ...`。
- **通过判据**：不修改任何字段。
- **备注**：合法 key：app/bit/boot_delay/auto_boot。

### T-SHELL-23　cfg save
- **目的**：持久化到 boot.cfg（原子提交）。
- **步骤**：CLI `cfg save`。
- **预期**：`cfg saved`；重启后保留。
- **通过判据**：掉电重启后配置仍在。
- **备注**：详见 T-LOAD-19/20。

### T-SHELL-24　boot [app] [bit] 正常
- **目的**：手动指定 app/bit 启动。
- **步骤**：CLI `boot app.bin pl_a.bit`。
- **预期**：同 T-LOAD-01。
- **通过判据**：成功进 app。
- **备注**：成功不返回。

### T-SHELL-25　boot 无参（用默认 cfg）
- **目的**：省略参数时用 g_runtime_cfg。
- **步骤**：CLI `boot`。
- **预期**：app 取 cfg.app_name；bit 取 cfg.bit_name（空则不配 PL）。
- **通过判据**：行为与 cfg 一致。
- **备注**：—。

### T-SHELL-26　boot 失败
- **目的**：boot_run 失败打印返回码。
- **步骤**：CLI `boot nothere.bin`。
- **预期**：`boot failed (-3)`。
- **通过判据**：返回 CLI，可重试。
- **备注**：—。

### T-SHELL-27　reset 软复位
- **目的**：写 SLCR_PSS_RST_CTRL 触发 PS 软复位。
- **步骤**：CLI `reset`。
- **预期**：`Resetting...` → 系统重启 → 重新走 FSBL。
- **通过判据**：重新出现 FSBL `[1/5]` 日志。
- **备注**：先 unlock SLCR（0xDF0D）再置复位位。

---

## 7. YMODEM 传输测试

> 命令 `ym load`（收 → DDR `0x02000000`）/ `ym save`（DDR → 存储文件）。协议机见 ymodem.c / ymodem.h。

### T-YM-01　ym load 正常接收小文件
- **目的**：完整接收一个小文件到 DDR。
- **前置**：CLI 可用，终端支持 YMODEM-CRC。
- **步骤**：CLI `ym load` → 终端发起 YMODEM 发送 small.bin。
- **预期**：`ym load: waiting for YMODEM transfer...` → `YMODEM: receiving 'small.bin' (N bytes)...` → `YMODEM: received N bytes` → `ym load: OK, 'small.bin' in DDR @0x02000000 (N bytes)`。
- **通过判据**：收到的字节数与源文件一致。
- **备注**：包0 解析 filename\0 size\0。

### T-YM-02　ym load 接收接近 8MB 大文件
- **目的**：大数据量分包接收正确。
- **前置**：准备 ~7.5MB 文件。
- **步骤**：`ym load` + 发送。
- **预期**：完整接收，字节数匹配。
- **通过判据**：字节数一致，无 CRC/SEQ 错。
- **备注**：验证 STX(1024B) 包路径。

### T-YM-03　ym load 超过 8MB（缓冲溢出 CAN）
- **目的**：offset+len 超 `YM_DDR_MAX_SZ` 时发 CAN 终止。
- **前置**：>8MB 文件。
- **步骤**：`ym load` + 发送。
- **预期**：`YMODEM: buffer overflow! offset=... len=...` → 终止，返回错误码。
- **通过判据**：传输被取消，返回提示符。
- **备注**：shell_cmd.c:380 ym_cb_data。

### T-YM-04　ym load 握手超时
- **目的**：60s 内无发送方应答 → 超时失败。
- **前置**：发起 `ym load` 后不发送。
- **步骤**：等待 >60s。
- **预期**：`ym load: failed (err=0x..)`（YM_ERR_TMO=0x70）。
- **通过判据**：超时返回，不死等。
- **备注**：handshake_timeout=60（shell_cmd.c:436）。

### T-YM-05　ym load CRC 错误
- **目的**：接收中 CRC 校验失败发 NAK/终止。
- **前置**：人为制造 CRC 不符的包。
- **步骤**：发送错误 CRC 的包。
- **预期**：触发 YM_ERR_CRC(0x73) 处理。
- **通过判据**：失败返回。
- **备注**：需能注入错误帧的发送工具。

### T-YM-06　ym load 用户取消（CAN）
- **目的**：发送方连发 CAN 取消传输。
- **步骤**：传输中终端点"取消"。
- **预期**：YM_ERR_CAN(0x75)，`ym load: failed`。
- **通过判据**：正常退出。
- **备注**：—。

### T-YM-07　ym save 正常保存
- **目的**：把 DDR 暂存数据写成存储文件。
- **前置**：已 `ym load` 成功。
- **步骤**：CLI `ym save`。
- **预期**：`ym save: writing N bytes (... free)` → `wrote N bytes to '<name>'`。
- **通过判据**：ls 出现文件且 cat/大小一致。
- **备注**：先查 free space，已存在文件先删再建。

### T-YM-08　ym save 未先 load
- **目的**：无暂存数据时拒绝。
- **步骤**：未 load 直接 `ym save`。
- **预期**：`ym save: no file loaded, run 'ym load' first`。
- **通过判据**：返回 -1，无写操作。
- **备注**：g_ym_recv.loaded=0 判定。

### T-YM-09　ym save 存储空间不足
- **目的**：文件大于剩余空间时拒绝。
- **前置**：暂存数据 > 介质空闲。
- **步骤**：`ym save`。
- **预期**：`ym save: file too large! (... > ... free)`。
- **通过判据**：返回 -1，未写。
- **备注**：fx_media_space_available 查询。

### T-YM-10　ym 未知子命令
- **目的**：非法子命令提示用法。
- **步骤**：CLI `ym foo`。
- **预期**：`ym: unknown subcommand 'foo'` + usage。
- **通过判据**：返回 -1。
- **备注**：合法子命令 load / save。

---

## 8. 异常与边界测试

### T-EX-01　串口中文 / 特殊字符显示
- **目的**：xil_printf 输出中文/特殊字符不乱码不卡死。
- **步骤**：通过 cat 含中文文件 / 触发含特殊字符的提示。
- **预期**：终端按 UTF-8/GBK 正常显示（取决于终端编码）。
- **通过判据**：无死机；显示与编码一致。
- **备注**：编码以终端设置为准。

### T-EX-02　长文件名
- **目的**：接近 FX_MAX_LONG_NAME_LEN 的文件名可列/可操作。
- **步骤**：放入长名文件，`ls`/`cat`。
- **预期**：完整显示，不截断越界。
- **通过判据**：无内存越界（看是否跑飞）。
- **备注**：name 缓冲 = FX_MAX_LONG_NAME_LEN+1。

### T-EX-03　存储满后写入
- **目的**：fx_file_write 在满介质上的失败处理。
- **前置**：填满 SD 卡。
- **步骤**：`ym save` 大文件 / `cfg save`。
- **预期**：对应错误码提示，不损坏现有文件。
- **通过判据**：失败返回，已有文件完好。
- **备注**：cfg save 写 .tmp 失败会清理 .tmp 不影响 boot.cfg。

### T-EX-04　反复 reset 稳定性
- **目的**：连续软复位 N 次仍能正常启动。
- **步骤**：循环 `reset` 20 次。
- **预期**：每次都能完整走 FSBL→SSBL。
- **通过判据**：无一次卡死/启动失败。
- **备注**：验证复位后寄存器/介质状态干净。

### T-EX-05　全链路 FSBL→SSBL→app→reset→FSBL
- **目的**：完整启动链闭环。
- **步骤**：上电 → 自动 boot 进 app → app 中（或终端）触发 reset → 重新 FSBL。
- **预期**：每个环节按序，app 跑→复位→FSBL 重来。
- **通过判据**：闭环无异常。
- **备注**：冒烟核心用例之一。

---

## 9. 冒烟回归清单（每次构建后必跑）

> 最小子集，快速判定整体可用性。任一失败即阻塞发布。

| 编号 | 用例 | 关键判据 |
|---|---|---|
| S1 | T-FSBL-04 SD 启动到 handoff | 出现 `SUCCESSFUL_HANDOFF` |
| S2 | T-SSBL-02 FileX SD 挂载 | 出现 `fx_media_open sdio.` |
| S3 | T-SSBL-12 auto_boot=0 进 CLI | 出现 Shell 提示符 |
| S4 | T-SHELL-01 info | 地址常量正确 |
| S5 | T-SHELL-03 ls | 列出 BOOT.BIN |
| S6 | T-LOAD-01 boot app+bit | PL LED 亮 + 进 app |
| S7 | T-SHELL-27 reset | 能重启回 FSBL |

---

## 附录 A：错误码速查

### FSBL（提示串，无枚举）
| 提示串 | 触发点 |
|---|---|
| `Unsupported boot mode` | main.c switch default |
| `SD/QSPI initialization failed` | InitSD/InitQspi |
| `IMAGE_HAS_NO_PARTITIONS` | IsEmptyHeader |
| `PARTITION_HEADER_CORRUPTION` | ValidatePartitionHeaderChecksum |
| `INVALID_PARTITION_LENGTH` | ImageWordLen 超限 |
| `Partition is encrypted.` | DataWordLen != ImageWordLen |
| `Partition is not PS App.` | 属性缺 PS_IMAGE_MASK |
| `Partition is not valid region.` | LoadAddr 越 DDR |
| `Handoff failed - should not reach here` | 跳转汇编异常 |

### SSBL app_err_t
| 码 | 值 | 含义 |
|---|---|---|
| APP_OK | 0 | 成功 |
| APP_ERR_INVAL | -1 | path 空/NULL |
| APP_ERR_IO | -2 | FileX open/read/close 失败 |
| APP_ERR_EMPTY | -3 | 文件 0 字节 |
| APP_ERR_TOO_BIG | -4 | > 4MB |

### SSBL bit_err_t
| 码 | 值 | 含义 |
|---|---|---|
| BIT_OK | 0 | 成功 |
| BIT_ERR_INIT | -1 | devcfg/电平转换器 |
| BIT_ERR_INVAL | -2 | 参数非法 |
| BIT_ERR_FORMAT | -3 | 非 .bit 签名 |
| BIT_ERR_EMPTY | -4 | 剥头后空 |
| BIT_ERR_FABRIC | -5 | PCFG_INIT 未就绪 |
| BIT_ERR_DMA | -6 | XDcfg_Transfer 失败 |
| BIT_ERR_TIMEOUT | -7 | DMA/DONE 超时 |
| BIT_ERR_PL | -8 | DMA done 但 PL DONE 未拉高 |
| BIT_ERR_IO | -9 | FileX 读失败 |
| BIT_ERR_TOO_BIG | -10 | > 4MB staging |

### SSBL boot_run 返回
| 返回 | 含义 |
|---|---|
| -1 | bit 加载失败 |
| -2 | bit 烧写失败 |
| -3 | app 加载失败 |
| 不返回 | 成功跳转 |

### YMODEM（ym_code）
| 码 | 值 | 含义 |
|---|---|---|
| YM_ERR_TMO | 0x70 | 握手超时 |
| YM_ERR_CODE | 0x71 | 字节错误 |
| YM_ERR_SEQ | 0x72 | 序号不连续 |
| YM_ERR_CRC | 0x73 | CRC 错误 |
| YM_ERR_DSZ | 0x74 | 数据不足 |
| YM_ERR_CAN | 0x75 | 用户终止 |
| YM_ERR_ACK | 0x76 | 错误应答 |
| YM_ERR_FILE | 0x77 | 传输文件无效 |
