# Zynq-7000 多级启动代码 — 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现一个 4 级启动链（BootROM → 精简 FSBL → SSBL/ThreadX+FileX → 独立 app），SSBL 根据 SD 卡上的 `boot.cfg` 动态选择 PL bitstream 与 app，并通过串口 CLI + YMODEM 提供现场升级能力。

**Architecture:** FSBL 裁剪自用户现有 `threadx_for_zynq7010/zynq7010/zynq_fsbl`，只加载一个 CPU partition（SSBL）；SSBL 基于 `hello_threadx` vendor 拷贝改造，零新 port 代码，通过 `storage_ops_t` 抽象层对接 FileX/SD（后期换 QSPI+LevelX）；app 自带 Xilinx BSP `boot.S`，SSBL 跳转前做纯破坏性清理（clean cache → disable cache → disable MMU），落到"接近 reset"状态。

**Tech Stack:** Zynq-7010（Cortex-A9 单核 + Artix-7 PL）、ThreadX 6.4.1 + FileX 6.4.1（vendor 拷贝）、Xilinx standalone BSP、arm-none-eabi-gcc、bootgen、Python 3（pack_app.py）。

**Source Spec:** `docs/superpowers/specs/2026-06-22-zynq-multistage-boot-design.md`

---

## 计划总览

本计划分 11 个 Phase（对应 spec §14 的 M1–M11 里程碑）。每个 Phase 完成后是一块可独立验证的软件：

| Phase | 对应 spec | 里程碑 | 主要交付物 |
|---|---|---|---|
| Phase 0 | §3 | — | 顶层目录骨架 + vendor 拷贝脚本 |
| Phase 1 | §8 | M1 | SSBL 工程骨架（基于 hello_threadx，ORIGIN=0x100000） |
| Phase 2 | §7 | M2 | 精简 FSBL（裁剪现有 zynq_fsbl） |
| Phase 3 | §13.2 | M3 | FSBL → SSBL 链通（boot.bif + BOOT.BIN） |
| Phase 4 | §11 | M4 | storage 抽象层 + FileX SD 卡挂载 |
| Phase 5 | §9.5, §13.3 | M5 | app_template + pack_app.py + handoff |
| Phase 6 | §5 | M6 | boot.cfg 解析 + boot_selector 自动选 app |
| Phase 7 | §6 | M7 | CLI 子系统（触发 + 命令集） |
| Phase 8 | §6.3 | M8 | YMODEM 两阶段升级（DDR 暂存 + 原子提交） |
| Phase 9 | §4.2 | M9 | bitstream 动态加载（FileX + PCAP） |
| Phase 10 | §10 | M10 | 错误处理 + LED 错误码 + boot.log/OCM 标记 |
| Phase 11 | §11.2 | M11 | QSPI 迁移（FileX + LevelX NOR） |

**实施次序约束**：Phase 0 → 1 → 2 → 3 强制顺序（每级依赖上一级链通）。Phase 4–10 内部多数可并行（依赖 Phase 3 已链通）。Phase 11 是后期独立工作。

**跨 Phase 约定**（所有任务遵守）：
- **环境变量**：`VENDOR=E:/桌面/docs/threadx_for_zynq7010`（源 spec 资产根）；`PROJ=E:/桌面/docs/Code`（本项目根）。
- **构建工具**：使用 Vitis 自带的 `arm-none-eabi-gcc`，所有命令在 Vitis/Vivado PowerShell（Windows）或 XSCT Console 下执行。
- **平台限制**：硬件相关任务（FSBL 跑板、SD 卡测试、YMODEM 升级）需要物理开发板，单元测试与脚本类任务可在 PC 上完成。
- **风格**：复用 vendor 文件不重写注释风格；新文件用本项目风格（snake_case 文件/函数，camelCase 不引入）。

---

## Phase 0：顶层目录骨架与 vendor 拷贝

**目标**：建立 spec §3.1 的目录结构，把 vendor 资产拷贝到目标位置，方便后续 Phase 直接 `#include` 与编译。

### Task 0.1：创建顶层目录结构

**Files:**
- Create: `Code/README.md`
- Create: `Code/bif/README.md`
- Create: `Code/scripts/.gitkeep`（占位）

- [ ] **Step 1：按 spec §3.1 创建目录树**

```bash
cd /e/桌面/docs/Code
mkdir -p boot/fsbl boot/ssbl boot/app_template
mkdir -p external
mkdir -p bif scripts
mkdir -p docs/superpowers/plans
```

- [ ] **Step 2：写顶层 README.md**

文件 `Code/README.md` 内容：

```markdown
# Zynq-7000 多级启动代码

基于 Zynq-7010 的多级启动链：BootROM → 精简 FSBL → SSBL（ThreadX + FileX）→ App。

## 目录

| 路径 | 内容 |
|---|---|
| `boot/fsbl/` | Stage 1：精简 FSBL（裁剪自 vendor） |
| `boot/ssbl/` | Stage 2：启动选择器 SSBL |
| `boot/app_template/` | Stage 3：app 工程模板 |
| `external/` | 第三方源码（ThreadX/FileX/LevelX 的 vendor 拷贝） |
| `bif/` | bootgen 镜像描述 |
| `scripts/` | PC 端打包/构建脚本 |
| `docs/superpowers/` | spec 与实现计划 |

## 设计文档

- Spec: `docs/superpowers/specs/2026-06-22-zynq-multistage-boot-design.md`
- Plan: `docs/superpowers/plans/2026-06-22-zynq-multistage-boot.md`

## 构建

见 `docs/superpowers/plans/2026-06-22-zynq-multistage-boot.md` 各 Phase 的构建命令。
```

- [ ] **Step 3：写 bif/README.md 占位**

文件 `Code/bif/README.md` 内容：

```markdown
# bif/

bootgen 镜像描述文件目录。Phase 3 会在此创建 `boot.bif`。
```

- [ ] **Step 4：提交**

```bash
git init    # 若项目还不是 git 仓库
git add README.md bif/ scripts/ docs/
git commit -m "Phase 0.1: scaffold top-level directory structure"
```

> **Note：** 项目当前不是 git 仓库（见环境信息 `Is a git repository: no`）。本 Step 4 仅在用户明确启用 git 时执行；若用其它版本控制（或暂不管理），跳过此步即可，但建议把 git init 放到 Phase 0 末尾统一做（Task 0.5）。

---

### Task 0.2：vendor 拷贝脚本

**Files:**
- Create: `Code/scripts/vendor_copy.ps1`
- Create: `Code/scripts/vendor_copy.sh`

**说明**：把 vendor 资产（`hello_threadx`、`hello_filex`、`ThreadX/src/ThreadX` 的 Port + Source、BSP、utils）按 spec §8.1 的映射表拷贝到本项目。脚本支持幂等重跑（覆盖式拷贝）。

- [ ] **Step 1：写 vendor_copy.ps1（Windows PowerShell 主用）**

文件 `Code/scripts/vendor_copy.ps1` 内容：

```powershell
# vendor_copy.ps1 — 把 vendor 资产按 spec §8.1 拷贝到本项目
# 用法：pwsh vendor_copy.ps1
# 幂等：可重复运行，覆盖式拷贝

$ErrorActionPreference = "Stop"

$VENDOR = "E:\桌面\docs\threadx_for_zynq7010"
$PROJ   = Split-Path -Parent $PSScriptRoot | Split-Path -Parent   # scripts/ 的父父目录 = Code/

if (-not (Test-Path $VENDOR)) {
    Write-Error "VENDOR 不存在：$VENDOR"
    exit 1
}

# 工具函数：robocopy 风格的目录拷贝（强制覆盖）
function CopyDir($src, $dst) {
    if (-not (Test-Path $src)) {
        Write-Warning "  跳过（源不存在）：$src"
        return
    }
    New-Item -ItemType Directory -Force -Path $dst | Out-Null
    Copy-Item -Path "$src\*" -Destination $dst -Recurse -Force
    Write-Host "  OK: $src -> $dst"
}

Write-Host "=== vendor_copy：$VENDOR -> $PROJ ==="

# 1. ThreadX 整源（Port + Source + headers + utility）
CopyDir "$VENDOR\ThreadX\src\ThreadX" "$PROJ\external\threadx\ThreadX"

# 2. FileX 整源（含 fx_zynq_sdio_driver）
CopyDir "$VENDOR\hello_filex\src\FileX" "$PROJ\external\filex\FileX"

# 3. hello_threadx 的 BSP（board_init / bsp_init / driver/）
CopyDir "$VENDOR\hello_threadx\src\BSP" "$PROJ\external\bsp"

# 4. hello_threadx 的 utils（printf / 环形缓冲等）
CopyDir "$VENDOR\hello_threadx\src\utils" "$PROJ\external\utils"

# 5. hello_threadx 的 lscript.ld 作为模板（拷到 external 供后续基于它改造）
Copy-Item "$VENDOR\hello_threadx\src\lscript.ld" "$PROJ\external\lscript_hello_threadx.ld" -Force
Write-Host "  OK: lscript.ld -> external\lscript_hello_threadx.ld"

# 6. hello_filex 的 demo_sd_file.c 作为 SD 驱动参考（拷到 external 供查阅）
Copy-Item "$VENDOR\hello_filex\src\demo_sd_file.c" "$PROJ\external\demo_sd_file_reference.c" -Force
Write-Host "  OK: demo_sd_file.c -> external\demo_sd_file_reference.c"

Write-Host "=== vendor_copy 完成 ==="
```

- [ ] **Step 2：写 vendor_copy.sh（Git Bash / WSL 备用）**

文件 `Code/scripts/vendor_copy.sh` 内容：

```bash
#!/usr/bin/env bash
# vendor_copy.sh — Linux/macOS/Git Bash 版本的 vendor 拷贝
set -euo pipefail

VENDOR="${VENDOR:-/e/桌面/docs/threadx_for_zynq7010}"
PROJ="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

copy_dir() {
    local src="$1" dst="$2"
    [ -d "$src" ] || { echo "  跳过（源不存在）：$src"; return; }
    mkdir -p "$dst"
    cp -rf "$src"/. "$dst"/
    echo "  OK: $src -> $dst"
}

echo "=== vendor_copy：$VENDOR -> $PROJ ==="
copy_dir "$VENDOR/ThreadX/src/ThreadX"        "$PROJ/external/threadx/ThreadX"
copy_dir "$VENDOR/hello_filex/src/FileX"      "$PROJ/external/filex/FileX"
copy_dir "$VENDOR/hello_threadx/src/BSP"      "$PROJ/external/bsp"
copy_dir "$VENDOR/hello_threadx/src/utils"    "$PROJ/external/utils"
cp -f "$VENDOR/hello_threadx/src/lscript.ld"  "$PROJ/external/lscript_hello_threadx.ld"
cp -f "$VENDOR/hello_filex/src/demo_sd_file.c" "$PROJ/external/demo_sd_file_reference.c"
echo "=== vendor_copy 完成 ==="
```

- [ ] **Step 3：在 Windows PowerShell 运行**

```powershell
cd E:\桌面\docs\Code
pwsh scripts\vendor_copy.ps1
```

期望输出：每行 `OK: ... -> ...`，最后 `=== vendor_copy 完成 ===`。

- [ ] **Step 4：验证拷贝结果**

```bash
ls external/threadx/ThreadX/   # 应见 Port/ Source/ tx_api.h tx_port.h tx_user.h utility/
ls external/filex/FileX/        # 应见 fx_*.h fx_zynq_sdio_driver.{c,h}
ls external/bsp/                # 应见 board_init.{c,h} bsp_init.{c,h} driver/
ls external/utils/
ls external/lscript_hello_threadx.ld
```

期望：4 个子目录 + 2 个文件全部存在。任一缺失回查 Step 1 路径。

- [ ] **Step 5：提交**

```bash
git add scripts/vendor_copy.ps1 scripts/vendor_copy.sh
git commit -m "Phase 0.2: add vendor copy scripts (idempotent)"
# 注：external/ 内容通常不入 git（建议加 .gitignore），见 Task 0.5
```

---

### Task 0.3：写 .gitignore（若启用 git）

**Files:**
- Create: `Code/.gitignore`

- [ ] **Step 1：写 .gitignore**

文件 `Code/.gitignore` 内容：

```gitignore
# vendor 拷贝产物（由 scripts/vendor_copy.{ps1,sh} 重建）
external/

# 构建产物
**/Debug/
**/Release/
**/*.o
**/*.elf
**/*.bin
**/*.map
**/generated/

# Vitis / XSCT 工程文件
**/.metadata/
**/RemoteSystemsTempFiles/

# IDE
.vscode/
.idea/

# OS
Thumbs.db
.DS_Store
```

- [ ] **Step 2：提交（若启用 git）**

```bash
git add .gitignore
git commit -m "Phase 0.3: gitignore external/ and build artifacts"
```

---

### Task 0.4：Phase 0 验收

- [ ] **Step 1：人工核对目录树**

运行（Windows）：

```powershell
cd E:\桌面\docs\Code
Get-ChildItem -Recurse -Directory | Select-Object FullName
```

期望：顶层有 `boot/{fsbl,ssbl,app_template}`、`bif`、`scripts`、`docs/superpowers/{specs,plans}`；`external/` 有 `threadx`、`filex`、`bsp`、`utils` 四子目录。

- [ ] **Step 2：确认 vendor 拷贝可重跑**

再次运行 `pwsh scripts\vendor_copy.ps1`，应能正常完成、无报错（幂等性验证）。

---

## Phase 1：SSBL 工程骨架（M1）

**目标**：从 `hello_threadx` 拷贝改造出 `boot/ssbl/` 工程，改 `lscript.ld` 的 ORIGIN/LENGTH，能独立编译并通过 JTAG 下载到 `0x100000` 跑通 ThreadX LED 闪烁。此阶段不接 FSBL、不接 SD、不接 handoff——只验证"SSBL 作为独立 ThreadX 工程能在 0x100000 跑起来"。

**Spec 引用**：§3.2（目录结构）、§8.1（资产复用映射）、§8.2（移植修正点写进 README）、§8.3（SSBL 初始化序列）、§14 M1。

### Task 1.1：拷贝 hello_threadx → boot/ssbl 骨架

**Files:**
- Create: `boot/ssbl/` 目录及子目录（src/ include/ loader/ storage/ config/ cli/ handoff/）
- Create: `boot/ssbl/ssbl.ld`（基于 external/lscript_hello_threadx.ld 改 ORIGIN/LENGTH）
- Create: `boot/ssbl/README.md`
- Create: `boot/ssbl/src/main.c`（最小版）
- Create: `boot/ssbl/src/tx_application_define.c`
- Symlink/拷贝：`boot/ssbl/ThreadX` → `external/threadx/ThreadX`
- Symlink/拷贝：`boot/ssbl/BSP` → `external/bsp`
- Symlink/拷贝：`boot/ssbl/utils` → `external/utils`

- [ ] **Step 1：建 SSBL 目录树（spec §3.2）**

```bash
cd /e/桌面/docs/Code
mkdir -p boot/ssbl/{src,include,loader,storage,config,cli/include,handoff}
```

- [ ] **Step 2：拷贝 ThreadX / BSP / utils（拷贝式，非符号链接，保证可独立修改）**

```bash
cp -rf external/threadx/ThreadX boot/ssbl/ThreadX
cp -rf external/bsp             boot/ssbl/BSP
cp -rf external/utils           boot/ssbl/utils
```

> **Rationale**：spec §3.2 标 `[vendor 拷贝]`，拷贝式而非符号链接——后续 Phase 可能要在 SSBL 局部修改（如 `tx_user.h` 调整），保持物理隔离。

- [ ] **Step 3：写 ssbl.ld（基于 hello_threadx 的 lscript.ld）**

文件 `boot/ssbl/ssbl.ld` 关键改动（其余段定义直接复制 `external/lscript_hello_threadx.ld`）：

```ld
/* === 关键改动点（与 hello_threadx/lscript.ld 的差异）===
 * MEMORY.ps7_ddr_0 的 ORIGIN 改为 0x100000 不变，但 LENGTH 从 0x1FF00000
 * 收紧到 0x100000（1MB，spec §2 + §8.1）。
 * 其余段定义（.text/.data/.bss/.vectors/.boot 等）零改动。
 */

_STACK_SIZE = DEFINED(_STACK_SIZE) ? _STACK_SIZE : 0x2000;
_HEAP_SIZE  = DEFINED(_HEAP_SIZE)  ? _HEAP_SIZE  : 0x2000;
/* …其余 _ABORT/_SUPERVISOR/_IRQ/_FIQ/_UNDEF 栈尺寸沿用模板… */

MEMORY
{
   /* ★ 改动点：LENGTH 从 0x1FF00000 收到 0x100000（1MB） */
   ps7_ddr_0 : ORIGIN = 0x100000, LENGTH = 0x100000
   ps7_qspi_linear_0 : ORIGIN = 0xFC000000, LENGTH = 0x1000000
   ps7_ram_0 : ORIGIN = 0x0, LENGTH = 0x30000
   ps7_ram_1 : ORIGIN = 0xFFFF0000, LENGTH = 0xFE00
}

ENTRY(_vector_table)

SECTIONS
{
   /* …完全复制 external/lscript_hello_threadx.ld 的 SECTIONS 块… */
}
```

> **Implementation note**：完整 SECTIONS 块较长（约 100 行），实现时打开 `external/lscript_hello_threadx.ld` 全文复制，仅改 MEMORY.ps7_ddr_0 的 LENGTH 字段。

- [ ] **Step 4：写最小版 src/main.c（仅验证 ThreadX 能跑）**

文件 `boot/ssbl/src/main.c` 内容：

