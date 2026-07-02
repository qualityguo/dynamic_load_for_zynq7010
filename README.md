# SSBL — Zynq-7010 二级引导加载器

基于 **ThreadX + FileX + LevelX** 的多任务二级 Bootloader，运行于 Xilinx Zynq-7010
（ARM Cortex-A9）平台。支持 SD 卡 / QSPI Flash 双介质启动、bitstream 动态加载、
app 热加载、YMODEM 串口升级以及交互式 CLI。

---

## 一、启动流程

```
┌──────────┐   上电    ┌──────────┐   加载到 0x100000   ┌──────────┐
│ BOOT.BIN │ ────────► │   FSBL   │ ──────────────────► │   SSBL   │
│ (QSPI/SD)│           │ (一级BL) │                     │ (二级BL) │
└──────────┘           └──────────┘                     └────┬─────┘
                                                              │
                ┌─────────────────────────────────────────────┤
                ▼                                             ▼
        读 boot.cfg → 倒计时                       Trigger(KEY0/串口魔数)
                │                                             │
        ┌───────┴───────┐                                     │
        ▼超时            ▼触发                                 │
   自动 boot_run     进入 CLI shell ◄───────────────────────────┘
   (配PL+加载app)    (info/ls/cfg/boot/ymodem ...)
```

1. **FSBL**：硬件初始化（DDR/clock/MIO），从 `BOOT.BIN` 中解析分区头，把 SSBL 搬运到
   `0x00100000`（1MB 处）并跳转，之后自身不再运行。
2. **SSBL**：进入 ThreadX 调度，`AppTaskStart` 编排全部初始化：
   - 按当前 `BOOT_MODE` 寄存器挂载 FileX 介质（SD / QSPI）；
   - 读取 `boot.cfg` 得到 app/bit 名、倒计时秒数、自动启动标志；
   - `AppTaskTrig` 监听 KEY0 按键与串口魔数（`0x41414141`），命中即打断倒计时进入 CLI；
   - `AppTaskShell` 执行 letter shell 命令。

---

## 二、功能特性

| 模块             | 说明                                                                 |
| ---------------- | ------------------------------------------------------------------- |
| **双介质启动**   | 按 `BOOT_MODE` 寄存器自动选择 SD（`fx_zynq_sd_driver`）或 QSPI（`fx_zynq_qspi_driver`） |
| **LevelX NOR**   | QSPI Flash 经 LevelX 做磨损均衡，首次未格式化时自动 `fx_media_format`  |
| **bitstream 加载** | PCAP DMA 烧写 `.bit` 到 PL，含剥头 + 字节反序 + PROG_B/DONE 轮询（`bitstream_loader`） |
| **app 热加载**   | 把扁平 `.bin` 读入 DDR 固定地址后 `jump_to_app` 跳转交权（`app_loader` / `handoff`） |
| **boot.cfg**     | 文本配置文件，`tmp→bak→cfg` 原子写，任意时刻掉电均可恢复完整配置        |
| **交互 CLI**     | letter shell，命令：`info / status / ls / cat / rm / mv / cfg / boot / reset / ymodem` |
| **YMODEM 升级**  | 串口 YMODEM 接收固件到 DDR，再写存储介质，支持现场更新 app/bit         |
| **触发机制**     | 倒计时窗口内按 KEY0 或发送串口魔数即可进入 CLI，否则超时自动启动       |

---

## 三、DDR 内存布局

| 区域               | 起始地址       | 大小  | 说明                  |
| ------------------ | -------------- | ----- | --------------------- |
| SSBL 运行区        | `0x00100000`   | 1 MB  | 链接脚本指定          |
| bitstream 暂存区   | `0x00800000`   | 4 MB  | `.bit` 读入后烧写 PL  |
| app 加载区         | `0x01000000`   | 4 MB  | app 必须链接到此地址  |
| YMODEM 接收暂存区  | `0x02000000`   | 8 MB  | `ym load` 缓冲        |

---

## 四、目录结构

```
ssbl/
├── ssbl/                     # Vitis 工作区（全部工程）
│   ├── new_fsbl/             # 一级引导 FSBL
│   ├── new_ssbl/             # 二级引导 SSBL（主工程）
│   │   └── src/
│   │       ├── main.c        # 任务/通信编排 + 自动启动决策
│   │       ├── bsp.c         # GIC/Timer/GPIO/UART 初始化
│   │       ├── shell_cmd.c   # CLI 命令实现
│   │       ├── loader/       # boot_run / app / bitstream / ymodem / handoff
│   │       ├── task/         # task_trigger / task_shell
│   │       ├── driver/       # LED / KEY / UART / 环形缓冲
│   │       ├── FileX/        # FileX 源码
│   │       ├── LevelX_NOR/   # LevelX NOR 驱动
│   │       └── ThreadX/      # ThreadX 源码
│   ├── hello_app1/           # 测试 app1
│   ├── hello_app2/           # 测试 app2
│   ├── ThreadX_A9/           # ThreadX 库工程
│   ├── FileX_A9/             # FileX 库工程
│   ├── LevelX_NOR/           # LevelX 库工程
│   └── zynq7010/             # Vivado 硬件平台
├── template/                 # 干净工程模板（含 fsbl/ssbl/scripts）
├── scripts/                  # 构建/链接辅助脚本
└── vivado/                   # Vivado 硬件工程
```

