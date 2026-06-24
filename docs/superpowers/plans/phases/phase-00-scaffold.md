> **章节导航**：
  [↑ 返回总述](../2026-06-22-zynq-multistage-boot.md) ｜ [下一章：SSBL 工程骨架 →](phase-01-ssbl-skeleton.md)

> **Phase 元信息**
> - 对应 Spec：`§3`
> - 里程碑：`—`
> - 交付物：顶层目录骨架 + vendor 拷贝脚本
> - 分章文件：`phases/phase-00-scaffold.md`

> **接口定位**：本章对其他 Phase 的依赖与对外暴露的符号，见
> [总述章 · 模块依赖矩阵](../2026-06-22-zynq-multistage-boot.md#3-模块依赖矩阵)。
> 新增功能时，先查该清单确认接口落点，再回本章扩展。

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