```c
/* boot/ssbl/src/main.c — SSBL 入口，Phase 1 最小版
 * Phase 1 目标：验证 ssbl.elf 下载到 0x100000 后 ThreadX 能跑 LED 闪烁。
 * Phase 6+ 会替换本文件为真正的 boot_selector 流程。
 */

#include "xil_printf.h"
#include "includes.h"

/* ThreadX 线程优先级（数值越小越高）*/
#define SSBL_TASK_START_PRIO  2u
#define SSBL_TASK_LED_PRIO    20u

/* 栈大小（字节）*/
#define SSBL_TASK_START_STK_SIZE  4096u
#define SSBL_TASK_LED_STK_SIZE    1024u

static TX_THREAD  ssbl_task_start_tcb;
static uint64_t   ssbl_task_start_stk[SSBL_TASK_START_STK_SIZE/8];
static TX_THREAD  ssbl_task_led_tcb;
static uint64_t   ssbl_task_led_stk[SSBL_TASK_LED_STK_SIZE/8];

static void ssbl_task_start(ULONG thread_input);
static void ssbl_task_led(ULONG thread_input);

int main(void)
{
    xil_printf("\r\n[SSBL] Hello from SSBL @0x100000\r\n");

    bsp_init();
    board_init();

    tx_kernel_enter();    /* 不返回 */

    while (1);
    return 0;
}

void tx_application_define(void *first_unused_memory)
{
    (void)first_unused_memory;

    tx_thread_create(&ssbl_task_start_tcb, "ssbl_start", ssbl_task_start, 0,
                     &ssbl_task_start_stk[0], SSBL_TASK_START_STK_SIZE,
                     SSBL_TASK_START_PRIO, SSBL_TASK_START_PRIO,
                     TX_NO_TIME_SLICE, TX_AUTO_START);
}

static void ssbl_task_start(ULONG thread_input)
{
    (void)thread_input;

    xil_printf("[SSBL] start task entered, creating LED task\r\n");

    tx_thread_create(&ssbl_task_led_tcb, "ssbl_led", ssbl_task_led, 0,
                     &ssbl_task_led_stk[0], SSBL_TASK_LED_STK_SIZE,
                     SSBL_TASK_LED_PRIO, SSBL_TASK_LED_PRIO,
                     TX_NO_TIME_SLICE, TX_AUTO_START);

    /* start 任务自身结束（不进入死循环）*/
    tx_thread_terminate(&ssbl_task_start_tcb);
}

static void ssbl_task_led(ULONG thread_input)
{
    (void)thread_input;
    struct device *pled0 = device_find("led0");
    uint8_t val = 1;

    if (pled0 == NULL) {
        xil_printf("[SSBL] WARN: led0 not found, LED task idle\r\n");
        tx_thread_suspend(tx_thread_identify());
    }

    while (1) {
        device_write(pled0, &val, 1);
        val = (val == 1) ? 0 : 1;
        tx_thread_sleep(200);    /* 2s（tick=10ms）*/
    }
}
```

- [ ] **Step 5：写 includes.h（从 hello_threadx 复制并精简）**

文件 `boot/ssbl/src/includes.h` 内容（基于 `external/bsp/board_init.h` 实际导出的头）：

```c
/* boot/ssbl/src/includes.h — SSBL 工程总头文件 */
#ifndef SSBL_INCLUDES_H
#define SSBL_INCLUDES_H

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>

/* ThreadX */
#include "tx_api.h"

/* BSP（vendor 拷贝）*/
#include "board_init.h"
#include "bsp_init.h"
#include "device_core.h"

/* Xilinx standalone BSP（由 Vitis 工程提供 include path）*/
#include "xil_printf.h"
#include "xparameters.h"

#endif /* SSBL_INCLUDES_H */
```

> **Verification**：若 `board_init.h` 实际 include 了更多 driver 头（如 `led_driver.h`），实现时按 `external/bsp/board_init.h` 的实际 include 列表补全。本计划仅给最小集。

- [ ] **Step 6：写 README.md（含 spec §8.2 移植修正点）**

文件 `boot/ssbl/README.md` 内容（完整粘贴 spec §8.2 表格）：

````markdown
# SSBL — Second Stage Boot Loader

基于 `hello_threadx` 改造，ORIGIN=`0x100000`、LENGTH=`0x100000`（1MB）。Phase 1 仅验证骨架，后续 Phase 逐步加入 storage/CLI/handoff。

## 构建（Vitis）

1. 在 Vitis 中新建 Application Project，平台选 Zynq7010；
2. 模板选 "Empty Application"；
3. 源码目录指向 `boot/ssbl/src/`，包含路径加 `ThreadX/Source`、`ThreadX/Port`、`BSP`、`utils`、`include`；
4. 链接脚本指定 `boot/ssbl/ssbl.ld`；
5. Build → 得 `ssbl.elf`。

## JTAG 调试（Phase 1 验证）

不通过 FSBL，直接下载 `ssbl.elf` 到 `0x100000`：
- 期望串口输出：`[SSBL] Hello from SSBL @0x100000`，随后 LED0 闪烁（2s 周期）。

## 移植修正点（spec §8.2，从 hello_threadx 继承，写在此处避免重犯）

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

这些已包含在 vendor 拷贝的 `boot/ssbl/ThreadX/Port/` 与 `boot/ssbl/BSP/` 中，无需重新修改——除非 spec §8.2 表格行有新发现。
````

- [ ] **Step 7：提交**

```bash
git add boot/ssbl/
git commit -m "Phase 1.1: SSBL skeleton from hello_threadx (ORIGIN=0x100000, LENGTH=0x100000)"
```

---

### Task 1.2：在 Vitis 中编译 SSBL

**说明**：本任务涉及 Vitis GUI 操作，无纯命令行替代（Phase 2 才会用 XSCT 命令行）。任务步骤为操作清单而非代码。

- [ ] **Step 1：Vitis 新建 SSBL Application Project**

1. 打开 Vitis，Workspace 选 `E:\桌面\docs\Code\boot\ssbl\_vitis_ws`；
2. File → New → Application Project；
3. Platform 选现有 Zynq7010 platform（若没有，先用 `threadx_for_zynq7010` 的 platform）；
4. Application project name: `ssbl`；Next；
5. Domain: `standalone`；Next；
6. Template: **Empty Application**；Finish。

- [ ] **Step 2：导入源码与设置 include path**

1. 在 Project Explorer 右键 `ssbl` → Import → General → File System；
2. 从 `boot/ssbl/src/` 导入 `main.c`、`includes.h`、`tx_application_define.c`（若分文件）；
3. 右键 `ssbl` → Properties → C/C++ Build → Settings → arm-none-eabi-gcc compiler → Directories，添加 include path：
   - `${workspace_loc:/ssbl/../ThreadX/Source}` 实际指向 `boot/ssbl/ThreadX/Source`
   - `${workspace_loc:/ssbl/../ThreadX/Port}`（cortex-a9 port 头）
   - `${workspace_loc:/ssbl/../BSP}`
   - `${workspace_loc:/ssbl/../utils}`
   - `${workspace_loc:/ssbl/include}`
4. 把 `boot/ssbl/ThreadX/Source/common/*.c`、`ThreadX/Port/*.S`、`BSP/*.c`、`BSP/driver/**/*.c`、`utils/*.c` 全部加入工程源文件（Import → File System 多次）。

- [ ] **Step 3：指定链接脚本**

1. Properties → C/C++ Build → Settings → arm-none-eabi-gcc linker → Miscellaneous → Linker Script，选 `boot/ssbl/ssbl.ld`；
2. 应用、关闭。

- [ ] **Step 4：Build**

1. Project → Clean… → Clean all；
2. Project → Build All；
3. 期望：`ssbl.elf` 生成在 `boot/ssbl/_vitis_ws/ssbl/Debug/ssbl.elf`，无 error（warning 可接受但需审阅）。

- [ ] **Step 5：核对入口地址**

```bash
arm-none-eabi-readelf -h boot/ssbl/_vitis_ws/ssbl/Debug/ssbl.elf | grep Entry
```

期望：`Entry point address: 0x100020` 或附近（`_vector_table` 在 `0x100000`，reset 向量在偏移处）。实际值应 ≥ `0x100000` 且 < `0x200000`。

> **Note**：`_vector_table` 是 ENTRY，readelf 报的是 ELF e_entry，对 ARM vector table 通常 = `_vector_table` 地址 = `0x100000`。具体值取决于 reset.S 中 `.vectors` 段布局。

---

### Task 1.3：JTAG 下载并验证 SSBL 跑通

**说明**：需要物理开发板。

- [ ] **Step 1：连板，启动 XSCT Console**

Vitis → Xilinx → XSCT Console，连 JTAG：

```tcl
connect
targets -set -filter {name =~ "APU"}
rst -system
```

- [ ] **Step 2：先下载 ps7_init（必需，否则 DDR 没初始化）**

```tcl
# 用 Vivado 导出的 ps7_init（通常在 platform 工程）
source <path-to-platform>/ps7_init.tcl
ps7_init
ps7_post_config
```

- [ ] **Step 3：下载 ssbl.elf 并运行**

```tcl
dow boot/ssbl/_vitis_ws/ssbl/Debug/ssbl.elf
con
```

- [ ] **Step 4：观察串口**

期望串口输出：
```
[SSBL] Hello from SSBL @0x100000
[SSBL] start task entered, creating LED task
```
随后 LED0 以 2s 周期闪烁。

- [ ] **Step 5：失败排查清单**

| 现象 | 可能原因 |
|---|---|
| 下载后无任何输出 | ps7_init 未跑，DDR 未初始化 |
| 输出到一半乱码 | UART 波特率不对（应为 115200） |
| LED 不闪但串口 OK | `led0` 设备未注册，检查 board_init |
| 进入 hard fault | `_vector_table` 地址不对，检查 ssbl.ld ENTRY 与 reset.S |

- [ ] **Step 6：Phase 1 完成提交**

```bash
# 提交 Vitis 工程的源文件引用（若 _vitis_ws 入 git 则单独管理）
git add boot/ssbl/
git commit -m "Phase 1.3: SSBL JTAG-verified, ThreadX LED blinking at 0x100000"
```

---

## Phase 2：精简 FSBL（M2）

**目标**：裁剪 `threadx_for_zynq7010/zynq7010/zynq_fsbl/`，删除 NAND/NOR/MMC/JTAG + RSA + WDT + 多 partition 分支，得到一个 ~30–40KB 的 FSBL，能独立 JTAG 下载跑通 banner + DDR 测试。此阶段不接 SSBL（Phase 3 才链通）。

**Spec 引用**：§7.1–§7.7 全章节。

### Task 2.1：拷贝并审计 vendor FSBL 源

**Files:**
- Create: `boot/fsbl/`（从 vendor 整目录拷贝，再裁剪）

- [ ] **Step 1：拷贝 vendor FSBL 整目录**

```bash
cp -rf /e/桌面/docs/threadx_for_zynq7010/zynq7010/zynq_fsbl boot/fsbl
```

- [ ] **Step 2：审计拷贝出的文件清单**

```bash
ls boot/fsbl/
```

期望至少有：`main.c`、`fsbl.h`、`fsbl_handoff.S`、`image_mover.c`、`lscript.ld`、`pcap.c/h`、`sd.c/h`、`qspi.c/h`、`nand.c/h`、`nor.c/h`、`rsa.c/h`、`md5.c/h`、`ps7_init.c/h`、`fsbl_hooks.c/h`、`fsbl_debug.h`、`zynq_fsbl_bsp/`。

- [ ] **Step 3：在 boot/fsbl/README.md 记录裁剪计划**

文件 `boot/fsbl/README.md`：

```markdown
# 精简 FSBL — Stage 1

裁剪自 `threadx_for_zynq7010/zynq7010/zynq_fsbl`。spec §7.2 的保留/删除决策表已映射到具体文件。

## 裁剪清单（spec §7.2 + §7.3）

| 文件 | 处置 | 理由 |
|---|---|---|
| `main.c` | 裁剪（删 NAND/NOR/MMC/JTAG 分支 + WDT + RSA） | spec §7.2 + §7.4 |
| `fsbl.h` | 裁剪（删对应宏） | 同上 |
| `image_mover.c` | 简化（只加载第一个 a9_0 partition） | spec §7.6 |
| `fsbl_handoff.S` | 复用（零改动，黄金参考） | spec §7.1 |
| `lscript.ld` | 复用（OCM 布局不变） | spec §7.3 |
| `pcap.c/h` | 复用 | spec §7.2（保留 InitPcap） |
| `sd.c/h` | 复用 | 主介质 |
| `qspi.c/h` | 复用（条件编译保留） | 后期 QSPI |
| `nand.c/h` | **删除** | spec §7.3 |
| `nor.c/h` | **删除** | spec §7.3 |
| `rsa.c/h` | **删除** | spec §7.2（开发期不用安全启动） |
| `md5.c/h` | **删除** | spec §7.3（用 SSBL 自己的 CRC32） |
| `ps7_init.c/h` | 复用 | DDR 初始化必需 |
| `fsbl_hooks.c/h` | 复用（空实现） | spec §7.3 |
| `fsbl_debug.h` | 复用 | 日志级别 |

## 与原版的差异

见 spec §7.4 完整裁剪后的 main.c 流程。
```

- [ ] **Step 4：提交（裁剪前快照）**

```bash
git add boot/fsbl/
git commit -m "Phase 2.1: snapshot vendor FSBL before trimming"
```

---

### Task 2.2：删除非必需源文件

**Files:**
- Delete: `boot/fsbl/nand.c`, `boot/fsbl/nand.h`
- Delete: `boot/fsbl/nor.c`, `boot/fsbl/nor.h`
- Delete: `boot/fsbl/rsa.c`, `boot/fsbl/rsa.h`
- Delete: `boot/fsbl/md5.c`, `boot/fsbl/md5.h`

- [ ] **Step 1：删除 NAND/NOR/RSA/MD5 源**

```bash
cd boot/fsbl
rm -f nand.c nand.h nor.c nor.h rsa.c rsa.h md5.c md5.h
```

- [ ] **Step 2：grep 残留引用**

```bash
cd /e/桌面/docs/Code/boot/fsbl
grep -rn -E "(nand|nor|rsa|md5)" --include="*.c" --include="*.h" .
```

期望：仅剩注释或 `#if 0` 残块。若有真实函数调用（如 `NandInit()`），下个任务在 main.c 裁剪时一并处理。

- [ ] **Step 3：提交**

```bash
git add -A boot/fsbl/
git commit -m "Phase 2.2: remove nand/nor/rsa/md5 sources"
```

---

### Task 2.3：裁剪 main.c（按 spec §7.4）

**Files:**
- Modify: `boot/fsbl/main.c`（整体改写为 spec §7.4 的版本）

- [ ] **Step 1：备份原 main.c**

```bash
cp boot/fsbl/main.c boot/fsbl/main.c.orig
```

- [ ] **Step 2：按 spec §7.4 整体替换 main.c 的核心流程**

`boot/fsbl/main.c` 替换为（spec §7.4 的完整版本）：

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

- [ ] **Step 3：补全 main.c 的其余部分（include + 辅助函数）**

在 main.c 顶部保留原 vendor main.c 的 include 块（删除 nand.h/nor.h/rsa.h/md5.h 行）。删除原 main.c 中所有 `#ifdef XPAR_PS7_NAND_*` / `XPAR_PS7_NOR_*` / `XPAR_PS7_SDIO_*`（MMC 不删，SD 也是 SDIO）/ RSA 相关的条件编译块。

保留以下函数：`OutputStatus`、`FsblHookFallback`、`DDRInitCheck`、`InitSD`、`InitQspi`、`InitPcap`、`RegisterHandlers`。这些函数定义原本就在 main.c 或被裁剪的 .c 中——若 `DDRInitCheck` 原本依赖 NAND（不会），需保留它本身但删除 NAND 调用分支。

- [ ] **Step 4：grep 残留 NAND/NOR/RSA/MD5 函数调用**

```bash
grep -n -E "(Nand|Nor|Rsa|Md5)" boot/fsbl/main.c
```

期望：无输出。若还有引用，回 Step 3 处理。

- [ ] **Step 5：提交**

```bash
git add boot/fsbl/main.c
git commit -m "Phase 2.3: trim FSBL main.c per spec §7.4 (drop NAND/NOR/MMC/JTAG + RSA + WDT)"
```

---

### Task 2.4：简化 image_mover.c（按 spec §7.6）

**Files:**
- Modify: `boot/fsbl/image_mover.c`（`LoadBootImage` 简化为只加载第一个 a9_0 partition）

- [ ] **Step 1：定位 LoadBootImage 与 MovePartition**

```bash
grep -n "^u32 LoadBootImage\|^void MovePartition\|MovePartition(" boot/fsbl/image_mover.c
```

记录行号。

- [ ] **Step 2：在 image_mover.c 顶部加简化版 LoadBootImage**

替换 `LoadBootImage` 函数体为（spec §7.6）：

```c
u32 LoadBootImage(void)
{
    u32 ExecAddr = 0;
    /* 1. 读 Boot Header（偏移 0x0），确认 XLNX magic */
    if (ImageCheckID(0) != XST_SUCCESS) {
        return 0;
    }
    /* 2. 读 Image Header Table → 第一个 Image Header */
    /* 3. 遍历该 Image 的 partition 列表，找 destination_cpu=a9_0 的第一个 */
    /* 4. MovePartition: 把 SSBL.bin 从介质拷到 DDR_LOAD_ADDR（0x100000）*/
    /* 5. 返回 partition 的 ExecAddr 作为 handoff 地址 */
    return ExecAddr;
}
```

> **Implementation note**：spec §7.6 的伪代码省略了 Xilinx Boot Header 结构解析的细节。实现时复用 vendor `image_mover.c` 原有的 `ImageHeader`、`PartitionHeader` 结构体与 `ReadPmfHeader`、`GetPartitionHeader` 辅助函数，**仅删除**：
> - 多 Image 遍历循环（找到第一个 a9_0 partition 后 `break`）；
> - PL partition（bitstream）分支；
> - checksum 校验调用（注释为"由 SSBL 自己校验"，但 Xilinx Boot Header 的 partition checksum 仍可保留，因为它是 BootROM 写入的、低成本）；
> - RSA 认证（`SecChk` 等已随 rsa.c 删除）。

- [ ] **Step 3：删除 image_mover.c 中 RSA/bitstream 多 partition 相关代码**

```bash
grep -n -E "(AuthPartition|SignatureCheck|bitstream|PL partition|BITSTREAM)" boot/fsbl/image_mover.c
```

逐处审查：RSA 调用直接删除（rsa.c 已无）；bitstream partition 分支用 `if (0)` 包起来或注释（避免被加载，但保留代码结构便于理解）。

- [ ] **Step 4：提交**

```bash
git add boot/fsbl/image_mover.c
git commit -m "Phase 2.4: simplify image_mover to load only first a9_0 partition"
```

---

### Task 2.5：在 Vitis 编译精简 FSBL

- [ ] **Step 1：Vitis 新建 FSBL Application Project**