---

## 五、CLI 使用

串口参数：`115200 8N1`。倒计时期间发送 `AAAA`（即魔数 `0x41414141`）或按 KEY0 进入 CLI。

```text
info                      # 查看版本 / 介质 / 加载地址 / 当前配置
status                    # 查看当前 boot.cfg
ls [dir]                  # 列目录
cat <file>                # 查看文件（>8KB 拒绝，防刷屏）
rm <file>                 # 删除文件（boot.cfg/当前 app/bit 需确认，禁止删 BOOT.BIN）
mv <old> <new>            # 重命名
cfg show                  # 查看配置
cfg set app=<file>        # 修改配置（app/bit/boot_delay/auto_boot）
cfg save                  # 保存到 boot.cfg
boot [app] [bit]          # 手动启动（可临时指定文件名）
ym load                   # YMODEM 接收文件到 DDR
ym save                   # 把 DDR 中的文件写入存储介质
reset                     # 软复位
```

---

## 六、后续计划（未完成）

### 网络升级支持

当前升级链路仅支持 **串口 YMODEM**，速率受限于 115200bps，大文件（bit/app 可达数 MB）传输耗时长、
易受干扰中断。计划引入 **以太网在线升级**，作为 SSBL 的第三种固件更新与触发通道。代码中已预留
`TRIGGER_FLAG_NET`（`ssbl.h`）事件位，网络触发可复用现有倒计时→CLI 编排逻辑。

协议栈选型为 **NetXDuo**（Azure RTOS 网络）——与本工程已有的 ThreadX / FileX / LevelX 同属
Azure RTOS 家族，共享同一套线程、互斥锁、信号量、定时器原语，**无需任何 RTOS 适配层**，
且自带 TFTP / DNS / DHCP / HTTP 等协议组件，可直接用于升级场景。

#### 目标能力

1. **NetXDuo 协议栈接入**
   - 以库工程形式集成 NetXDuo（与现有 `ThreadX_A9` / `FileX_A9` / `LevelX_NOR` 并列），
     原生对接 ThreadX 调度与 GIC 中断框架，零同步原语适配；
   - 编写 Zynq GEM（`xemacps`）网络驱动，对接 NetXDuo 通用网络驱动接口
    （`nxdriver`：`nx_driver_entry` → 初始化/发送/接收/中断处理），收发走 BD/DMA；
   - 启用 DHCP 自动获取 IP，或回退静态 IP。

2. **TFTP 客户端下载**
   - 复用 NetXDuo 自带 **TFTP client** 组件，从升级服务器拉取 `app.bin` / `pl.bit` / `boot.cfg`；
   - 下载到现有 DDR 暂存区后，复用 `app_loader` / `bitstream_loader` 完成烧写，保持加载链路不变；
   - 支持下载后自动 `cfg save` + `boot`，或仅下载不启动。

3. **网络触发进 CLI**
   - `AppTaskTrig` 增加 `TRIGGER_FLAG_NET` 监听（开 UDP socket 收魔数包）；
   - 命中后与 KEY0 / 串口魔数同等处理，发 `TRIGGER_FLAG_PROCESSED` 打断倒计时进入 CLI；
   - 支持无 boot.cfg 时从网络拉取兜底配置。

4. **远程命令 / 远程升级**
   - 基于 TCP socket 的简易命令通道，远程执行 `boot / cfg / ls` 等命令；
   - 长远考虑利用 NetXDuo HTTP 组件做固件分发 + 镜像签名校验，保证升级安全。

#### 待解决的难点

| 难点                   | 说明                                                                 |
| ---------------------- | ------------------------------------------------------------------- |
| GEM 网络驱动移植       | `xemacps` BD/DMA 初始化、收发中断对接 NetXDuo `nx_driver_entry` 接口   |
| PHY 驱动与链路检测     | GEM + PHY（如 RTL8211）复位、自协商、链路 up/down 状态轮询与重连       |
| 内存规划               | NetXDuo 缓冲池（Packet Pool）+ BD/DMA 描述符占用 DDR，需避开现有 SSBL/bit/app/ym 区域 |
| 掉电安全               | 网络下载需复用 `tmp→bak→cfg` 原子写流程，防止升级中断导致 brick       |
| 超时与重试             | TFTP/网络链路异常时的超时回退、断点续传策略                            |

#### 规划里程碑

- [ ] M1：NetXDuo 库工程接入，`xemacps` 网络驱动跑通，DHCP/静态 IP + ping 通；
- [ ] M2：NetXDuo TFTP client 拉取文件并写入存储介质，打通下载链路；
- [ ] M3：`TRIGGER_FLAG_NET` 网络魔数触发进 CLI；
- [ ] M4：远程命令通道 + 镜像校验 + 断点续传。

---

## 七、构建说明

- 工具链：Xilinx Vitis / SDK（ARM Cortex-A9 gcc）；
- 硬件平台：`ssbl/zynq7010/`（导出自 Vivado 的 `.xsa`/硬件工程）；
- 依赖库：`ThreadX_A9` / `FileX_A9` / `LevelX_NOR`（源码已包含在 `src/` 下，或以 `.a` 静态库形式链接）；
- SSBL 编译产出 `.elf` 后，与 FSBL、bitstream 一起打包成 `BOOT.BIN` 烧入 QSPI/SD。
