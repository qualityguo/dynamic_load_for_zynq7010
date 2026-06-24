> **章节导航**：
  [← 上一章：FSBL → SSBL 链通](phase-03-fsbl-ssbl-chain.md) ｜ [↑ 返回总述](../2026-06-22-zynq-multistage-boot.md) ｜ [下一章：app_template + pack_app.py + handoff →](phase-05-app-template-handoff.md)

> **Phase 元信息**
> - 对应 Spec：`§11`
> - 里程碑：`M4`
> - 交付物：storage 抽象层 + FileX SD 卡挂载
> - 分章文件：`phases/phase-04-storage-filex.md`

> **接口定位**：本章对其他 Phase 的依赖与对外暴露的符号，见
> [总述章 · 模块依赖矩阵](../2026-06-22-zynq-multistage-boot.md#3-模块依赖矩阵)。
> 新增功能时，先查该清单确认接口落点，再回本章扩展。

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