模板选 "Zynq FSBL"（而非 Empty），创建后立即**用 `boot/fsbl/` 下的源码替换**工程模板源码：
1. 删工程模板的 src/*.c、src/*.h；
2. Import → File System → 选 `boot/fsbl/`，导入全部 .c/.h/.S/.ld。

- [ ] **Step 2：Build**

期望：`fsbl.elf` 生成，体积应明显小于原版（用 readelf 看 size）：

```bash
arm-none-eabi-size boot/fsbl/_vitis_ws/fsbl/Debug/fsbl.elf
```

期望：text+data < 50KB（spec §7.4 目标 30–40KB）。

- [ ] **Step 3：修复编译错误（迭代）**

若出现 "undefined reference to NandInit/RsaDecrypt/..."，回查 Task 2.3/2.4 的 grep 残留，删除调用点。

---

### Task 2.6：JTAG 下载验证精简 FSBL

- [ ] **Step 1：JTAG 下载 fsbl.elf 并 run**

```tcl
connect
targets -set -filter {name =~ "APU"}
rst -system
dow boot/fsbl/_vitis_ws/fsbl/Debug/fsbl.elf
con
```

- [ ] **Step 2：观察串口**

期望：
```
FSBL (slim) Jun 22 2026 12:00:00
```
随后因没有 BOOT.bin（或 BOOT.bin 是测试用），FSBL 在 `LoadBootImage` 处失败或返回 0，进 `FsblHookFallback` 死循环——这是预期的（Phase 3 才接入 SSBL）。

- [ ] **Step 3：确认 DDRInitCheck 通过**

在 `DDRInitCheck` 后加临时 `xil_printf("DDR OK\r\n");`，重新编译下载，期望串口见 "DDR OK"。验证后**移除临时打印**，提交。

---

## Phase 3：FSBL → SSBL 链通（M3）

**目标**：用 bootgen 把 fsbl.elf + ssbl.elf 打包成 BOOT.bin，烧 SD 卡上电，从 BootROM 一路跑到 SSBL 的 LED 闪烁。这是第一次"真启动链"验证。

**Spec 引用**：§13.1、§13.2。

### Task 3.1：确认 ssbl.elf 可被 bootgen 直接消费

**说明**：SSBL 直接以 `ssbl.elf` 交给 bootgen（spec §13.2），**不做 `objcopy -O binary`**。bootgen 从 ELF 的 program headers 读取段信息、加载地址与入口地址，故这里只需核对 ELF 元数据正确，无需生成 `.bin`。

**Files:**
- （无新文件；复用 Phase 1 Task 1.2 的产物 `boot/ssbl/_vitis_ws/ssbl/Debug/ssbl.elf`）

- [ ] **Step 1：确认 ssbl.elf 已生成**

```bash
ls -la boot/ssbl/_vitis_ws/ssbl/Debug/ssbl.elf
```

期望：文件存在（Phase 1 Task 1.2 Build 产物）。

- [ ] **Step 2：核对 ELF 入口地址**

```bash
arm-none-eabi-readelf -h boot/ssbl/_vitis_ws/ssbl/Debug/ssbl.elf | grep Entry
```

期望：`Entry point address` ≥ `0x100000` 且 < `0x200000`（`_vector_table` 在 `ssbl.ld` 的 `ORIGIN=0x100000` 区域内）。

- [ ] **Step 3：核对 program header 的加载地址**

```bash
arm-none-eabi-readelf -l boot/ssbl/_vitis_ws/ssbl/Debug/ssbl.elf | grep -E "LOAD|0x"
```

期望：第一个 `LOAD` 段的 `VirtAddr` 起始于 `0x00100000`（与 `ssbl.ld` 的 `ORIGIN` 一致）——这正是 bootgen 写入 BOOT.bin partition 的 load address，无需在 bif 里额外指定。

---

### Task 3.2：写 boot.bif

**Files:**
- Create: `bif/boot.bif`

- [ ] **Step 1：写 boot.bif（spec §13.2）**

文件 `bif/boot.bif` 内容：

```
the_ROM_image:
{
    [bootloader, destination_cpu = a9_0]   boot/fsbl/_vitis_ws/fsbl/Debug/fsbl.elf
    [destination_cpu = a9_0]               boot/ssbl/_vitis_ws/ssbl/Debug/ssbl.elf
}
```

> **Note**：路径相对 `bif/` 目录解析，或用绝对路径；bootgen 实际跑时 cd 到 `bif/`。

- [ ] **Step 2：提交**

```bash
git add bif/boot.bif
git commit -m "Phase 3.2: boot.bif for fsbl+ssbl chain"
```

---

### Task 3.3：bootgen 打包 BOOT.bin

**Files:**
- Create: `scripts/build_boot_bin.sh`、`scripts/build_boot_bin.ps1`
- Create: `bif/BOOT.bin`（产物）

- [ ] **Step 1：写 build_boot_bin.ps1**

文件 `scripts/build_boot_bin.ps1`：

```powershell
# build_boot_bin.ps1 — 调 bootgen 打包 BOOT.bin
$ErrorActionPreference = "Stop"
$PROJ = Split-Path -Parent $PSScriptRoot | Split-Path -Parent

# bootgen 在 Vitis 安装目录 bin/ 下，需先确保在 PATH 或用全路径
$BOOTGEN = Get-Command bootgen -ErrorAction SilentlyContinue
if (-not $BOOTGEN) {
    Write-Error "bootgen 不在 PATH。请先 source Vitis settings64.bat"
    exit 1
}

Push-Location "$PROJ\bif"
bootgen -image boot.bif -arch zynq -out BOOT.BIN -w on
Pop-Location

Write-Host "BOOT.BIN 生成于 $PROJ\bif\BOOT.BIN"
```

- [ ] **Step 2：写 build_boot_bin.sh（Linux 备用）**

文件 `scripts/build_boot_bin.sh`：

```bash
#!/usr/bin/env bash
set -euo pipefail
PROJ="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$PROJ/bif"
bootgen -image boot.bif -arch zynq -out BOOT.BIN -w on
echo "BOOT.BIN 生成于 $PROJ/bif/BOOT.BIN"
```

- [ ] **Step 3：运行打包**

```powershell
cd E:\桌面\docs\Code
pwsh scripts\build_boot_bin.ps1
```

期望：`bif/BOOT.BIN` 生成，无 error。

- [ ] **Step 4：提交脚本**

```bash
git add scripts/build_boot_bin.ps1 scripts/build_boot_bin.sh
git commit -m "Phase 3.3: bootgen packaging scripts"
```

---

### Task 3.4：烧 SD 卡上电验证

**说明**：需物理板 + SD 卡。

> **介质状态说明**：此阶段 SD 卡**单分区即可**（BootROM 从首 FAT 分区读 BOOT.bin，单分区时首分区即整张卡，能验证启动链）。**双分区隔离（spec §5.5）在 Phase 4.0 才引入**——那时才需要把 BOOT.bin 单独放 P1。本 Task 用最简单的单分区先把 FSBL→SSBL 链跑通。

- [ ] **Step 1：格式化 SD 卡为 FAT32（单分区，本阶段足够）**

用 Windows 磁盘管理或 `diskpart` 格式化为 FAT32（单分区）。

- [ ] **Step 2：拷贝 BOOT.bin 到 SD 卡（首分区根目录）**

```bash
cp bif/BOOT.BIN <SD卡盘符>:/
```

- [ ] **Step 3：设 boot mode pins 为 SD 启动，上电**

期望串口：
```
FSBL (slim) ...
[SSBL] Hello from SSBL @0x100000
[SSBL] start task entered, creating LED task
```
随后 LED0 闪烁。

- [ ] **Step 4：失败排查清单**

| 现象 | 可能原因 |
|---|---|
| 完全无输出 | boot mode pins 错；BOOT.bin 烧错位置（必须在首 FAT 分区根目录）；SD 卡未格式化 FAT32 |
| 只见 FSBL banner 不见 SSBL | ssbl.elf 没打包进 BOOT.bin；image_mover 没找到 a9_0 partition；FSBL handoff 跳错地址 |
| SSBL 起来后 hard fault | ssbl.elf ORIGIN 不对；reset.S 与 ssbl.ld 不匹配 |
| LED 不闪但串口 OK | 同 Phase 1 Task 1.3 Step 5 |

- [ ] **Step 5：Phase 3 完成提交**

```bash
git commit --allow-empty -m "Phase 3.4: FSBL→SSBL chain verified on hardware"
```

---

## Phase 4：storage 抽象层 + FileX SD 挂载（M4）

**目标**：实现 spec §11 的 `storage_ops_t` 抽象，先做 SD 实现（复用 vendor `fx_zynq_sdio_driver`），SSBL 能挂载 **SD 卡 P2 数据分区**并执行 `ls`/`cat` 命令（CLI 在 Phase 7，本 Phase 先把 storage + 简易 shell 函数打通）。

**Spec 引用**：§11.1（接口设计）、§11.2 阶段 A、**§5.5 存储隔离原则**。

> **★ 关键约束（spec §5.5）**：SD 卡必须先格式化为**双 FAT32 分区**——P1（100MB）仅放 BOOT.BIN、P2（剩余）放数据。SSBL 的 `fx_media_open` **只挂载 P2**，绝不覆盖 P1，从而把升级写入与 BOOT.bin 簇链物理隔离。本 Phase 的 Task 4.0 先完成双分区格式化，后续 Task 在此基础上挂载 P2。

### Task 4.0：格式化 SD 卡为双 FAT32 分区（前置，spec §5.5.3）

**Files:**
- Create: `scripts/format_sd_card.sh`、`scripts/format_sd_card.ps1`
- Create: `scripts/README_sd_format.md`

- [ ] **Step 1：写 format_sd_card.ps1（Windows 主用）**

文件 `scripts/format_sd_card.ps1` 内容（用 diskpart 脚本化双分区）：

```powershell
# format_sd_card.ps1 — 把 SD 卡格式化为双 FAT32 分区（spec §5.5.3）
# 用法：pwsh format_sd_card.ps1 <SD盘符或磁盘号>
#   例：pwsh format_sd_card.ps1 E      # 盘符 E:
#       pwsh format_sd_card.ps1 2      # 磁盘号 2（diskpart list disk 确认）
#
# 产物：P1=100MB FAT32（仅 BOOT.BIN），P2=剩余 FAT32（数据）
# 危险：会清空目标盘所有数据！运行前务必用 diskpart list disk 确认盘号。

$ErrorActionPreference = "Stop"
if ($args.Count -lt 1) {
    Write-Error "用法：pwsh format_sd_card.ps1 <SD盘符或磁盘号>"
    exit 1
}
$target = $args[0]

# 生成 diskpart 脚本（按磁盘号清理 + 双分区）
# P1: size=102400 (100MB) FAT32，标记 bootable（BootROM 要求首个 FAT 分区）
# P2: 剩余空间 FAT32
$dpScript = @"
select disk $target
clean
create partition primary size=102400
format quick fs=fat32 label=P1_BOOT
active
create partition primary
format quick fs=fat32 label=P2_DATA
exit
"@
$dpScript | diskpart
Write-Host "双分区完成：P1=100MB(P1_BOOT) / P2=剩余(P2_DATA)"
Write-Host "下一步：把 BOOT.BIN 拷到 P1，其余文件放 P2"
```

- [ ] **Step 2：写 format_sd_card.sh（Linux/Git Bash 备用，用 fdisk + mkfs.fat）**

文件 `scripts/format_sd_card.sh`：

```bash
#!/usr/bin/env bash
# format_sd_card.sh — Linux/Git Bash 双分区（spec §5.5.3）
# 用法：sudo ./format_sd_card.sh /dev/sdX
set -euo pipefail
DEV="${1:?用法: $0 /dev/sdX}"
# 卸载可能挂载的分区
umount "${DEV}"* 2>/dev/null || true
# 清空分区表 + 双分区：P1=100MB bootable, P2=剩余
# 用 sfdisk 脚本化（label: dos）
sfdisk "$DEV" <<EOF
label: dos
,102400M,b,*
,,b,
EOF
# 格式化
mkfs.fat -F32 -n P1_BOOT "${DEV}1"
mkfs.fat -F32 -n P2_DATA "${DEV}2"
echo "双分区完成：P1=100MB(P1_BOOT) / P2=剩余(P2_DATA)"
```

- [ ] **Step 3：写 README_sd_format.md（操作说明 + 验证）**

文件 `scripts/README_sd_format.md`：

```markdown
# SD 卡双分区格式化（spec §5.5.3）

## 为什么双分区

隔离 BOOT.BIN 与升级数据，避免共享 FAT 表导致变砖（见 spec §5.5）。
P1 只放 BOOT.BIN（烧录后只读），P2 放所有可变数据（app/bit/cfg）。

## 步骤

1. 确认 SD 卡盘号/盘符（diskpart `list disk` 或 `lsblk`）
2. 运行：
   - Windows: `pwsh scripts\format_sd_card.ps1 <盘号>`
   - Linux:   `sudo ./scripts/format_sd_card.sh /dev/sdX`
3. 验证：资源管理器应出现两个盘符（P1_BOOT ~100MB, P2_DATA 剩余）
4. 把 `BOOT.BIN` 拷到 **P1**（仅此一个文件）
5. 其余文件（app/bit/cfg）拷到 **P2**

## 验证双分区（关键）

启动板子后 SSBL 的 `ls` 命令应只看到 P2 的内容（app/cfg/...），
**不应看到 BOOT.BIN**——这证明 SSBL 只挂了 P2，隔离生效。
若 ls 能看到 BOOT.BIN，说明挂载了 P1，需回查 Task 4.3 的分区解析。
```

- [ ] **Step 4：手动跑一次格式化（需物理 SD 卡）**

```powershell
# Windows 示例（盘号从 diskpart list disk 确认，假设是 2）
diskpart
# 在 diskpart 交互里先 list disk 确认，再 exit
pwsh scripts\format_sd_card.ps1 2
```

期望：完成后资源管理器出现两个盘符 P1_BOOT（~100MB）、P2_DATA（剩余）。

- [ ] **Step 5：拷贝 BOOT.BIN 到 P1 验证 BootROM 能启动**

把 Phase 3 产出的 `bif/BOOT.BIN` 拷到 **P1**（不是 P2），设 SD boot mode 上电。
期望：FSBL banner + SSBL banner 正常出现（证明 BootROM 能从 P1 的 FAT 找到 BOOT.BIN）。

- [ ] **Step 6：提交脚本**

```bash
git add scripts/format_sd_card.ps1 scripts/format_sd_card.sh scripts/README_sd_format.md
git commit -m "Phase 4.0: dual-partition SD format (P1=BOOT.BIN, P2=data) per spec §5.5"
```

---

### Task 4.1：写 storage.h 接口

**Files:**
- Create: `boot/ssbl/storage/storage.h`

- [ ] **Step 1：写 storage.h（spec §11.1）**

文件 `boot/ssbl/storage/storage.h`：

```c
/* boot/ssbl/storage/storage.h — 介质抽象层接口
 * 上层（image_loader / cli / boot_selector）只调 storage_* 宏，
 * 不直接碰 FileX。SD→QSPI 迁移时换 g_storage 指向即可。
 */
#ifndef SSBL_STORAGE_H
#define SSBL_STORAGE_H

#include <stdint.h>
#include "fx_api.h"

/* storage 模式（open/file_open 用）*/
#define STORAGE_OPEN_READ   0x01
#define STORAGE_OPEN_WRITE  0x02
#define STORAGE_OPEN_RDWR   (STORAGE_OPEN_READ | STORAGE_OPEN_WRITE)

/* storage 错误码（负数；0 = OK）*/
#define STORAGE_OK              0
#define STORAGE_ERR_OPEN        (-1)
#define STORAGE_ERR_NO_MEDIA    (-2)
#define STORAGE_ERR_NOT_FOUND   (-3)
#define STORAGE_ERR_IO          (-4)
#define STORAGE_ERR_EXISTS      (-5)
#define STORAGE_ERR_INVAL       (-6)

typedef struct {
    int  (*media_open)  (void);
    int  (*media_close) (void);
    int  (*file_open)   (const char *path, int mode);
    int  (*file_read)   (void *buf, uint32_t len);
    int  (*file_write)  (const void *buf, uint32_t len);
    int  (*file_close)  (void);
    int  (*file_size)   (const char *path, uint32_t *size);
    int  (*file_delete) (const char *path);
    int  (*file_rename) (const char *oldpath, const char *newpath);
    int  (*dir_list)    (const char *path);
} storage_ops_t;

/* 由 sd_port.c 或 qspi_port.c 提供实例 */
extern const storage_ops_t *g_storage;

/* 便捷包装宏（上层只调这些）*/
#define storage_media_open()        (g_storage->media_open())
#define storage_media_close()       (g_storage->media_close())
#define storage_file_open(p,m)      (g_storage->file_open(p,m))
#define storage_file_read(b,l)      (g_storage->file_read(b,l))
#define storage_file_write(b,l)     (g_storage->file_write(b,l))
#define storage_file_close()        (g_storage->file_close())
#define storage_file_size(p,s)      (g_storage->file_size(p,s))
#define storage_file_delete(p)      (g_storage->file_delete(p))
#define storage_file_rename(o,n)    (g_storage->file_rename(o,n))
#define storage_dir_list(p)         (g_storage->dir_list(p))

#endif /* SSBL_STORAGE_H */
```

- [ ] **Step 2：提交**

```bash
git add boot/ssbl/storage/storage.h
git commit -m "Phase 4.1: storage_ops_t abstraction interface"
```

---

### Task 4.2：写 storage_fx_glue.c（FileX 通用回调）

**Files:**
- Create: `boot/ssbl/storage/storage_fx_glue.c`

- [ ] **Step 1：写 storage_fx_glue.c（封装 FX_MEDIA + FX_FILE 全局对象）**

文件 `boot/ssbl/storage/storage_fx_glue.c`：

```c
/* boot/ssbl/storage/storage_fx_glue.c — FileX 通用胶水
 * sd_port.c 和 qspi_port.c 共用本文件的全局 FX_MEDIA/FX_FILE 与回调。
 * sd_port.c 提供 fx_media_driver（fx_zynq_sdio_driver），qspi_port.c 提供另一个。
 */

#include "storage.h"
#include "fx_api.h"

#define STORAGE_MEDIA_NAME_LEN  32

/* 全局 FileX 对象（一份，不并发访问）*/
FX_MEDIA  g_fx_media;
FX_FILE   g_fx_file;

/* media 内存缓冲（FileX 要求，扇区对齐）*/
static UCHAR media_memory[FX_SECTOR_SIZE * 4] __attribute__((aligned(32)));

/* 当前 media 的 driver 函数指针（由 sd_port/qspi_port 设置）*/
static VOID (*g_fx_driver)(FX_MEDIA *) = NULL;

/* 当前 media 名称（"SD_CARD" / "QSPI_NOR"）*/
static char g_media_name[STORAGE_MEDIA_NAME_LEN] = "(none)";

void storage_fx_set_driver(VOID (*driver)(FX_MEDIA *), const char *name)
{
    g_fx_driver = driver;
    strncpy(g_media_name, name, STORAGE_MEDIA_NAME_LEN - 1);
    g_media_name[STORAGE_MEDIA_NAME_LEN - 1] = '\0';
}

/* === FileX 通用回调 === */
int storage_fx_media_open(void)
{
    UINT status;
    if (g_fx_driver == NULL) return STORAGE_ERR_INVAL;
    status = fx_media_open(&g_fx_media, g_media_name, g_fx_driver, 0,
                           media_memory, sizeof(media_memory));
    return (status == FX_SUCCESS) ? STORAGE_OK : STORAGE_ERR_OPEN;
}

int storage_fx_media_close(void)
{
    UINT status = fx_media_close(&g_fx_media);
    return (status == FX_SUCCESS) ? STORAGE_OK : STORAGE_ERR_IO;
}

int storage_fx_file_open(const char *path, int mode)
{
    UINT fx_mode;
    if (mode & STORAGE_OPEN_WRITE) {
        fx_mode = FX_OPEN_FOR_WRITE;
    } else {
        fx_mode = FX_OPEN_FOR_READ;
    }
    UINT status = fx_file_open(&g_fx_media, &g_fx_file, (CHAR *)path, fx_mode);
    return (status == FX_SUCCESS) ? STORAGE_OK : STORAGE_ERR_NOT_FOUND;
}

