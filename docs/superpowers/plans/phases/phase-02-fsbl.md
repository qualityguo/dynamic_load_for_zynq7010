> **章节导航**：
  [← 上一章：SSBL 工程骨架](phase-01-ssbl-skeleton.md) ｜ [↑ 返回总述](../2026-06-22-zynq-multistage-boot.md) ｜ [下一章：FSBL → SSBL 链通 →](phase-03-fsbl-ssbl-chain.md)

> **Phase 元信息**
> - 对应 Spec：`§7`
> - 里程碑：`M2`
> - 交付物：精简 FSBL（裁剪现有 zynq_fsbl）
> - 分章文件：`phases/phase-02-fsbl.md`

> **接口定位**：本章对其他 Phase 的依赖与对外暴露的符号，见
> [总述章 · 模块依赖矩阵](../2026-06-22-zynq-multistage-boot.md#3-模块依赖矩阵)。
> 新增功能时，先查该清单确认接口落点，再回本章扩展。

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
