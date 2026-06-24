> **章节导航**：
  [← 上一章：精简 FSBL](phase-02-fsbl.md) ｜ [↑ 返回总述](../2026-06-22-zynq-multistage-boot.md) ｜ [下一章：storage 抽象层 + FileX SD 挂载 →](phase-04-storage-filex.md)

> **Phase 元信息**
> - 对应 Spec：`§13.2`
> - 里程碑：`M3`
> - 交付物：FSBL → SSBL 链通（boot.bif + BOOT.BIN）
> - 分章文件：`phases/phase-03-fsbl-ssbl-chain.md`

> **接口定位**：本章对其他 Phase 的依赖与对外暴露的符号，见
> [总述章 · 模块依赖矩阵](../2026-06-22-zynq-multistage-boot.md#3-模块依赖矩阵)。
> 新增功能时，先查该清单确认接口落点，再回本章扩展。

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