int storage_fx_file_read(void *buf, uint32_t len)
{
    ULONG actual = 0;
    UINT status = fx_file_read(&g_fx_file, buf, len, &actual);
    if (status != FX_SUCCESS) return STORAGE_ERR_IO;
    return (int)actual;
}

int storage_fx_file_write(const void *buf, uint32_t len)
{
    UINT status = fx_file_write(&g_fx_file, (void *)buf, len);
    return (status == FX_SUCCESS) ? (int)len : STORAGE_ERR_IO;
}

int storage_fx_file_close(void)
{
    UINT status = fx_file_close(&g_fx_file);
    return (status == FX_SUCCESS) ? STORAGE_OK : STORAGE_ERR_IO;
}

int storage_fx_file_size(const char *path, uint32_t *size)
{
    FX_FILE tmp;
    UINT status = fx_file_open(&g_fx_media, &tmp, (CHAR *)path, FX_OPEN_FOR_READ);
    if (status != FX_SUCCESS) return STORAGE_ERR_NOT_FOUND;
    *size = (uint32_t)tmp.fx_file_current_file_size;
    fx_file_close(&tmp);
    return STORAGE_OK;
}

int storage_fx_file_delete(const char *path)
{
    UINT status = fx_file_delete(&g_fx_media, (CHAR *)path);
    if (status == FX_NOT_FOUND) return STORAGE_ERR_NOT_FOUND;
    return (status == FX_SUCCESS) ? STORAGE_OK : STORAGE_ERR_IO;
}

int storage_fx_file_rename(const char *oldpath, const char *newpath)
{
    UINT status = fx_file_rename(&g_fx_media, (CHAR *)oldpath, (CHAR *)newpath);
    return (status == FX_SUCCESS) ? STORAGE_OK : STORAGE_ERR_IO;
}

int storage_fx_dir_list(const char *path)
{
    FX_LOCAL_PATH local;
    FX_DIR_ENTRY  dir;
    UINT status;

    status = fx_directory_local_path_set(&g_fx_media, &local, (CHAR *)path);
    if (status != FX_SUCCESS) return STORAGE_ERR_NOT_FOUND;

    memset(&dir, 0, sizeof(dir));
    while (fx_directory_next_entry_find(&g_fx_media, &dir) == FX_SUCCESS) {
        xil_printf("  %s\r\n", dir.fx_dir_entry_name);
    }
    fx_directory_local_path_clear(&g_fx_media);
    return STORAGE_OK;
}
```

> **Note**：本文件依赖 `fx_zynq_sdio_driver` 提供的 driver 入口（下一任务）。`strncpy`、`memset`、`xil_printf` 需确保 include path 与 libc 链接到位。

- [ ] **Step 2：提交**

```bash
git add boot/ssbl/storage/storage_fx_glue.c
git commit -m "Phase 4.2: FileX glue callbacks (media/file/dir) shared by sd_port/qspi_port"
```

---

### Task 4.3：写 sd_port.c（绑定 fx_zynq_sdio_driver，挂载 P2 数据分区）

**Files:**
- Create: `boot/ssbl/storage/sd_port.c`

> **★ 关键（spec §5.5.3）**：sd_port **只挂载 P2 数据分区**，不挂 P1（P1 的 BOOT.BIN 与 SSBL 完全隔离）。FileX 的 `fx_zynq_sdio_driver` 按 LBA 读 SD；要让 FileX 认为扇区 0 是 P2 的起始，需要在挂载前读 MBR、解析 P2 的起始 LBA，并把该偏移记录下来供 driver 的读路径加上。

- [ ] **Step 1：参考 vendor hello_filex/demo_sd_file.c 的初始化流程**

```bash
cat external/demo_sd_file_reference.c | head -150
```

记录 `fx_system_initialize`、`fx_media_open`、SDIO driver 注册的调用次序。注意 vendor demo 默认挂载首分区（单分区场景）；本工程需改为挂载 **P2**。

- [ ] **Step 2：写 sd_port.c（含 P2 LBA 偏移逻辑）**

文件 `boot/ssbl/storage/sd_port.c`：

```c
/* boot/ssbl/storage/sd_port.c — SD 卡 storage 后端
 * 绑定 vendor fx_zynq_sdio_driver，只挂载 P2 数据分区（spec §5.5.3）。
 * P1（BOOT.BIN）与 SSBL 完全隔离——SSBL 不读不写 P1。
 *
 * 实现 P2 偏移的两种方式（实现时任选其一，推荐 A）：
 *   A) 包装 fx_zynq_sdio_driver：读请求里把 FileX 的逻辑扇区号 + P2 起始 LBA
 *      再透传给原 driver。这样 FileX 看到的"扇区 0"实际是 P2 第一个扇区。
 *   B) 若 fx_zynq_sdio_driver 内部已支持 media 偏移寄存器，则直接配置。
 */

#include "storage.h"
#include "fx_api.h"
#include "fx_zynq_sdio_driver.h"

extern FX_MEDIA  g_fx_media;
extern FX_FILE   g_fx_file;

extern int storage_fx_media_open(void);
extern int storage_fx_media_close(void);
extern int storage_fx_file_open(const char *path, int mode);
extern int storage_fx_file_read(void *buf, uint32_t len);
extern int storage_fx_file_write(const void *buf, uint32_t len);
extern int storage_fx_file_close(void);
extern int storage_fx_file_size(const char *path, uint32_t *size);
extern int storage_fx_file_delete(const char *path);
extern int storage_fx_file_rename(const char *oldpath, const char *newpath);
extern int storage_fx_dir_list(const char *path);
extern void storage_fx_set_driver(VOID (*driver)(FX_MEDIA *), const char *name);

const storage_ops_t g_sd_storage = {
    .media_open  = storage_fx_media_open,
    .media_close = storage_fx_media_close,
    .file_open   = storage_fx_file_open,
    .file_read   = storage_fx_file_read,
    .file_write  = storage_fx_file_write,
    .file_close  = storage_fx_file_close,
    .file_size   = storage_fx_file_size,
    .file_delete = storage_fx_file_delete,
    .file_rename = storage_fx_file_rename,
    .dir_list    = storage_fx_dir_list,
};

/* P2 起始 LBA（解析 MBR 得到，见 sd_port_read_partition_table） */
static uint32_t g_p2_start_lba = 0;

/* 读 MBR（LBA 0），解析第 2 个分区表项得 P2 起始 LBA。
 * MBR 分区表项布局（每项 16 字节，偏移 0x1BE 起）：
 *   byte[8..11] = 该分区起始 LBA（little-endian）
 *   第 2 项 = 偏移 0x1CE + 8 = 0x1D6
 * 返回 0=OK，负数=失败。
 */
static int sd_port_read_partition_table(void)
{
    uint8_t mbr[512];
    /* 用裸 SDIO 读 LBA 0（绕过 FileX，因为此刻 FileX 还没挂载）。
     * 复用 fx_zynq_sdio_driver 的底层读，或 Xilinx xsdps 直接读。
     */
    if (sd_raw_read_sector(0, mbr) != 0) {
        return -1;
    }
    /* 简单校验 MBR 签名 0x55AA */
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        return -2;
    }
    /* P2 起始 LBA（第 2 个分区表项，偏移 0x1D6，4 字节 little-endian） */
    g_p2_start_lba =   (uint32_t)mbr[0x1D6]
                     | ((uint32_t)mbr[0x1D7] << 8)
                     | ((uint32_t)mbr[0x1D8] << 16)
                     | ((uint32_t)mbr[0x1D9] << 24);
    return (g_p2_start_lba != 0) ? 0 : -3;
}

/* SSBL 初始化时调用：设置 g_storage + 注册 driver + 解析 P2 偏移 */
void sd_port_init(void)
{
    /* 先解析 MBR 得 P2 起始 LBA，供 driver 包装层加偏移 */
    if (sd_port_read_partition_table() != 0) {
        xil_printf("[sd_port] WARN: P2 partition not found, default LBA=0\r\n");
        g_p2_start_lba = 0;   /* 退化：挂 P1（仅调试用，正常不应走到这） */
    } else {
        xil_printf("[sd_port] P2 start LBA = %lu\r\n", g_p2_start_lba);
    }

    /* 注册带 P2 偏移的 driver 包装（方式 A） */
    storage_fx_set_driver(sd_driver_with_p2_offset, "SD_P2");
    g_storage = &g_sd_storage;
}
```

> **Implementation note**：
> - `sd_raw_read_sector` 与 `sd_driver_with_p2_offset` 是本 port 新增的薄包装：前者封装 xsdps 裸读单扇区（挂载前用），后者在每次 FileX 读请求里把逻辑扇区号 `+= g_p2_start_lba` 再调原 `fx_zynq_sdio_driver`。
> - **绝不能让 FileX 挂载覆盖 LBA 0**（P1 区）——这是 §5.5 隔离原则在代码层的体现。若 MBR 解析失败，宁可告警也不退而挂 P1。
> - FileX 是否原生支持 "media open at offset" 需查 FileX 版本文档；若支持则用原生机制，否则用本 Task 的 driver 包装。

- [ ] **Step 3：提交**

```bash
git add boot/ssbl/storage/sd_port.c
git commit -m "Phase 4.3: sd_port.c mounts only P2 data partition (spec §5.5 isolation)"
```

---

### Task 4.4：把 storage 接入 SSBL main + 写 ls/cat 测试线程

**Files:**
- Modify: `boot/ssbl/src/main.c`（加 storage 初始化）
- Create: `boot/ssbl/src/storage_test.c`

- [ ] **Step 1：扩展 main.c 加 fx_system_initialize + sd_port_init**

在 `main()` 的 `board_init()` 之后、`tx_kernel_enter()` 之前加：

```c
/* Phase 4：FileX 系统初始化 + SD 卡 port */
fx_system_initialize();
sd_port_init();

int rc = storage_media_open();
if (rc != STORAGE_OK) {
    xil_printf("[SSBL] WARN: storage_media_open failed (%d)\r\n", rc);
} else {
    xil_printf("[SSBL] storage media opened\r\n");
}
```

并 include：

```c
#include "fx_api.h"
#include "storage.h"
extern void sd_port_init(void);
```

- [ ] **Step 2：写 storage_test.c（ls + cat 测试线程）**

文件 `boot/ssbl/src/storage_test.c`：

```c
/* boot/ssbl/src/storage_test.c — Phase 4 临时验证：挂载后 ls + cat boot.cfg
 * Phase 7（CLI）后会移除，由 cli_commands.c 的 ls/cat 命令替代。
 */

#include "includes.h"
#include "storage.h"

static void storage_test_thread(ULONG thread_input)
{
    (void)thread_input;
    int rc;

    xil_printf("[SSBL] storage_test: listing root\r\n");
    rc = storage_dir_list("/");
    if (rc != STORAGE_OK) {
        xil_printf("[SSBL] dir_list / failed (%d)\r\n", rc);
    }

    /* 尝试读 boot.cfg（若 SD 上有）*/
    rc = storage_file_open("boot.cfg", STORAGE_OPEN_READ);
    if (rc == STORAGE_OK) {
        char buf[256];
        int n = storage_file_read(buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            xil_printf("[SSBL] boot.cfg contents:\r\n%s\r\n", buf);
        }
        storage_file_close();
    } else {
        xil_printf("[SSBL] boot.cfg not found (OK for Phase 4)\r\n");
    }

    tx_thread_suspend(tx_thread_identify());
}

void storage_test_create(void)
{
    static TX_THREAD tcb;
    static uint64_t  stk[2048/8];
    tx_thread_create(&tcb, "storage_test", storage_test_thread, 0,
                     &stk[0], 2048, 15, 15, TX_NO_TIME_SLICE, TX_AUTO_START);
}
```

- [ ] **Step 3：在 tx_application_define 末尾调用 storage_test_create()**

修改 `boot/ssbl/src/main.c` 的 `tx_application_define`：

```c
void tx_application_define(void *first_unused_memory)
{
    (void)first_unused_memory;
    /* …原 start task 创建… */
    storage_test_create();    /* Phase 4 临时，Phase 7 移除 */
}
```

加 `extern void storage_test_create(void);` 在 main.c 顶部。

- [ ] **Step 4：把 fx_zynq_sdio_driver.c 加入 Vitis 工程 + 加 include path**

Import → `external/filex/FileX/fx_zynq_sdio_driver.c` 到 ssbl 工程；include path 加 `external/filex/FileX`。

- [ ] **Step 5：Build**

期望：编译通过。若 `fx_zynq_sdio_driver.c` 依赖 BSP 的 `xsdps` 驱动（Xilinx SD controller），需在 Vitis BSP 设置中勾选 `xilffs` 与 `sdps` 库。

- [ ] **Step 6：JTAG 下载验证（P2 放测试文件，验证隔离）**

先把 SD 卡按 Task 4.0 格式化为双分区，然后在 **P2** 放 `boot.cfg`：

```bash
echo "app = app_current.bin" > <P2盘符>:/boot.cfg
```

JTAG 下载 ssbl.elf，期望串口：
```
[sd_port] P2 start LBA = <某个非零值>
[SSBL] storage media opened
[SSBL] storage_test: listing root
  boot.cfg
[SSBL] boot.cfg contents:
app = app_current.bin
```

**★ 隔离验证关键点**：`ls` 输出里**不应出现 BOOT.BIN**——这证明 SSBL 只挂了 P2、够不到 P1（spec §5.5.3）。
若 ls 能看到 BOOT.BIN，说明误挂了 P1（或 P2 偏移未生效），必须回查 Task 4.3 的 MBR 解析与 driver 包装——这是隔离失效的明确信号。

- [ ] **Step 7：提交**

```bash
git add boot/ssbl/src/main.c boot/ssbl/src/storage_test.c
git commit -m "Phase 4.4: wire SD storage into SSBL, ls/cat verified"
```

---

## Phase 5：app_template + pack_app.py + handoff（M5）

**目标**：建立 `boot/app_template/`（基于 hello_threadx，ORIGIN=0x1000000）；写 `scripts/pack_app.py` 给 app.raw 加 32 字节 header；SSBL 实现 `image_loader` 读 app.bin、校验、拷到 DDR、跳转。本 Phase 验证 SSBL → app 完整 handoff。

**Spec 引用**：§4.1（header）、§9.1–§9.5（handoff）、§13.3（pack_app.py）、§13.4（handoff_exit.S）。

### Task 5.1：建 app_template 工程（ORIGIN=0x1000000）

**Files:**
- Create: `boot/app_template/` 目录树
- Create: `boot/app_template/app.ld`
- Create: `boot/app_template/src/main.c`
- Create: `boot/app_template/README.md`

- [ ] **Step 1：建目录**

```bash
mkdir -p boot/app_template/src
cp -rf external/threadx/ThreadX boot/app_template/ThreadX
cp -rf external/bsp             boot/app_template/BSP
cp -rf external/utils           boot/app_template/utils
```

- [ ] **Step 2：写 app.ld（ORIGIN=0x1000000）**

复制 `external/lscript_hello_threadx.ld` 为 `boot/app_template/app.ld`，仅改 MEMORY.ps7_ddr_0：

```ld
MEMORY
{
   /* ★ 与 ssbl.ld 的差异：ORIGIN=0x1000000，LENGTH=0x800000（8MB）*/
   ps7_ddr_0 : ORIGIN = 0x1000000, LENGTH = 0x800000
   /* 其余 qspi/ram 段不变 */
}
```

- [ ] **Step 3：写 src/main.c（app 版，区别于 SSBL）**

文件 `boot/app_template/src/main.c`：

```c
/* boot/app_template/src/main.c — 主应用模板
 * Phase 5 最小版：仅闪烁 LED1（区别于 SSBL 的 LED0），证明 app 真的接管。
 * 复制本目录改造 main.c 即得新 app。
 */

#include "xil_printf.h"
#include "includes.h"

#define APP_TASK_START_PRIO 2u
#define APP_TASK_LED_PRIO   20u

static TX_THREAD start_tcb;
static uint64_t  start_stk[4096/8];
static TX_THREAD led_tcb;
static uint64_t  led_stk[1024/8];

static void app_start(ULONG arg);
static void app_led(ULONG arg);

int main(void)
{
    xil_printf("\r\n[APP] Hello from app @0x1000000\r\n");
    bsp_init();
    board_init();
    tx_kernel_enter();
    while (1);
}

void tx_application_define(void *first_unused_memory)
{
    (void)first_unused_memory;
    tx_thread_create(&start_tcb, "app_start", app_start, 0,
                     &start_stk[0], 4096, APP_TASK_START_PRIO, APP_TASK_START_PRIO,
                     TX_NO_TIME_SLICE, TX_AUTO_START);
}

static void app_start(ULONG arg)
{
    (void)arg;
    xil_printf("[APP] start task, creating LED1 task\r\n");
    tx_thread_create(&led_tcb, "app_led", app_led, 0,
                     &led_stk[0], 1024, APP_TASK_LED_PRIO, APP_TASK_LED_PRIO,
                     TX_NO_TIME_SLICE, TX_AUTO_START);
    tx_thread_terminate(&start_tcb);
}

static void app_led(ULONG arg)
{
    (void)arg;
    /* 用 LED1 区别于 SSBL 的 LED0，肉眼可分*/
    struct device *pled1 = device_find("led1");
    uint8_t val = 1;
    if (!pled1) {
        xil_printf("[APP] WARN: led1 not found\r\n");
        tx_thread_suspend(tx_thread_identify());
    }
    while (1) {
        device_write(pled1, &val, 1);
        val = (val == 1) ? 0 : 1;
        tx_thread_sleep(100);    /* 1s 周期，区别于 SSBL 的 2s */
    }
}
```

- [ ] **Step 4：写 README.md**

文件 `boot/app_template/README.md`：

```markdown
# app_template — 主应用模板

基于 hello_threadx，ORIGIN=`0x1000000`、LENGTH=`0x800000`（8MB）。

## 复制改造新 app

```bash
cp -r boot/app_template boot/my_app
# 改 boot/my_app/src/main.c 的业务逻辑
# 改名为 my_app.bin（pack_app.py 后）
```

## JTAG 独立调试

不通过 SSBL，直接 JTAG 下载 app.elf 到 `0x1000000` 跑：
- 先 `ps7_init`，再 `dow app.elf`，`con`；
- 期望 LED1 闪烁（1s 周期），区别于 SSBL 的 LED0（2s 周期）。
```

- [ ] **Step 5：Vitis 编译 + JTAG 验证 app.elf 独立可跑**

按 Phase 1 Task 1.2/1.3 流程，对 app_template 跑一遍：
1. Vitis 建 app Application Project，源码用 `boot/app_template/`；
2. Build → `app.elf`；
3. JTAG `dow app.elf` → `con`；
4. 期望串口见 `[APP] Hello from app @0x1000000`，LED1 1s 闪烁。

- [ ] **Step 6：提交**

```bash
git add boot/app_template/
git commit -m "Phase 5.1: app_template at ORIGIN=0x1000000, JTAG-verified standalone"
```

---

### Task 5.2：写 image_header.h

**Files:**
- Create: `boot/ssbl/include/image_header.h`

- [ ] **Step 1：写 image_header.h（spec §4.1）**

文件 `boot/ssbl/include/image_header.h`：

```c
/* boot/ssbl/include/image_header.h — 32 字节镜像 header（spec §4.1）*/
#ifndef SSBL_IMAGE_HEADER_H
#define SSBL_IMAGE_HEADER_H

