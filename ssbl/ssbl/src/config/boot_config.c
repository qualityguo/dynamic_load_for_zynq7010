/*
*********************************************************************************************************
*
*	模块名称 : boot.cfg INI 解析器（spec §5，Phase 6 / Task 6.2）
*	文件名称 : boot_config.c
*	版    本 : V1.0
*	说    明 : 解析 SD 卡 P2 根目录的 boot.cfg，结果写入 boot_cfg_t。
*
*	解析规则（spec §5.2）：
*	   - 行首/行尾空白忽略；= 两侧空白忽略；行内 # 之后为注释
*	   - 大小写敏感（key 全小写）
*	   - 未识别 key 忽略（前向兼容，如 last_good_app=）
*	   - 已知 key：app / bit / boot_delay / auto_boot
*	   - bit 空值的四种写法（空 / "" / none / 缺省）统一映射为 bit_path == NULL
*	   - boot_delay 单位 = 秒；本结构存秒，tick 换算在 countdown/boot_selector 里做
*
*	容错（spec §5.3）：boot.cfg 不存在/读失败/任一行解析歧义 → 退回默认值，
*	   不返回错误（只有介质量坏等严重错误才返回负数）。
*
*	原子写（spec §6.3 #6）：boot_config_save 走 boot.cfg.tmp → delete → rename，
*	   断电至多残留 .tmp，不会损坏正在用的 boot.cfg。
*
*	注：apply_kv 的逻辑在 boot_config_host_stub.c 有一份逐行镜像副本，供
*	   test_boot_config_host.c 在 PC 上 gcc 编译跑单测。改本函数时请同步改副本。
*
*********************************************************************************************************
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

/*
*********************************************************************************************************
*                                默认值（spec §5.3）
*********************************************************************************************************
*/
void boot_config_defaults(boot_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->app_name, "default.bin", BOOT_CFG_APP_NAME_MAX - 1);
    cfg->bit_path             = NULL;
    cfg->bit_path_storage[0]  = '\0';
    cfg->boot_delay_seconds   = 3;
    cfg->auto_boot            = 1;
    cfg->cfg_loaded           = 0;
}

/*
*********************************************************************************************************
*                                处理一行 "key = value"（已去注释、去行首空白）
*	返 回 值 : 1 = 识别到已知 key；0 = 未知 key（忽略）
*	注       : 本函数会就地修改 line（在 '=' 处截断）。逻辑与
*	           boot_config_host_stub.c::test_apply_kv 必须保持一致。
*********************************************************************************************************
*/
static int apply_kv(boot_cfg_t *cfg, char *line)
{
    /* 行内注释：spec §5.2 “行内 # 之后为注释”——截断首个 # */
    char *hash = strchr(line, '#');
    if (hash) *hash = '\0';

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
        /* 四种空值写法（spec §5.2）：空 / "" / none → 不下载 bitstream */
        if (val[0] == '\0' ||
            strcmp(val, "\"\"") == 0 ||
            strcmp(val, "none") == 0) {
            cfg->bit_path            = NULL;
            cfg->bit_path_storage[0] = '\0';
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

/*
*********************************************************************************************************
*                                把 buf 里的全文按行解析
*********************************************************************************************************
*/
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

/*
*********************************************************************************************************
*                                解析 boot.cfg
*********************************************************************************************************
*/
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
               (unsigned)cfg->boot_delay_seconds, (unsigned)cfg->auto_boot);
    return 0;
}

/*
*********************************************************************************************************
*                                序列化回 INI 文本，原子写回（.tmp + rename）
*********************************************************************************************************
*/
int boot_config_save(const boot_cfg_t *cfg)
{
    char text[BOOT_CFG_MAX_BYTES];
    int len = snprintf(text, sizeof(text),
        "# Zynq SSBL boot config\r\n"
        "app = %s\r\n"
        "bit = %s\r\n"
        "boot_delay = %u\r\n"
        "auto_boot = %s\r\n",
        cfg->app_name,
        cfg->bit_path ? cfg->bit_path : "",
        (unsigned)cfg->boot_delay_seconds,
        cfg->auto_boot ? "yes" : "no");
    if (len <= 0 || (uint32_t)len >= sizeof(text)) {
        return -1;
    }

    /* 1. 写 .tmp：create（覆盖语义，清空残留）→ open(write) → write → close */
    int rc = storage_file_create(BOOT_CFG_TMP_PATH);
    if (rc != STORAGE_OK) return rc;
    rc = storage_file_open(BOOT_CFG_TMP_PATH, STORAGE_OPEN_WRITE);
    if (rc != STORAGE_OK) return rc;
    int wrote = storage_file_write(text, (uint32_t)len);
    storage_file_close();
    if (wrote != len) return -1;

    /* 2. rename .tmp → boot.cfg（fx_file_rename 在 dst 存在时返回错误，先 delete dst）*/
    storage_file_delete(BOOT_CFG_PATH);
    rc = storage_file_rename(BOOT_CFG_TMP_PATH, BOOT_CFG_PATH);
    return rc;
}

/***************************** (END OF FILE) *********************************/
