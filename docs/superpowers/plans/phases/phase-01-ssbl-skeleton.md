> **章节导航**：
  [← 上一章：顶层目录骨架与 vendor 拷贝](phase-00-scaffold.md) ｜ [↑ 返回总述](../2026-06-22-zynq-multistage-boot.md) ｜ [下一章：精简 FSBL →](phase-02-fsbl.md)

> **Phase 元信息**
> - 对应 Spec：`§8`
> - 里程碑：`M1`
> - 交付物：SSBL 工程骨架（基于 hello_threadx，ORIGIN=0x100000）
> - 分章文件：`phases/phase-01-ssbl-skeleton.md`

> **接口定位**：本章对其他 Phase 的依赖与对外暴露的符号，见
> [总述章 · 模块依赖矩阵](../2026-06-22-zynq-multistage-boot.md#3-模块依赖矩阵)。
> 新增功能时，先查该清单确认接口落点，再回本章扩展。

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