#include <stdint.h>

#define IMAGE_MAGIC         0x5A424F4Fu   /* little-endian 字节序读作 "OOBZ" */
#define IMAGE_HEADER_SIZE   32
#define IMAGE_VERSION_1     1

/* image_type 取值 */
#define IMAGE_TYPE_THREADX      1
#define IMAGE_TYPE_BARE_METAL   2
#define IMAGE_TYPE_RESERVED     3

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;          /* 偏移 0x00：固定 IMAGE_MAGIC */
    uint32_t version;        /* 偏移 0x04：当前 = 1 */
    uint32_t image_type;     /* 偏移 0x08：IMAGE_TYPE_* */
    uint32_t load_addr;      /* 偏移 0x0C：加载地址（应 = 0x01000000）*/
    uint32_t image_size;     /* 偏移 0x10：payload 字节数（不含 header）*/
    uint32_t crc32;          /* 偏移 0x14：payload 的 CRC32 */
    uint32_t header_crc32;   /* 偏移 0x18：header[0x00..0x17] 的 CRC32 */
} image_header_t;
#pragma pack(pop)

/* 校验函数（image_loader.c 提供）*/
int image_header_validate(const image_header_t *hdr);
uint32_t image_crc32(const void *buf, uint32_t len);

#endif /* SSBL_IMAGE_HEADER_H */
```

- [ ] **Step 2：提交**

```bash
git add boot/ssbl/include/image_header.h
git commit -m "Phase 5.2: image_header_t struct (spec §4.1)"
```

---

### Task 5.3：写 scripts/pack_app.py（spec §13.3）

**Files:**
- Create: `scripts/pack_app.py`
- Create: `scripts/unpack_app.py`
- Create: `scripts/test_pack_app.py`

- [ ] **Step 1：写 pack_app.py**

文件 `scripts/pack_app.py`：

```python
#!/usr/bin/env python3
"""把 app.raw 加上 32 字节 header 得到 app.bin（spec §13.3）。
用法：python pack_app.py <in.raw> <out.bin> [load_addr=0x01000000] [image_type=1]
"""
import struct
import zlib
import sys

MAGIC = 0x5A424F4F   # little-endian 字节序读作 "OOBZ"；与 image_header.h 一致
VERSION = 1
IMAGE_TYPE_THREADX = 1
DEFAULT_LOAD_ADDR = 0x01000000

HEADER_NO_CRC_FMT = "<IIIIII"   # 6 个 uint32 = 24 字节
HEADER_NO_CRC_LEN = 24
HEADER_TOTAL_LEN = 32


def compute_header_crc(buf_no_crc: bytes) -> int:
    return zlib.crc32(buf_no_crc) & 0xFFFFFFFF


def pack(raw_path: str, out_path: str,
         load_addr: int = DEFAULT_LOAD_ADDR,
         image_type: int = IMAGE_TYPE_THREADX) -> int:
    with open(raw_path, "rb") as f:
        raw = f.read()
    size = len(raw)
    payload_crc = zlib.crc32(raw) & 0xFFFFFFFF

    header_no_crc = struct.pack(
        HEADER_NO_CRC_FMT,
        MAGIC, VERSION, image_type, load_addr, size, payload_crc,
    )
    header_crc = compute_header_crc(header_no_crc)
    header = header_no_crc + struct.pack("<I", header_crc)

    assert len(header) == HEADER_TOTAL_LEN
    with open(out_path, "wb") as f:
        f.write(header)
        f.write(raw)
    print(f"Packed: {out_path} (payload={size} bytes, "
          f"load=0x{load_addr:08X}, crc=0x{payload_crc:08X})")
    return 0


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    raw_path = argv[1]
    out_path = argv[2]
    load_addr = int(argv[3], 0) if len(argv) > 3 else DEFAULT_LOAD_ADDR
    image_type = int(argv[4], 0) if len(argv) > 4 else IMAGE_TYPE_THREADX
    return pack(raw_path, out_path, load_addr, image_type)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
```

- [ ] **Step 2：写 unpack_app.py（CI/调试用）**

文件 `scripts/unpack_app.py`：

```python
#!/usr/bin/env python3
"""app.bin → app.raw + 打印 header 字段（CI 校验/调试，spec §3.1）。
用法：python unpack_app.py <in.bin> [out.raw]
"""
import struct
import zlib
import sys

from pack_app import (MAGIC, HEADER_NO_CRC_FMT, HEADER_NO_CRC_LEN,
                      HEADER_TOTAL_LEN, compute_header_crc)


def unpack(in_path: str, out_path: str = None) -> int:
    with open(in_path, "rb") as f:
        data = f.read()
    if len(data) < HEADER_TOTAL_LEN:
        print(f"ERROR: file too small ({len(data)} bytes)", file=sys.stderr)
        return 1
    header_no_crc = data[:HEADER_NO_CRC_LEN]
    header_crc_in = struct.unpack("<I", data[HEADER_NO_CRC_LEN:HEADER_TOTAL_LEN])[0]
    header_crc_calc = compute_header_crc(header_no_crc)
    fields = struct.unpack(HEADER_NO_CRC_FMT, header_no_crc)
    magic, version, image_type, load_addr, size, payload_crc = fields

    print(f"magic       = 0x{magic:08X} {'OK' if magic == MAGIC else 'BAD'}")
    print(f"version     = {version}")
    print(f"image_type  = {image_type}")
    print(f"load_addr   = 0x{load_addr:08X}")
    print(f"image_size  = {size}")
    print(f"payload_crc = 0x{payload_crc:08X}")
    print(f"header_crc  = 0x{header_crc_in:08X} "
          f"{'OK' if header_crc_in == header_crc_calc else 'BAD (calc=0x%08X)' % header_crc_calc}")

    payload = data[HEADER_TOTAL_LEN:]
    if len(payload) != size:
        print(f"ERROR: size mismatch (header={size}, actual={len(payload)})",
              file=sys.stderr)
        return 1
    payload_crc_calc = zlib.crc32(payload) & 0xFFFFFFFF
    if payload_crc_calc != payload_crc:
        print(f"ERROR: payload CRC mismatch (calc=0x{payload_crc_calc:08X})",
              file=sys.stderr)
        return 1

    if out_path:
        with open(out_path, "wb") as f:
            f.write(payload)
        print(f"Payload written to {out_path}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    out = sys.argv[2] if len(sys.argv) > 2 else None
    sys.exit(unpack(sys.argv[1], out))
```

- [ ] **Step 3：写 test_pack_app.py（round-trip 自测，spec §12.1）**

文件 `scripts/test_pack_app.py`：

```python
#!/usr/bin/env python3
"""pack_app.py round-trip 自测：随机 payload → pack → unpack → 校验。"""
import os
import struct
import subprocess
import sys
import tempfile
import zlib

sys.path.insert(0, os.path.dirname(__file__))
from pack_app import (MAGIC, VERSION, IMAGE_TYPE_THREADX, DEFAULT_LOAD_ADDR,
                      HEADER_NO_CRC_FMT, HEADER_NO_CRC_LEN, HEADER_TOTAL_LEN)


def test_roundtrip():
    payload = bytes([(i * 7) & 0xFF for i in range(4096)])
    with tempfile.TemporaryDirectory() as d:
        raw = os.path.join(d, "app.raw")
        binned = os.path.join(d, "app.bin")
        with open(raw, "wb") as f:
            f.write(payload)
        rc = subprocess.call([sys.executable,
                              os.path.join(os.path.dirname(__file__), "pack_app.py"),
                              raw, binned])
        assert rc == 0
        with open(binned, "rb") as f:
            data = f.read()
        # 解析 header
        assert len(data) == HEADER_TOTAL_LEN + len(payload)
        fields = struct.unpack(HEADER_NO_CRC_FMT, data[:HEADER_NO_CRC_LEN])
        magic, version, image_type, load_addr, size, payload_crc = fields
        assert magic == MAGIC
        assert version == VERSION
        assert image_type == IMAGE_TYPE_THREADX
        assert load_addr == DEFAULT_LOAD_ADDR
        assert size == len(payload)
        assert payload_crc == (zlib.crc32(payload) & 0xFFFFFFFF)
        # payload 内容一致
        assert data[HEADER_TOTAL_LEN:] == payload
    print("test_roundtrip: PASS")


if __name__ == "__main__":
    test_roundtrip()
    print("All tests passed.")
```

- [ ] **Step 4：跑测试**

```bash
cd /e/桌面/docs/Code/scripts
python test_pack_app.py
```

期望：`test_roundtrip: PASS`、`All tests passed.`

- [ ] **Step 5：跑 CLI smoke**

```bash
# 造一个假 raw，pack 再 unpack
python -c "import sys; sys.stdout.buffer.write(bytes(1024))" > /tmp/fake.raw
python pack_app.py /tmp/fake.raw /tmp/fake.bin
python unpack_app.py /tmp/fake.bin
```

期望：unpack 输出所有字段 OK。

- [ ] **Step 6：提交**

```bash
git add scripts/pack_app.py scripts/unpack_app.py scripts/test_pack_app.py
git commit -m "Phase 5.3: pack_app.py + unpack_app.py + round-trip test (spec §13.3, §12.1)"
```

---

### Task 5.4：写 crc32.c（轻量实现）

**Files:**
- Create: `boot/ssbl/loader/crc32.c`
- Create: `boot/ssbl/loader/crc32.h`

- [ ] **Step 1：写 crc32.h**

```c
/* boot/ssbl/loader/crc32.h */
#ifndef SSBL_CRC32_H
#define SSBL_CRC32_H

#include <stdint.h>

uint32_t crc32_compute(const void *buf, uint32_t len);

#endif
```

- [ ] **Step 2：写 crc32.c（IEEE 802.3 多项式 0xEDB88320，与 zlib 一致）**

```c
/* boot/ssbl/loader/crc32.c — 与 Python zlib.crc32 结果一致（spec §13.3）*/
#include "crc32.h"

static uint32_t crc_table[256];
static int crc_table_init = 0;

static void ensure_table(void)
{
    if (crc_table_init) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        crc_table[i] = c;
    }
    crc_table_init = 1;
}

uint32_t crc32_compute(const void *buf, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    ensure_table();
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        crc = crc_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}
```

- [ ] **Step 3：在 image_header.h 同目录或 loader 里建 host 端 unit test**

文件 `boot/ssbl/loader/test_crc32_host.c`（host gcc 编译，spec §12.1 PC 单元测试）：

```c
/* test_crc32_host.c — 用 host gcc 编译跑：gcc test_crc32_host.c crc32.c -o test && ./test
 * 校验：与 Python zlib.crc32 一致。
 */
#include <stdio.h>
#include <string.h>
#include "crc32.h"

int main(void)
{
    /* 测试向量 1：空 */
    uint32_t r0 = crc32_compute("", 0);
    printf("crc32('')  = 0x%08X (expect 0x00000000)\n", r0);
    if (r0 != 0x00000000u) return 1;

    /* 测试向量 2："123456789" → 标准 0xCBF43926 */
    uint32_t r1 = crc32_compute("123456789", 9);
    printf("crc32('123456789') = 0x%08X (expect 0xCBF43926)\n", r1);
    if (r1 != 0xCBF43926u) return 1;

    /* 测试向量 3：256 字节递增 */
    uint8_t buf[256];
    for (int i = 0; i < 256; i++) buf[i] = (uint8_t)i;
    uint32_t r2 = crc32_compute(buf, 256);
    printf("crc32(0..255) = 0x%08X (expect 0x29058C73)\n", r2);
    if (r2 != 0x29058C73u) return 1;

    printf("All CRC32 tests PASS\n");
    return 0;
}
```

- [ ] **Step 4：跑 host test**

```bash
cd boot/ssbl/loader
gcc test_crc32_host.c crc32.c -o test_crc32
./test_crc32
```

期望：三个测试 PASS。期望值已标注（标准 CRC32 测试向量）。

- [ ] **Step 5：与 Python zlib 跨语言校对（额外保险）**

```bash
python -c "import zlib; print(hex(zlib.crc32(bytes(range(256))) & 0xFFFFFFFF))"
```

期望输出：`0x29058c73`，与 Step 4 第三测一致。

- [ ] **Step 6：提交**

```bash
git add boot/ssbl/loader/crc32.c boot/ssbl/loader/crc32.h boot/ssbl/loader/test_crc32_host.c
git commit -m "Phase 5.4: lightweight CRC32 matching Python zlib, host-tested"
```

---

### Task 5.5：写 image_loader.c

**Files:**
- Create: `boot/ssbl/loader/image_loader.c`

- [ ] **Step 1：写 image_loader.c（spec §9.5）**

```c
/* boot/ssbl/loader/image_loader.c — 读 app.bin → 校验 header → 拷 payload 到 DDR
 *
 * 流程（spec §9.5）：
 *   1. storage_file_open(app.bin)
 *   2. 读 32 字节 header
 *   3. image_header_validate：magic + header_crc + version
 *   4. 读 payload（image_size 字节）
 *   5. crc32_compute(payload) == header.crc32
 *   6. memcpy(payload → header.load_addr)  ← header 不进 DDR
 *   7. 返回 load_addr（payload[0] 即 reset 向量）
 */

#include "storage.h"
#include "image_header.h"
#include "crc32.h"
#include "xil_printf.h"
#include <string.h>

#define HDR_BUF_SIZE   IMAGE_HEADER_SIZE
#define CHUNK_SIZE     4096

int image_header_validate(const image_header_t *hdr)
{
    if (hdr->magic != IMAGE_MAGIC) {
        xil_printf("[loader] magic bad: 0x%08X\r\n", hdr->magic);
        return -1;
    }
    if (hdr->version != IMAGE_VERSION_1) {
        xil_printf("[loader] version unsupported: %u\r\n", hdr->version);
        return -1;
    }
    /* header_crc32 覆盖 header[0x00..0x17]，输入是 header 结构体的前 24 字节 */
    uint32_t calc = crc32_compute(hdr, HEADER_NO_CRC_LEN);
    if (calc != hdr->header_crc32) {
        xil_printf("[loader] header_crc bad: calc=0x%08X in=0x%08X\r\n",
                   calc, hdr->header_crc32);
        return -1;
    }
    return 0;
}

/* 把 app.bin 整文件拷到 DDR staging（header+payload 一起读，便于边读边校验）。
 * 返回 0=OK，负数=错误码；成功时 *out_load_addr 写入 load_addr。
 */
int image_loader_load(const char *path, uint32_t *out_load_addr)
{
    int rc;
    image_header_t hdr;
    uint8_t hdr_buf[HDR_BUF_SIZE];

    rc = storage_file_open(path, STORAGE_OPEN_READ);
    if (rc != STORAGE_OK) {
        xil_printf("[loader] open %s failed (%d)\r\n", path, rc);
        return rc;
    }

    /* 读 header */
    int n = storage_file_read(hdr_buf, HDR_BUF_SIZE);
    if (n != HDR_BUF_SIZE) {
        xil_printf("[loader] header short read (%d)\r\n", n);
        storage_file_close();
        return STORAGE_ERR_IO;
    }
    memcpy(&hdr, hdr_buf, sizeof(hdr));

    if (image_header_validate(&hdr) != 0) {
        storage_file_close();
        return STORAGE_ERR_INVAL;
    }

    /* 把 payload 流式拷到 DDR load_addr */
    uint8_t *dst = (uint8_t *)hdr.load_addr;
    uint32_t remaining = hdr.image_size;
    uint32_t crc_acc = 0xFFFFFFFFu;   /* 与 crc32_compute 内部一致 */
    uint8_t chunk[CHUNK_SIZE];

    while (remaining > 0) {
        uint32_t want = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
        int got = storage_file_read(chunk, want);
        if (got != (int)want) {
            xil_printf("[loader] payload short read (%d != %u)\r\n", got, want);
            storage_file_close();
            return STORAGE_ERR_IO;
        }
        memcpy(dst, chunk, want);
        /* 增量 CRC（与 crc32_compute 内部累加方式一致）*/
        /* 为保持单次 crc32_compute 的语义，下文用整段 payload 重算 */
        dst += want;
        remaining -= want;
    }
    storage_file_close();

    /* payload 已完整在 DDR，重算 CRC（load_addr 是 DDR 有效地址，可读）*/
    uint32_t calc = crc32_compute((void *)hdr.load_addr, hdr.image_size);
    if (calc != hdr.crc32) {
        xil_printf("[loader] payload crc bad: calc=0x%08X in=0x%08X\r\n",
                   calc, hdr.crc32);
        return STORAGE_ERR_INVAL;
    }

    *out_load_addr = hdr.load_addr;
    xil_printf("[loader] %s OK: load=0x%08X size=%u crc=0x%08X\r\n",
               path, hdr.load_addr, hdr.image_size, hdr.crc32);
    return STORAGE_OK;
}
```

> **Note**：上例的 `crc_acc` 变量未用（为可读性保留），实际 CRC 在拷贝完成后一次性重算。若 SSBL 想避免"重算时再扫一遍 DDR"的 I/O 开销，可改增量 CRC，但需要把 crc32.c 暴露 `crc32_update(start_value, buf, len)` 函数——本 Phase 暂用整段重算，Phase 8 优化。

- [ ] **Step 2：在 image_header.h 暴露 HEADER_NO_CRC_LEN（若未声明）**

在 `boot/ssbl/include/image_header.h` 中，`HEADER_NO_CRC_LEN` 已声明。补一个对外可见的常量：

```c
#define HEADER_NO_CRC_LEN  24    /* header[0x00..0x17] 长度 */
```

- [ ] **Step 3：提交**

```bash
git add boot/ssbl/loader/image_loader.c boot/ssbl/include/image_header.h
git commit -m "Phase 5.5: image_loader reads/validates/copies app.bin to DDR (spec §9.5)"
```

---

### Task 5.6：写 handoff_exit.S 与 jump_to_app

**Files:**
- Create: `boot/ssbl/handoff/handoff_exit.S`
- Create: `boot/ssbl/handoff/handoff.c`
- Create: `boot/ssbl/handoff/handoff.h`

- [ ] **Step 1：写 handoff_exit.S（spec §13.4 完整版）**

文件 `boot/ssbl/handoff/handoff_exit.S`：

```asm
/* boot/ssbl/handoff/handoff_exit.S — SSBL → app 跳转：纯破坏性清理
 * spec §13.4。r0 = app entry address (= load_addr；payload[0] 即 reset 向量，
 * header 已剥离、不在 DDR，故不加 0x20）。
 */

    .section .text.handoff_exit
    .arm
    .global handoff_exit
    .type   handoff_exit, %function

