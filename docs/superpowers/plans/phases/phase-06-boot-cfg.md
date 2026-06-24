> **章节导航**：
  [← 上一章：app_template + pack_app.py + handoff](phase-05-app-template-handoff.md) ｜ [↑ 返回总述](../2026-06-22-zynq-multistage-boot.md) ｜ [下一章：CLI 子系统 →](phase-07-cli.md)

> **Phase 元信息**
> - 对应 Spec：`§5`
> - 里程碑：`M6`
> - 交付物：boot.cfg 解析 + boot_selector 自动选 app
> - 分章文件：`phases/phase-06-boot-cfg.md`

> **接口定位**：本章对其他 Phase 的依赖与对外暴露的符号，见
> [总述章 · 模块依赖矩阵](../2026-06-22-zynq-multistage-boot.md#3-模块依赖矩阵)。
> 新增功能时，先查该清单确认接口落点，再回本章扩展。

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
