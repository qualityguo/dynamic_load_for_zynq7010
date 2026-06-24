> **章节导航**：
  [← 上一章：错误处理 + LED 错误码 + OCM 标记](phase-10-error-handling.md) ｜ [↑ 返回总述](../2026-06-22-zynq-multistage-boot.md) ｜ [下一章：NetXDuo 集成 + Web 配置页 + 网络升级 →](phase-12-network-web.md)

> **Phase 元信息**
> - 对应 Spec：`§11.2`
> - 里程碑：`M11`
> - 交付物：QSPI 迁移（FileX + LevelX NOR）
> - 分章文件：`phases/phase-11-qspi.md`

> **接口定位**：本章对其他 Phase 的依赖与对外暴露的符号，见
> [总述章 · 模块依赖矩阵](../2026-06-22-zynq-multistage-boot.md#3-模块依赖矩阵)。
> 新增功能时，先查该清单确认接口落点，再回本章扩展。

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