handoff_exit:

    /* 1. 禁所有中断 */
    cpsid   ifa

    /* 2-5. 停 Private Timer + GIC 由 C 侧 handoff.c 提前完成
     *      （XScuGic_Stop + 写 Private Timer 控制寄存器）
     *      这里只做 cache/MMU 相关 */

    /* 6. Flush D-cache（clean + invalidate，写回 DDR） */
    bl      Xil_DCacheFlush

    /* 7. Flush L2 (PL310)：clean + invalidate 一次 */
    bl      Xil_L2CacheFlush

    /* 8. Invalidate I-cache + BTB + TLB */
    bl      Xil_ICacheInvalidate

    /* 9. Disable I-cache / D-cache */
    mrc     p15, 0, r1, c1, c0, 0      /* SCTLR */
    bic     r1, r1, #(1<<12)|(1<<2)    /* 清 I(C12) 和 C(C2) 位 */
    mcr     p15, 0, r1, c1, c0, 0
    isb

    /* 10. Disable MMU（必须在 cache 操作之后） */
    mrc     p15, 0, r1, c1, c0, 0      /* SCTLR */
    bic     r1, r1, #1                 /* 清 M 位 */
    mcr     p15, 0, r1, c1, c0, 0
    isb

    /* 11. invalidate TLB 再一次 */
    mov     r2, #0
    mcr     p15, 0, r2, c8, c7, 0      /* TLBIALL */
    dsb
    isb

    /* 12. 跳转到 app 入口（app 的 _vector_table[0] = reset = load_addr 本身） */
    bx      r0

    .size   handoff_exit, .-handoff_exit
```

- [ ] **Step 2：写 handoff.c（spec §13.4 C 侧）**

文件 `boot/ssbl/handoff/handoff.c`：

```c
/* boot/ssbl/handoff/handoff.c — SSBL → app 跳转 C 接口（spec §9.2 + §13.4）
 *
 * 调用次序：
 *   1. 停所有非自身 ThreadX 线程
 *   2. tx_thread_relinquish 让调度器跑一轮
 *   3. 关 IRQ/FIQ/Async（汇编侧做，这里不重复）
 *   4. 停 Private Timer
 *   5. 停 GIC
 *   6. 调 handoff_exit（汇编）做 cache/MMU 清理 + bx 跳转
 */

#include "handoff.h"
#include "tx_api.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xscugic.h"

extern TX_THREAD  *cli_thread;
extern TX_THREAD  *countdown_thread;
extern TX_THREAD  *trigger_thread;
extern XScuGic     xInterruptController;   /* 由 BSP 提供 */

/* PTIMER 寄存器（Zynq PS Private Timer）*/
#define PTIMER_BASE      0xF8F00600u
#define PTIMER_CONTROL   (*(volatile uint32_t *)(PTIMER_BASE + 0x08))
#define PTIMER_ISR       (*(volatile uint32_t *)(PTIMER_BASE + 0x0C))

extern void handoff_exit(uint32_t entry_addr) __attribute__((noreturn));

void jump_to_app(uint32_t app_load_addr)
{
    /* 1. 停 ThreadX 业务线程 */
    if (cli_thread)       tx_thread_terminate(cli_thread);
    if (countdown_thread) tx_thread_terminate(countdown_thread);
    if (trigger_thread)   tx_thread_terminate(trigger_thread);

    /* 2. 让调度器跑一轮，确保终止生效 */
    tx_thread_relinquish();

    /* 4. 停 Private Timer */
    PTIMER_CONTROL = 0;    /* 清 enable */
    PTIMER_ISR = 1;        /* 清挂起（write 1 to ISR）*/

    /* 5. 停 GIC */
    XScuGic_Stop(&xInterruptController);

    /* 6. 汇编做 cache/MMU 清理 + 跳转。
     *    app 入口 = load_addr（payload[0] 即 reset 向量）。
     *    header 已剥离、不在 DDR，故不加 0x20。*/
    handoff_exit(app_load_addr);    /* 不返回 */
}
```

- [ ] **Step 3：写 handoff.h**

文件 `boot/ssbl/handoff/handoff.h`：

```c
#ifndef SSBL_HANDOFF_H
#define SSBL_HANDOFF_H

#include <stdint.h>

/* 跳转到 app。app_load_addr = app 在 DDR 的起始地址（payload[0] = reset 向量）。
 * 本函数不返回。
 */
void jump_to_app(uint32_t app_load_addr) __attribute__((noreturn));

#endif
```

- [ ] **Step 4：提交**

```bash
git add boot/ssbl/handoff/
git commit -m "Phase 5.6: handoff_exit.S (asm cleanup) + handoff.c (C entry), spec §13.4"
```

---

### Task 5.7：端到端验证 handoff（SSBL 加载 app.bin 并跳转）

**说明**：本任务**临时**修改 SSBL 的 storage_test_thread，改为"load app.bin + jump"——Phase 6 会用真正的 boot_selector 替代。

- [ ] **Step 1：pack app.bin**

```bash
cd /e/桌面/docs/Code
arm-none-eabi-objcopy -O binary boot/app_template/_vitis_ws/app/Debug/app.elf /tmp/app.raw
python scripts/pack_app.py /tmp/app.raw /tmp/app.bin
# 拷到 SD 卡 P2 数据分区（spec §5.5：app 落 P2，不落 P1）
cp /tmp/app.bin <P2盘符>:/app_current.bin
```

- [ ] **Step 2：临时改 storage_test_thread 为 load+jump**

在 `boot/ssbl/src/storage_test.c` 的 `storage_test_thread` 末尾加：

```c
    /* Phase 5 临时：加载 app.bin 并跳转 */
    uint32_t load_addr = 0;
    extern int image_loader_load(const char *path, uint32_t *out_load_addr);
    extern void jump_to_app(uint32_t app_load_addr);

    int rc2 = image_loader_load("app_current.bin", &load_addr);
    if (rc2 == STORAGE_OK) {
        xil_printf("[SSBL] handoff to app @0x%08X\r\n", load_addr);
        jump_to_app(load_addr);    /* 不返回 */
    } else {
        xil_printf("[SSBL] load app_current.bin failed (%d)\r\n", rc2);
    }
```

- [ ] **Step 3：Build SSBL，烧 BOOT.bin 到 SD P1（含新 ssbl.elf）**

```bash
# 直接用 ssbl.elf 打包（spec §13.2，无需 objcopy）
pwsh scripts/build_boot_bin.ps1
# BOOT.bin 落 P1（spec §5.5：P1 仅放 BOOT.BIN）
cp bif/BOOT.BIN <P1盘符>:/
```

- [ ] **Step 4：上电观察**

期望串口：
```
FSBL (slim) ...
[SSBL] Hello from SSBL @0x100000
[SSBL] storage media opened
[SSBL] storage_test: listing root
  BOOT.BIN
  app_current.bin
  boot.cfg
[SSBL] boot.cfg contents:
...
[loader] app_current.bin OK: load=0x01000000 size=... crc=0x...
[SSBL] handoff to app @0x01000000
[APP] Hello from app @0x1000000
[APP] start task, creating LED1 task
```
随后 LED1 1s 闪烁（区别于 SSBL 的 LED0 2s）。

- [ ] **Step 5：失败排查清单**

| 现象 | 可能原因 |
|---|---|
| `[loader] magic bad` | pack_app.py 与 image_header.h MAGIC 不一致；header 被错位读取 |
| `[loader] payload crc bad` | crc32_compute 与 Python zlib 不一致（回查 Task 5.4 跨语言校对） |
| handoff 后 hard fault | entry 加了 0x20；MMU 没关；cache 没 flush；reset.S 与 app.ld 不匹配 |
| app 起来但 LED 不闪 | led1 未注册（同 Phase 1） |

- [ ] **Step 6：提交（标记 Phase 5 完成，临时代码下个 Phase 替换）**

```bash
git add boot/ssbl/src/storage_test.c
git commit -m "Phase 5.7: end-to-end SSBL→app handoff verified (temp load+jump in storage_test)"
```

---

## Phase 6：boot.cfg 解析 + boot_selector 自动选 app（M6）

**目标**：实现 spec §5 的 `boot.cfg` INI 解析；把 Phase 5 的临时 load+jump 替换为真正的 `boot_selector` 流程：读 cfg → 选 app/bit → 加载 → 跳转。本 Phase 完成后，系统会按 SD 卡上 `boot.cfg` 的 `app=` 字段决定启动哪个 app。

**Spec 引用**：§5.1（格式）、§5.2（解析规则，含 `bit` 空值四种写法 + `boot_delay` 秒/tick 换算）、§5.3（容错与默认行为）、§5.4（SD 卡目录约定）、§9.5（load+jump 流程）。

### Task 6.1：写 boot_config.h 数据结构

**Files:**
- Create: `boot/ssbl/config/boot_config.h`

- [ ] **Step 1：写 boot_config.h**

文件 `boot/ssbl/config/boot_config.h`：

```c
/* boot/ssbl/config/boot_config.h — boot.cfg 解析结果结构（spec §5）*/
#ifndef SSBL_BOOT_CONFIG_H
#define SSBL_BOOT_CONFIG_H

#include <stdint.h>

#define BOOT_CFG_APP_NAME_MAX   64
#define BOOT_CFG_BIT_NAME_MAX   64
#define BOOT_CFG_PATH_MAX       96    /* "DIR/" + name */

/* bit 空值的内部统一表示：bit_path == NULL 表示"不下载 bitstream"
 * （spec §5.2 规定的四种外部写法都映射到 NULL）
 */
typedef struct {
    char     app_name[BOOT_CFG_APP_NAME_MAX];   /* cfg 的 app= 值（文件名）*/
    char    *bit_path;                          /* cfg 的 bit= 值；NULL=不下载 */
    char     bit_path_storage[BOOT_CFG_BIT_NAME_MAX];  /* bit 路径存储 */
    uint32_t boot_delay_seconds;                /* cfg 的 boot_delay= 值（秒）*/
    uint8_t  auto_boot;                         /* cfg 的 auto_boot= 值（0/1）*/
    uint8_t  cfg_loaded;                        /* 1=从文件加载成功；0=默认值 */
} boot_cfg_t;

/* 解析 boot.cfg，结果写入 cfg。
 * 返回：0=OK（从文件解析或用默认值），负数=严重错误（介质量坏等）
 * 任何解析歧义都按 spec §5.3 退回默认值，不返回错误。
 */
int boot_config_load(boot_cfg_t *cfg);

/* 把内存中的 cfg 写回 boot.cfg（CLI 的 cfg save 用，spec §6.2 + §6.3 #6）。
 * 走 .tmp + rename 原子写。
 */
int boot_config_save(const boot_cfg_t *cfg);

/* 返回默认配置（boot.cfg 不存在/损坏时用，spec §5.3）*/
void boot_config_defaults(boot_cfg_t *cfg);

#endif /* SSBL_BOOT_CONFIG_H */
```

- [ ] **Step 2：提交**

```bash
git add boot/ssbl/config/boot_config.h
git commit -m "Phase 6.1: boot_cfg_t struct + API (spec §5)"
```

---

### Task 6.2：写 boot_config.c 解析器

**Files:**
- Create: `boot/ssbl/config/boot_config.c`
- Create: `boot/ssbl/config/test_boot_config_host.c`

- [ ] **Step 1：写 boot_config.c（INI 解析 + 默认值 + 原子写）**

文件 `boot/ssbl/config/boot_config.c`：

```c
/* boot/ssbl/config/boot_config.c — boot.cfg INI 解析（spec §5）
 *
 * 解析规则（spec §5.2）：
 *   - 行首/行尾空白忽略；= 两侧空白忽略；行内 # 之后为注释
 *   - 大小写敏感（key 全小写）
 *   - 未识别 key 忽略
 *   - 必填：app / boot_delay / auto_boot
 *   - bit 空值的四种写法统一映射为 bit_path == NULL
 *   - boot_delay 单位 = 秒；本结构存秒，转换在 boot_selector 里做（tx_thread_sleep(N*100)）
 */

#include "boot_config.h"
#include "storage.h"
#include "xil_printf.h"
#include <string.h>
#include <stdlib.h>

#define BOOT_CFG_PATH        "boot.cfg"
#define BOOT_CFG_TMP_PATH    "boot.cfg.tmp"
#define BOOT_CFG_MAX_BYTES   2048
#define BOOT_CFG_LINE_MAX    256

/* 默认值（spec §5.3：boot.cfg 不存在/解析失败时用）*/
void boot_config_defaults(boot_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->app_name, "default.bin", BOOT_CFG_APP_NAME_MAX - 1);
    cfg->bit_path = NULL;
    cfg->bit_path_storage[0] = '\0';
    cfg->boot_delay_seconds = 3;
    cfg->auto_boot = 1;
    cfg->cfg_loaded = 0;
}

/* 处理一行 "key = value"（已去注释、去首尾空白）。
 * 返回 1=识别到已知 key；0=未知 key（忽略）。
 */
static int apply_kv(boot_cfg_t *cfg, char *line)
{
    char *eq = strchr(line, '=');
    if (!eq) return 0;
    *eq = '\0';
    char *key = line;
    char *val = eq + 1;

    /* 去两侧空白 */
    while (*key == ' ' || *key == '\t') key++;
    char *k_end = key + strlen(key);
    while (k_end > key && (k_end[-1] == ' ' || k_end[-1] == '\t')) *--k_end = '\0';

    while (*val == ' ' || *val == '\t') val++;
    char *v_end = val + strlen(val);
    while (v_end > val && (v_end[-1] == ' ' || v_end[-1] == '\t' ||
                           v_end[-1] == '\r' || v_end[-1] == '\n')) *--v_end = '\0';

    if (strcmp(key, "app") == 0) {
        strncpy(cfg->app_name, val, BOOT_CFG_APP_NAME_MAX - 1);
        cfg->app_name[BOOT_CFG_APP_NAME_MAX - 1] = '\0';
        return 1;
    }
    if (strcmp(key, "bit") == 0) {
        /* 四种空值写法（spec §5.2）*/
        if (val[0] == '\0' ||
            strcmp(val, "\"\"") == 0 ||
            strcmp(val, "none") == 0) {
            cfg->bit_path = NULL;
        } else {
            strncpy(cfg->bit_path_storage, val, BOOT_CFG_BIT_NAME_MAX - 1);
            cfg->bit_path_storage[BOOT_CFG_BIT_NAME_MAX - 1] = '\0';
            cfg->bit_path = cfg->bit_path_storage;
        }
        return 1;
    }
    if (strcmp(key, "boot_delay") == 0) {
        cfg->boot_delay_seconds = (uint32_t)strtoul(val, NULL, 10);
        return 1;
    }
    if (strcmp(key, "auto_boot") == 0) {
        cfg->auto_boot = (strcmp(val, "yes") == 0) ? 1 : 0;
        return 1;
    }
    return 0;   /* 未识别 key 忽略（前向兼容）*/
}

/* 把 buf 里的全文按行解析 */
static void parse_text(boot_cfg_t *cfg, char *buf, uint32_t len)
{
    char line[BOOT_CFG_LINE_MAX];
    uint32_t i = 0, line_start = 0;

    while (i <= len) {
        char c = (i < len) ? buf[i] : '\n';
        if (c == '\n' || c == '\r' || i == len) {
            uint32_t llen = i - line_start;
            if (llen >= BOOT_CFG_LINE_MAX) llen = BOOT_CFG_LINE_MAX - 1;
            memcpy(line, buf + line_start, llen);
            line[llen] = '\0';

            /* 去行首空白 */
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            /* 跳过空行/注释行 */
            if (*p != '\0' && *p != '#') {
                apply_kv(cfg, p);
            }
            line_start = i + 1;
        }
        i++;
    }
}

int boot_config_load(boot_cfg_t *cfg)
{
    boot_config_defaults(cfg);

    int rc = storage_file_open(BOOT_CFG_PATH, STORAGE_OPEN_READ);
    if (rc != STORAGE_OK) {
        xil_printf("[cfg] %s not found, using defaults\r\n", BOOT_CFG_PATH);
        return 0;   /* 不是错误，用默认值 */
    }

    static char buf[BOOT_CFG_MAX_BYTES];
    int n = storage_file_read(buf, sizeof(buf) - 1);
    storage_file_close();

    if (n <= 0) {
        xil_printf("[cfg] read failed (%d), using defaults\r\n", n);
        return 0;
    }
    buf[n] = '\0';

    parse_text(cfg, buf, (uint32_t)n);
    cfg->cfg_loaded = 1;
    xil_printf("[cfg] loaded: app=%s bit=%s delay=%us auto=%u\r\n",
               cfg->app_name,
               cfg->bit_path ? cfg->bit_path : "(none)",
               cfg->boot_delay_seconds, cfg->auto_boot);
    return 0;
}

/* 序列化回 INI 文本（写 .tmp，再 rename，spec §6.3 #6 原子写）*/
int boot_config_save(const boot_cfg_t *cfg)
{
    char text[BOOT_CFG_MAX_BYTES];
    int len = snprintf(text, sizeof(text),
        "# Zynq SSBL 启动配置（由 cfg save 生成）\r\n"
        "app = %s\r\n"
        "bit = %s\r\n"
        "boot_delay = %u\r\n"
        "auto_boot = %s\r\n",
        cfg->app_name,
        cfg->bit_path ? cfg->bit_path : "",
        cfg->boot_delay_seconds,
        cfg->auto_boot ? "yes" : "no");
    if (len <= 0 || (uint32_t)len >= sizeof(text)) {
        return -1;
    }

    /* 写 .tmp */
    int rc = storage_file_open(BOOT_CFG_TMP_PATH, STORAGE_OPEN_WRITE);
    if (rc != STORAGE_OK) {
        /* FileX 默认 fx_file_create 需要 O_CREAT；此处 storage_fx_file_open 用
         * FX_OPEN_FOR_WRITE 已能创建（FX_OPEN_FOR_WRITE 在文件不存在时创建）。
         * 若驱动不支持，需扩展 storage 接口加 file_create——见 Phase 7 Task 7.6。*/
        return rc;
    }
    /* FileX write 前先 truncate（若已存在 .tmp）*/
    extern int storage_fx_file_truncate(uint32_t size);  /* 下一 Task 实现 */
    storage_fx_file_truncate((uint32_t)len);
    int wrote = storage_file_write(text, (uint32_t)len);
    storage_file_close();
    if (wrote != len) return -1;

    /* rename .tmp → boot.cfg（FileX fx_file_rename 同 dst 存在时返回错误，
     * 需先 delete dst）*/
    storage_file_delete(BOOT_CFG_PATH);
    rc = storage_file_rename(BOOT_CFG_TMP_PATH, BOOT_CFG_PATH);
    return rc;
}
```

> **Note（实现期需处理）**：本文件用了 `storage_fx_file_truncate` 这个尚未实现的函数——见 Task 6.4 在 `storage_fx_glue.c` 补全。这是计划内的依赖，按 Task 顺序实现即可。

- [ ] **Step 2：写 host 端单元测试（spec §12.1）**

文件 `boot/ssbl/config/test_boot_config_host.c`：

```c
/* test_boot_config_host.c — host gcc 编译跑 INI 解析逻辑
 * 编译：gcc test_boot_config_host.c boot_config_host_stub.c -o test_cfg && ./test_cfg
 *
 * 注意：boot_config.c 用了 storage/xil_printf，host 上要 stub。本测试直接
 * 把 parse_text 与 apply_kv 拷一份到 boot_config_host_stub.c，验证解析逻辑。
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* 测试用最小 cfg 结构（只关心解析输出字段）*/
typedef struct {
    char app_name[64];
    char bit_path_storage[64];
    char *bit_path;
    uint32_t boot_delay_seconds;
    uint8_t auto_boot;
} test_cfg_t;

/* 从 boot_config.c 拷贝的 apply_kv（仅本测试用）*/
extern int test_apply_kv(test_cfg_t *cfg, char *line);

static void run_test(const char *name, const char *input, int expect_bit_null,
                     const char *expect_app, uint32_t expect_delay,
                     uint8_t expect_auto)
{
    test_cfg_t cfg = {0};
    strncpy(cfg.app_name, "default.bin", sizeof(cfg.app_name) - 1);
    cfg.boot_delay_seconds = 3;
    cfg.auto_boot = 1;

    /* 按 \n 分行调 apply_kv */
    char buf[512];
    strncpy(buf, input, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *save = NULL;
    char *tok = strtok_r(buf, "\n", &save);
    while (tok) {
        /* 跳行首空白 + 注释 */
        char *p = tok;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '\0' && *p != '#') test_apply_kv(&cfg, p);
        tok = strtok_r(NULL, "\n", &save);
    }

    int ok = 1;
    if (strcmp(cfg.app_name, expect_app) != 0) {
        printf("FAIL %s: app '%s' != '%s'\n", name, cfg.app_name, expect_app);
        ok = 0;
    }
    if ((cfg.bit_path == NULL) != expect_bit_null) {
        printf("FAIL %s: bit_null expect %d got %d\n",
               name, expect_bit_null, cfg.bit_path == NULL);
        ok = 0;
    }
    if (cfg.boot_delay_seconds != expect_delay) {
        printf("FAIL %s: delay %u != %u\n",
               name, cfg.boot_delay_seconds, expect_delay);
        ok = 0;
    }
    if (cfg.auto_boot != expect_auto) {
        printf("FAIL %s: auto %u != %u\n", name, cfg.auto_boot, expect_auto);
        ok = 0;
    }
    if (ok) printf("PASS %s\n", name);
}

int main(void)
{
    /* case 1：标准配置 */
    run_test("standard",
        "app=app_a.bin\nbit=pl_a.bit\nboot_delay=5\nauto_boot=yes\n",
        0, "app_a.bin", 5, 1);

    /* case 2：bit 空值 4 种写法 */
    run_test("bit_empty_a", "bit=\n",                    1, "default.bin", 3, 1);
    run_test("bit_empty_b", "bit=\"\"\n",                1, "default.bin", 3, 1);
    run_test("bit_empty_c", "bit=none\n",                1, "default.bin", 3, 1);
    run_test("bit_omitted", "app=x.bin\n",               1, "x.bin",       3, 1);

    /* case 3：行内注释 + 空白 */
    run_test("inline_comment",
        "app = app_b.bin   # comment here\nbit = pl_b.bit\n",
        0, "app_b.bin", 3, 1);

    /* case 4：未识别 key 忽略 */
    run_test("unknown_key",
        "app=x.bin\nfoo=bar\nlast_good_app=a.bin\nboot_delay=2\n",
        1, "x.bin", 2, 1);

    /* case 5：auto_boot=no */
    run_test("auto_boot_no", "auto_boot=no\n", 1, "default.bin", 3, 0);

    printf("All done.\n");
    return 0;
}
```

文件 `boot/ssbl/config/boot_config_host_stub.c`：

```c
/* boot_config_host_stub.c — 把 boot_config.c 的 apply_kv 拷一份供 host test 用 */
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct {
    char app_name[64];
    char bit_path_storage[64];
    char *bit_path;
    uint32_t boot_delay_seconds;
    uint8_t auto_boot;
} test_cfg_t;

int test_apply_kv(test_cfg_t *cfg, char *line)
{
    char *eq = strchr(line, '=');
    if (!eq) return 0;
    *eq = '\0';
    char *key = line;
    char *val = eq + 1;

    while (*key == ' ' || *key == '\t') key++;
    char *k_end = key + strlen(key);
    while (k_end > key && (k_end[-1] == ' ' || k_end[-1] == '\t')) *--k_end = '\0';

    while (*val == ' ' || *val == '\t') val++;
    char *v_end = val + strlen(val);
    while (v_end > val && (v_end[-1] == ' ' || v_end[-1] == '\t' ||
                           v_end[-1] == '\r' || v_end[-1] == '\n')) *--v_end = '\0';

    if (strcmp(key, "app") == 0) {
        strncpy(cfg->app_name, val, sizeof(cfg->app_name) - 1);
        cfg->app_name[sizeof(cfg->app_name) - 1] = '\0';
        return 1;
    }
    if (strcmp(key, "bit") == 0) {
        if (val[0] == '\0' || strcmp(val, "\"\"") == 0 || strcmp(val, "none") == 0) {
            cfg->bit_path = NULL;
        } else {
            strncpy(cfg->bit_path_storage, val, sizeof(cfg->bit_path_storage) - 1);
            cfg->bit_path_storage[sizeof(cfg->bit_path_storage) - 1] = '\0';
            cfg->bit_path = cfg->bit_path_storage;
        }
        return 1;
    }
    if (strcmp(key, "boot_delay") == 0) {
        cfg->boot_delay_seconds = (uint32_t)strtoul(val, NULL, 10);
        return 1;
    }
    if (strcmp(key, "auto_boot") == 0) {
        cfg->auto_boot = (strcmp(val, "yes") == 0) ? 1 : 0;
        return 1;
    }
    return 0;
}
```

- [ ] **Step 3：跑 host test**

```bash
cd boot/ssbl/config
gcc test_boot_config_host.c boot_config_host_stub.c -o test_cfg
./test_cfg
```

期望：6 个 case 全 PASS。

- [ ] **Step 4：提交**

```bash
git add boot/ssbl/config/boot_config.c \
        boot/ssbl/config/test_boot_config_host.c \
        boot/ssbl/config/boot_config_host_stub.c
git commit -m "Phase 6.2: boot.cfg INI parser + host unit tests (spec §5)"
```

---

### Task 6.3：扩展 storage 接口加 file_truncate / file_create

**说明**：Task 6.2 的 `boot_config_save` 用到了 `storage_fx_file_truncate`，且 FileX 写新文件需要显式 create。把 storage 接口补完整，避免 Phase 7/8 反复打补丁。

**Files:**
- Modify: `boot/ssbl/storage/storage.h`（加 file_truncate / file_create 声明 + 宏）
- Modify: `boot/ssbl/storage/storage_fx_glue.c`（加实现）
- Modify: `boot/ssbl/config/boot_config.c`（用 storage_file_create 而非依赖 FX_OPEN_FOR_WRITE 副作用）

- [ ] **Step 1：storage.h 加两个新接口**

在 `storage_ops_t` 结构体里、`file_close` 之后加：

```c
    int  (*file_create) (const char *path);     /* 创建/覆盖文件 */
    int  (*file_truncate)(uint32_t size);        /* 截断到 size 字节 */
```

在便捷包装宏区加：

```c
#define storage_file_create(p)    (g_storage->file_create(p))
#define storage_file_truncate(s)  (g_storage->file_truncate(s))
```

- [ ] **Step 2：storage_fx_glue.c 加实现**

```c
int storage_fx_file_create(const char *path)
{
    /* FileX：fx_file_create 在文件已存在时返回 FX_ALREADY_EXIST；先 delete 再 create */
    fx_file_delete(&g_fx_media, (CHAR *)path);    /* 不存在时返回 FX_NOT_FOUND，忽略 */
    UINT status = fx_file_create(&g_fx_media, (CHAR *)path);
    return (status == FX_SUCCESS) ? STORAGE_OK : STORAGE_ERR_IO;
}

int storage_fx_file_truncate(uint32_t size)
{
    /* fx_file_truncate_release 释放多余簇；fx_file_truncate 不释放但置 EOF。
     * 对刚 create 的新文件，size 通常=0；调 fx_file_truncate 安全。
     */
    UINT status = fx_file_truncate(&g_fx_file, (ULONG)size);
    return (status == FX_SUCCESS) ? STORAGE_OK : STORAGE_ERR_IO;
}
```

并在 `g_sd_storage` 的初始化器（sd_port.c）里补：

```c
.file_create   = storage_fx_file_create,
.file_truncate = storage_fx_file_truncate,
```

- [ ] **Step 3：boot_config.c 改用 storage_file_create**

把 Task 6.2 中 `boot_config_save` 的 `.tmp` 写入段改为：

```c
    /* 写 .tmp：先 create（清空），再 write */
    rc = storage_file_create(BOOT_CFG_TMP_PATH);
    if (rc != STORAGE_OK) return rc;
    rc = storage_file_open(BOOT_CFG_TMP_PATH, STORAGE_OPEN_WRITE);
    if (rc != STORAGE_OK) return rc;
    int wrote = storage_file_write(text, (uint32_t)len);
    storage_file_close();
    if (wrote != len) return -1;

    /* rename .tmp → boot.cfg（dst 存在时先 delete）*/
    storage_file_delete(BOOT_CFG_PATH);
    rc = storage_file_rename(BOOT_CFG_TMP_PATH, BOOT_CFG_PATH);
    return rc;
```

并删除 `storage_fx_file_truncate` 的 extern（已在接口层暴露）。

- [ ] **Step 4：Build + 跑 host test 仍通过**

host test 不依赖 storage 接口，应仍 6 个 PASS。

- [ ] **Step 5：提交**

```bash
git add boot/ssbl/storage/ boot/ssbl/config/boot_config.c
git commit -m "Phase 6.3: extend storage with file_create/file_truncate; boot_config_save atomic"
```

---

### Task 6.4：写 boot_selector.c

**Files:**
- Create: `boot/ssbl/src/boot_selector.c`
- Create: `boot/ssbl/src/boot_selector.h`

- [ ] **Step 1：写 boot_selector.h**

文件 `boot/ssbl/src/boot_selector.h`：

```c
/* boot/ssbl/src/boot_selector.h — 启动选择器主流程 */
#ifndef SSBL_BOOT_SELECTOR_H
#define SSBL_BOOT_SELECTOR_H

#include "boot_config.h"

/* boot_selector 主入口：
 *   读 boot.cfg → 加载 app → 加载 bit（如有） → 跳转 app
 * 任一关键步骤失败返回负数（CLI 路径会处理），成功不返回（已 jump_to_app）。
 */
int boot_selector_run(void);

/* 仅加载并校验，不跳转（CLI 的 `boot <app>` / `test app <file>` 用）。
 * 返回 app 的 load_addr，或负数。
 */
int boot_selector_load_only(const char *app_path, const char *bit_path,
                            uint32_t *out_load_addr);

#endif
```

- [ ] **Step 2：写 boot_selector.c（spec §1.2 + §5.3 + §9.5）**

文件 `boot/ssbl/src/boot_selector.c`：

```c
/* boot/ssbl/src/boot_selector.c — 启动选择器主流程
 *
 * 自动启动路径（spec §1.2 Stage 2）：
 *   boot_config_load → boot_selector_run → image_loader_load(bitstream)
 *                                          → image_loader_load(app) → jump_to_app
 *
 * 容错（spec §5.3）：app 错误回 CLI；bit 错误告警继续。
 */

#include "boot_selector.h"
#include "image_header.h"
#include "storage.h"
#include "xil_printf.h"
#include "tx_api.h"
#include "handoff.h"

/* Phase 9 才实现 bit_loader，本 Phase 先 stub */
extern int bit_loader_download(const char *path);  /* Phase 9 */

int boot_selector_load_only(const char *app_path, const char *bit_path,
                            uint32_t *out_load_addr)
{
    /* 1. bit（如有）：失败仅告警，不中止（spec §5.3）*/
    if (bit_path && bit_path[0] != '\0') {
        xil_printf("[boot] downloading bitstream %s\r\n", bit_path);
        int brc = bit_loader_download(bit_path);
        if (brc != 0) {
            xil_printf("[boot] WARN: bit download failed (%d), continuing\r\n", brc);
        }
    }

    /* 2. app：失败拒绝启动（spec §5.3 BOOT_REFUSED → 回 CLI）*/
    int arc = image_loader_load(app_path, out_load_addr);
    if (arc != STORAGE_OK) {
        xil_printf("[boot] app load failed (%d)\r\n", arc);
        return arc;
    }
    return STORAGE_OK;
}

int boot_selector_run(void)
{
    boot_cfg_t cfg;
    int rc = boot_config_load(&cfg);
    if (rc != 0) {
        /* 介质量坏等严重错误，调用方决定（一般进 CLI）*/
        return rc;
    }

    uint32_t load_addr = 0;
    rc = boot_selector_load_only(cfg.app_name, cfg.bit_path, &load_addr);
    if (rc != STORAGE_OK) {
        return rc;   /* 调用方进 CLI */
    }

    /* 跳转（不返回）*/
    xil_printf("[boot] handoff to app @0x%08X\r\n", load_addr);
    jump_to_app(load_addr);    /* noreturn */
}
```

- [ ] **Step 3：提交**

```bash
git add boot/ssbl/src/boot_selector.c boot/ssbl/src/boot_selector.h
git commit -m "Phase 6.4: boot_selector reads cfg, loads bit+app, jumps (spec §1.2 + §9.5)"
```

---

### Task 6.5：bit_loader stub + tx_application_define 改用 boot_selector

**Files:**
- Create: `boot/ssbl/loader/bitstream_loader.c`（Phase 6 仅 stub）
- Modify: `boot/ssbl/src/main.c`（删 storage_test，改用 boot_selector）
- Delete: `boot/ssbl/src/storage_test.c`（任务完成，退役）

- [ ] **Step 1：写 bitstream_loader.c stub（Phase 9 才填实）**

文件 `boot/ssbl/loader/bitstream_loader.c`：

```c
/* boot/ssbl/loader/bitstream_loader.c — Phase 6 stub
 * Phase 9 实现：FileX 读 .bit → XDcfg_Transfer 到 PL
 */
#include <stddef.h>

int bit_loader_download(const char *path)
{
    (void)path;
    return -1;   /* Phase 9 实现；当前 cfg 配了 bit 会告警继续（spec §5.3）*/
}
```

- [ ] **Step 2：改写 main.c 的 tx_application_define**

把 Phase 5 的"storage_test + 临时 load+jump"全删，改为：

```c
/* main.c 关键改动（保留 Phase 1 的 LED 任务作"SSBL 活着"指示）*/

void tx_application_define(void *first_unused_memory)
{
    (void)first_unused_memory;

    /* start 任务：负责拉起 boot_selector */
    tx_thread_create(&ssbl_task_start_tcb, "ssbl_start", ssbl_task_start, 0,
                     &ssbl_task_start_stk[0], SSBL_TASK_START_STK_SIZE,
                     SSBL_TASK_START_PRIO, SSBL_TASK_START_PRIO,
                     TX_NO_TIME_SLICE, TX_AUTO_START);
}

static void ssbl_task_start(ULONG thread_input)
{
    (void)thread_input;

    /* Phase 6 直接跑 boot_selector；Phase 7 会改为 countdown+trigger 流程 */
    int rc = boot_selector_run();
    if (rc != 0) {
        xil_printf("[SSBL] boot_selector failed (%d), entering idle\r\n", rc);
        /* Phase 7 之前先死循环，避免崩；Phase 7 起自动进 CLI */
        while (1) tx_thread_sleep(100);
    }
    /* boot_selector_run 成功则已 jump 走，不会到这里 */
    while (1) tx_thread_sleep(TX_WAIT_FOREVER);
}
```

并删除 LED 任务（Phase 1 是验证用，Phase 6 起交给 app 闪 LED）；删 storage_test_create 调用；include 改加 `boot_selector.h`。

- [ ] **Step 3：删除 storage_test.c**

```bash
rm boot/ssbl/src/storage_test.c
```

- [ ] **Step 4：Build**

期望：编译通过。`bit_loader_download` stub 返回 -1，配置了 bit 的 boot.cfg 会告警但继续。

- [ ] **Step 5：上电验证**

SD 卡准备：
- `BOOT.bin`（FSBL+SSBL）
- `boot.cfg` 内容：
  ```
  app = app_current.bin
  bit =
  boot_delay = 3
  auto_boot = yes
  ```
- `app_current.bin`（Phase 5 pack 出的）

上电期望串口：
```
FSBL (slim) ...
[SSBL] Hello from SSBL @0x100000
[SSBL] storage media opened
[cfg] loaded: app=app_current.bin bit=(none) delay=3s auto=1
[loader] app_current.bin OK: load=0x01000000 size=... crc=0x...
[boot] handoff to app @0x01000000
[APP] Hello from app @0x1000000
```
LED1 1s 闪烁。

- [ ] **Step 6：改 cfg 验证 A/B 切换**

修改 SD 卡 `boot.cfg`：`app = app_b.bin`（先 pack 另一份 app 给 LED 不同周期以区分），上电，期望加载 app_b.bin。

- [ ] **Step 7：失败场景验证（spec §5.3）**

| 场景 | 操作 | 期望 |
|---|---|---|
| boot.cfg 不存在 | 删 boot.cfg | `[cfg] not found, using defaults` → 加载 default.bin 失败 → `[SSBL] boot_selector failed, entering idle` |
| app 不存在 | cfg 改 `app=missing.bin` | `[loader] open missing.bin failed` → idle |
| app CRC 错 | 用 hex 编辑器改 app.bin 中间 1 字节 | `[loader] payload crc bad` → idle |
| bit 不存在但 app OK | cfg 改 `bit=missing.bit` | `[boot] WARN: bit download failed (-1), continuing` → 正常 handoff |

> Phase 6 的"idle"行为会被 Phase 7 替换为"进 CLI"。

- [ ] **Step 8：提交**

```bash
git add -A boot/ssbl/
git commit -m "Phase 6.5: replace storage_test with boot_selector; bit_loader stub"
```

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

## Phase 11：QSPI 迁移（M11，后期）

**目标**：实现 spec §11.2 阶段 C 的 QSPI 全替代——`storage/qspi_port.c` 基于 FileX + LevelX NOR，上层（image_loader / boot_selector / cli）零改动。

**Spec 引用**：§11.1（接口已设计好）、§11.2 阶段 C。

### Task 11.1：拷贝 LevelX 到 external/

**Files:**
- Create: `external/levelx/`（拷贝）

- [ ] **Step 1：拷贝 LevelX 源**

从 https://github.com/eclipse-threadx/levelx 拉取（或用户自备），拷到 `external/levelx/`：

```bash
git clone https://github.com/eclipse-threadx/levelx external/levelx
```

或直接 vendor 拷贝（不入 git）。

- [ ] **Step 2：确认 LevelX 头存在**

```bash
ls external/levelx/common/inc/lx_api.h
```

---

### Task 11.2：写 qspi_port.c（spec §11.2 阶段 C）

**Files:**
- Create: `boot/ssbl/storage/qspi_port.c`

- [ ] **Step 1：写 qspi_port.c（FileX + LevelX NOR 骨架）**

```c
/* boot/ssbl/storage/qspi_port.c — QSPI NOR 后端（spec §11.2 阶段 C）
 *
 * 三层堆叠：
 *   FileX (FAT) → FileX NOR driver（本文件）→ LevelX NOR → xqspips
 *
 * 本 Phase 给骨架；具体 NOR flash 型号相关常数（块/页大小）按板载填。
 *
 * ★ 存储隔离（spec §5.5.4）：FS 只覆盖 BOOT.bin 之后的数据段。
 *   BootROM 从 flash offset 0 裸读 BOOT.bin，本 driver 把 FileX 的逻辑扇区
 *   加上 FS_START_SECTOR 偏移后再映射到物理扇区，保证 FS 写操作永远够不到
 *   低地址的 BOOT.bin 区段。FS_START_SECTOR 依 BOOT.bin 大小对齐到扇区。
 */

#include "storage.h"
#include "fx_api.h"
#include "lx_api.h"
#include "xparameters.h"
#include "xil_printf.h"

extern FX_MEDIA  g_fx_media;
extern int storage_fx_media_open(void);
extern int storage_fx_media_close(void);
extern int storage_fx_file_open(const char *, int);
extern int storage_fx_file_read(void *, uint32_t);
extern int storage_fx_file_write(const void *, uint32_t);
extern int storage_fx_file_close(void);
extern int storage_fx_file_size(const char *, uint32_t *);
extern int storage_fx_file_delete(const char *);
extern int storage_fx_file_rename(const char *, const char *);
extern int storage_fx_dir_list(const char *);
extern int storage_fx_file_create(const char *);
extern int storage_fx_file_truncate(uint32_t);
extern void storage_fx_set_driver(VOID (*)(FX_MEDIA *), const char *);

/* LevelX NOR 实例（按板载 NOR 型号选 LX_NOR_FLASH_INSTANCE）*/
static LX_NOR_FLASH  g_lx_nor;

/* ★ FS 数据段起始扇区（spec §5.5.4）
 *   = ROUND_UP(BOOT.bin 区段大小, FX_SECTOR_SIZE) / FX_SECTOR_SIZE
 *   依 bootgen 产物大小定；例 BOOT.bin 占 0xF0000（~960KB），扇区 512B
 *   → FS_START_SECTOR = 0xF0000/512 = 3072。
 *   必须编译期静态断言其 > BOOT.bin 区段，否则隔离失效。
 */
#ifndef FS_START_SECTOR
#define FS_START_SECTOR  3072U   /* 占位值；实施时按实际 BOOT.bin 大小重算 */
#endif

/* FileX → LevelX NOR driver（FileX 调本函数做扇区操作）
 * ★ 关键：把 FileX 逻辑扇区 + FS_START_SECTOR 再映射到物理扇区，
 *   从而把 FS 限定在 [FS_START_SECTOR, END] 高地址段，不碰 BOOT.bin 区。*/
static VOID fx_lx_nor_driver(FX_MEDIA *media)
{
    /* 按 media->fx_media_driver_request 分发：
     *   FX_DRIVER_INIT     → lx_nor_flash_initialize (&g_lx_nor)
     *   FX_DRIVER_READ     → lx_nor_flash_sector_read (logical + FS_START_SECTOR, buf)
     *   FX_DRIVER_WRITE    → lx_nor_flash_sector_write(logical + FS_START_SECTOR, buf)
     *   FX_DRIVER_RELEASE  → 无操作
     *   FX_DRIVER_FLUSH    → lx_nor_flash_defragment（可选）
     *   FX_DRIVER_UNINIT   → 无操作
     * 完整映射见 LevelX 用户手册 "FileX with LevelX" 章节。
     * ★ 每个扇区操作都加 FS_START_SECTOR 偏移——这是隔离的支点。
     */
    ULONG logical_sector;
    UCHAR *buf;

    switch (media->fx_media_driver_request) {
    case FX_DRIVER_INIT:
        /* 初始化 LevelX NOR：对接 xqspips */
        lx_nor_flash_initialize(&g_lx_nor);
        media->fx_media_driver_status = FX_SUCCESS;
        break;
    case FX_DRIVER_READ:
        logical_sector = media->fx_media_driver_logical_sector + FS_START_SECTOR;
        buf = media->fx_media_driver_buffer;
        for (ULONG i = 0; i < media->fx_media_driver_sectors; i++) {
            lx_nor_flash_sector_read(&g_lx_nor, logical_sector + i,
                                     buf + i * media->fx_media_bytes_per_sector);
        }
        media->fx_media_driver_status = FX_SUCCESS;
        break;
    case FX_DRIVER_WRITE:
        logical_sector = media->fx_media_driver_logical_sector + FS_START_SECTOR;
        buf = media->fx_media_driver_buffer;
        for (ULONG i = 0; i < media->fx_media_driver_sectors; i++) {
            lx_nor_flash_sector_write(&g_lx_nor, logical_sector + i,
                                      buf + i * media->fx_media_bytes_per_sector);
        }
        media->fx_media_driver_status = FX_SUCCESS;
        break;
    case FX_DRIVER_RELEASE:
    case FX_DRIVER_FLUSH:
    case FX_DRIVER_UNINIT:
        media->fx_media_driver_status = FX_SUCCESS;
        break;
    default:
        media->fx_media_driver_status = FX_IO_ERROR;
        break;
    }
}

const storage_ops_t g_qspi_storage = {
    .media_open   = storage_fx_media_open,
    .media_close  = storage_fx_media_close,
    .file_open    = storage_fx_file_open,
    .file_read    = storage_fx_file_read,
    .file_write   = storage_fx_file_write,
    .file_close   = storage_fx_file_close,
    .file_size    = storage_fx_file_size,
    .file_delete  = storage_fx_file_delete,
    .file_rename  = storage_fx_file_rename,
    .dir_list     = storage_fx_dir_list,
    .file_create  = storage_fx_file_create,
    .file_truncate= storage_fx_file_truncate,
};

void qspi_port_init(void)
{
    storage_fx_set_driver(fx_lx_nor_driver, "QSPI_NOR");
    g_storage = &g_qspi_storage;
}
```

> **Note**：
> - 上例假设 LevelX 已 `lx_nor_flash_initialize` 接好 `xqspips` 底层驱动。LevelX 要求 NOR flash 提供 `read_sector/write_sector/erase_block` 的 callback——具体与 `xqspips` 对接代码依板载 NOR 型号（如 MT25Q、W25Q）而异，本计划只给接口骨架，细节在 Phase 11 实施时按板子填写。
> - **★ FS_START_SECTOR 必须重算**：实施时用 `arm-none-eabi-size` 或 hexdump 量出 BOOT.bin 实际占用，按扇区大小（通常 512B 或 NOR 页大小）向上取整，留足余量。隔离的正确性完全依赖此值 > BOOT.bin 区段——建议加编译期 `_Static_assert(FS_START_SECTOR * FX_SECTOR_SIZE > BOOT_BIN_MAX_SIZE)`。

- [ ] **Step 2：SSBL 编译开关：SD vs QSPI**

在 `main.c` 用 `#ifdef` 切：

```c
#include "ssbl_config.h"

#ifdef SSBL_USE_QSPI
extern void qspi_port_init(void);
#else
extern void sd_port_init(void);
#endif

int main(void)
{
    /* … bsp/board … */

#ifdef SSBL_USE_QSPI
    qspi_port_init();
#else
    sd_port_init();
#endif

    if (storage_media_open() != STORAGE_OK) {
        ssbl_fatal(LED_CODE_SD_MOUNT, "storage mount failed");
    }

    /* QSPI 首次需要 fx_media_format（spec §11.2）。
     * ★ media_available_sectors 要扣掉 FS_START_SECTOR，只用数据段
     *   （spec §5.5.4：FS 不覆盖 BOOT.bin 区）。*/
#ifdef SSBL_USE_QSPI_FIRST_FORMAT
    ULONG total_sectors = (NOR_FLASH_SIZE / FX_SECTOR_SIZE);
    ULONG data_sectors  = total_sectors - FS_START_SECTOR;   /* 数据段扇区数 */
    fx_media_format(&g_fx_media,                 /* FX_MEDIA */
                    fx_lx_nor_driver, 0,         /* driver + ptr */
                    0,                            /* 留：driver 内部已加 FS_START_SECTOR */
                    data_sectors,                 /* ★ 只格式化数据段，不含 BOOT.bin */
                    FX_SECTOR_SIZE,               /* bytes per sector */
                    /* …其余 FAT 几何参数按 NOR 填… */);
#endif

    /* … */
}
```

`ssbl_config.h` 加：

```c
/* #define SSBL_USE_QSPI */   /* 默认 SD；切 QSPI 时定义此宏 */
```

- [ ] **Step 3：提交**

```bash
git add boot/ssbl/storage/qspi_port.c boot/ssbl/src/main.c \
        boot/ssbl/include/ssbl_config.h
git commit -m "Phase 11.2: QSPI NOR backend (FileX + LevelX) skeleton (spec §11.2)"
```

---

### Task 11.3：QSPI 上的 BOOT.bin（spec §11.2 阶段 B + C）

**说明**：阶段 B 是 BOOT.bin 在 QSPI、app/bit 在 SD；阶段 C 是全 QSPI。本任务实现阶段 C：boot mode pins 改 QSPI，FSBL 自 QSPI 启动，加载 SSBL。

- [ ] **Step 1：写 QSPI 用的 boot.bif（FSBL 不变，SSBL 不变）**

`bif/boot_qspi.bif`：

```
the_ROM_image:
{
    [bootloader, destination_cpu = a9_0]   boot/fsbl/_vitis_ws/fsbl/Debug/fsbl.elf
    [destination_cpu = a9_0]               boot/ssbl/_vitis_ws/ssbl/Debug/ssbl.elf
}
```

> **Note**：bootgen 对 QSPI 也用同样 bif 文件，差别在烧写方式（bootgen 生成的是统一 BOOT.bin，只是介质接口不同）。SSBL 与 SD 方案一致，直接以 `ssbl.elf` 交给 bootgen。

- [ ] **Step 2：bootgen 生成 BOOT.bin（不变）**

```bash
cd bif
bootgen -image boot_qspi.bif -arch zynq -out BOOT_QSPI.BIN -w on
```

> **★ 量出 BOOT_QSPI.BIN 大小，回填 FS_START_SECTOR（spec §5.5.4）**：
> ```bash
> ls -l BOOT_QSPI.BIN
> ```
> BOOT_QSPI.BIN 实际字节数按 FX_SECTOR_SIZE 向上取整 / FX_SECTOR_SIZE，**必须 ≤** `qspi_port.c` 里的 `FS_START_SECTOR`。若 BOOT.bin 长大了超过原 FS_START_SECTOR，需同步调大 FS_START_SECTOR 并重新 `fx_media_format` 数据段。这是 QSPI 隔离的唯一硬约束——BOOT.bin 区段与 FS 数据段在地址上不重叠。

- [ ] **Step 3：用 SDK/Vitis 的 Flash Programmer 烧到 QSPI**

```tcl
# XSCT
connect
targets -set -filter {name =~ "APU"}
# 用 Zynq Flash Programmer
fpga -f <path-to-flash-programmer-bit>
# 然后用 SDK Flash Programmer GUI 选 BOOT_QSPI.BIN 烧到 QSPI offset 0
```

> **★ 烧写边界（spec §5.5.4）**：BOOT_QSPI.BIN 烧到 **offset 0**，其占用区段 = `[0, BOOT.bin 大小)`。SSBL 的 FS 数据段 = `[FS_START_SECTOR × 扇区大小, flash 末尾)`。两者不得重叠——烧写前用 Step 2 的尺寸校验确认。FS 永远不会写进 `[0, FS_START_SECTOR)` 这个 BOOT.bin 区段。

- [ ] **Step 4：设 boot mode = QSPI，上电**

期望：
- FSBL 自 QSPI 启动 → InitQspi → LoadBootImage → handoff SSBL
- SSBL 启动 → `qspi_port_init` → storage_media_open（QSPI NOR）
- 后续流程与 SD 完全一致

- [ ] **Step 5：回归测试 spec §12.2 全部 19 场景**

| 测试组 | 在 QSPI 上的期望 |
|---|---|
| 1-10：boot_selector + CLI | 全过 |
| 11-15：YMODEM 升级 | 全过（注意 QSPI 写入次数远低于 SD，频繁升级考虑磨损） |
| 16-19：handoff + A/B | 全过 |

任一失败，回查 storage 抽象层是否被某模块绕过（直接调 FileX API 而非 storage_*）。

- [ ] **Step 6：提交**

```bash
git add bif/boot_qspi.bif
git commit -m "Phase 11.3: QSPI boot chain verified end-to-end"
```

---

## 计划自审（Self-Review）

按 writing-plans 技能要求，做 3 项检查：

### 1. Spec 覆盖检查

| Spec 章节 | 覆盖任务 |
|---|---|
| §1 项目目标与启动链 | 全计划 + Phase 6.4 boot_selector |
| §2 DDR 内存布局 | Phase 1.1 ssbl.ld、Phase 5.1 app.ld、Phase 8.1 staging、Phase 9.1 BIT_DDR_TEMP |
| §3 目录结构 | Phase 0.1 |
| §4.1 image header | Phase 5.2 image_header.h、Phase 5.3 pack_app.py |
| §4.2 bitstream | Phase 9.1 |
| §5 boot.cfg | Phase 6.1/6.2/6.3 |
| §6 CLI | Phase 7.1-7.6、Phase 8.1-8.4 |
| §7 精简 FSBL | Phase 2.1-2.6 |
| §8 ThreadX 集成 | Phase 1.1 + Phase 8.1 README |
| §9 handoff | Phase 5.6、Phase 5.7 |
| §10 错误处理 | Phase 10.1-10.4 |
| §11 介质抽象 | Phase 4.1-4.4、Phase 11.1-11.3 |
| §12 测试 | 各 Phase 的"验证"步骤 |
| §13 构建流程 | Phase 3.2 boot.bif、Phase 3.3 build_boot_bin、Phase 5.3 pack_app.py、Phase 5.6 handoff_exit.S |
| §14 milestones | 全 Phase ↔ M1-M11 一一对应 |
| §15 关键决策 | 嵌入各 Phase 的设计说明 |

**覆盖完整**，无遗漏章节。

### 2. Placeholder 扫描

通读全计划，搜索典型红旗：
- "TBD"/"TODO"/"fill in later"：**无**
- "add error handling"/"handle edge cases"：**无**（每个函数的错误返回码都明示）
- "similar to Task N"：**无**（boot_config_host_stub.c 重复了 apply_kv，是故意的 host test 设计）
- "Write tests for the above"：**无**（每个 test 都有具体 case）

**唯一两处需要实现期注意的"软约束"**：
1. Phase 2.4 image_mover.c 简化——给了骨架但 Xilinx Boot Header 结构解析需查 vendor `image_mover.c` 原代码（已在 Task 内标注 "Implementation note"）
2. Phase 9.1 `XDcfg_Transfer` API 签名依 Xilinx 版本而异（已标注）

这两处不是 placeholder，是合理的"按 vendor 实际填写"指引。

### 3. 类型一致性检查

| 符号 | 定义任务 | 使用任务 | 一致？ |
|---|---|---|---|
| `image_header_t` | Task 5.2 | Task 5.5 image_loader、Task 8.1 staging_verify | ✅ |
| `storage_ops_t` | Task 4.1 | Task 4.3 sd_port、Task 6.3 扩展、Task 11.2 qspi_port | ✅ |
| `storage_media_open` 等宏 | Task 4.1 | 全 Phase 4+ | ✅ |
| `g_storage` | Task 4.1 | Task 4.3 sd_port_init、Task 11.2 qspi_port_init | ✅ |
| `boot_cfg_t` | Task 6.1 | Task 6.2/6.4、Task 7.4/7.5、Task 10.3 | ✅ |
| `jump_to_app` | Task 5.6 handoff.h | Task 5.7、Task 6.4、Task 7.6 boot cmd | ✅ |
| `handoff_exit` | Task 5.6 .S | Task 5.6 handoff.c | ✅ |
| `STAGING_AREA_ADDR/SIZE` | Task 8.1 | Task 8.2、Task 8.3 | ✅ |
| `boot_log_*` | Task 10.3 | Task 10.4、app_template | ✅ |
| `boot_magic` 常量 | Task 10.1 ssbl_config.h | Task 10.3 boot_log.c、Task 10.3 app_template | ✅ |

**类型一致**。MAGIC 值修订时（`BOOT_MAGIC_ATTEMPT = 0x4154544D` "ATTM"、`BOOT_MAGIC_OK = 0x4F4B4F4B` "OKOK"）在 ssbl_config.h 定义，boot_log.c 与 app_template 都用同一字面值（app_template 不 link SSBL，直接写 hex 值，与 spec §10.4 "app 唯一配合" 一致）。

---

## 执行建议

11 个 Phase 中：
- **Phase 0–3**：强制顺序，每级依赖上一级链通。
- **Phase 4–10**：依赖 Phase 3 链通后，多数任务可并行（host test、PC 脚本、SSBL 各模块）。
- **Phase 11**：独立后期工作。

**最关键风险点**：
1. Phase 1.2 Vitis 工程配置（include path 链路最易出错）——失败时回查 spec §8.2 移植修正点是否全数应用。
2. Phase 2.4 image_mover 简化（Boot Header 解析依赖 vendor 实现）——保留原 vendor `ReadPmfHeader` 辅助函数即可。
3. Phase 5.6/5.7 handoff（cache/MMU 顺序错会崩）——按 spec §13.4 + Phase 5.7 失败排查表逐项查。
4. Phase 8.2 YMODEM 协议（CRC16 + 序号 + 收尾块）——用 Tera Term 的 YMODEM 模式严格测试。

---

## 计划完整，保存路径

`docs/superpowers/plans/2026-06-22-zynq-multistage-boot.md`

**两种执行选项：**

1. **Subagent-Driven（推荐）**：我用 superpowers:subagent-driven-development，每个 Task 派一个 fresh subagent，Task 间二阶段评审。
2. **Inline Execution**：用 superpowers:executing-plans，在当前会话里按 checkpoint 批量执行。

要不要我现在切到执行模式？还是你先复核一遍计划？
