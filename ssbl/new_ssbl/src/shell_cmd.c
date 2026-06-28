/*
*********************************************************************************************************
*
*	模块名称 : Shell 命令实现
*	文件名称 : shell_cmd.c
*	版    本 : V1.0
*	说    明 : 基于 letter shell（argc/argv 模式，SHELL_AUTO_PRASE=0）。
*	           实现：info/status/ls/cat/rm/mv/cfg/boot/mem/test/reset/ymodem
*	           文件操作直接使用 FileX（g_fx_media），加载使用 app_loader/bitstream_loader。
*
*********************************************************************************************************
*/

#include "ssbl.h"
#include "xil_printf.h"
#include <string.h>
#include <stdlib.h>

/* main.c 中定义，shell 与各 loader 共享 */
extern boot_cfg_t g_runtime_cfg;
extern FX_MEDIA   g_fx_media;
extern u32        g_boot_mode;

/* 文件操作共用句柄（shell 单线程运行，复用安全）*/
static FX_FILE s_file;


/* ============================== 辅助函数 ============================== */

/* 大小写不敏感比较：返回 1=相等，0=不等 */
static int name_eq_ci(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = (int)(unsigned char)*a;
        int cb = (int)(unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca += ('a' - 'A');
        if (cb >= 'A' && cb <= 'Z') cb += ('a' - 'A');
        if (ca != cb) return 0;
        a++; b++;
    }
    return (*a == '\0' && *b == '\0');
}

/* 通过 shell 读一个 y/N 确认，返回 1=确认，0=取消 */
static int shell_confirm(const char *prompt)
{
    SHELL_TypeDef *sh = shellGetCurrent();
    char c = 0;

    xil_printf("%s", prompt);
    if (sh == NULL || sh->read == NULL) {
        return 0;
    }
    for (;;) {
        if (sh->read(&c) == 0) {
            if (c == '\r' || c == '\n') break;
            if (c == 'y' || c == 'Y') {
                while (sh->read(&c) == 0 && c != '\r' && c != '\n');
                return 1;
            }
            if (c == 'n' || c == 'N' || c >= ' ') {
                while (sh->read(&c) == 0 && c != '\r' && c != '\n');
                return 0;
            }
        }
    }
    return 0;
}

/* 查询文件大小（字节），文件不存在或路径为空返回 0 */
static uint32_t get_file_size(const char *path)
{
    if (path == NULL || path[0] == '\0') return 0;
    UINT status = fx_file_open(&g_fx_media, &s_file, (CHAR *)path, FX_OPEN_FOR_READ);
    if (status != FX_SUCCESS) return 0;
    uint32_t size = (uint32_t)s_file.fx_file_current_file_size;
    fx_file_close(&s_file);
    return size;
}


/* ============================== info ============================== */
int cmd_info(int argc, char *argv[])
{
    (void)argc; (void)argv;
    xil_printf("SSBL version: new_ssbl V1.0\r\n");

    switch (g_boot_mode) {
        case SD_MODE:
            xil_printf("media:        SD card (FileX FAT)\r\n");
            break;
#ifdef XPAR_PS7_QSPI_LINEAR_0_S_AXI_BASEADDR
        case QSPI_MODE:
            xil_printf("media:        QSPI flash (FileX FAT)\r\n");
            break;
#endif
        default:
            xil_printf("media:        unknown (boot_mode=0x%x)\r\n",
                       (unsigned)g_boot_mode);
            break;
    }

    xil_printf("load addr:    app=0x%08X  ssbl=0x00100000\r\n", APP_LOAD_ADDR);
    xil_printf("staging area: bit=0x%08X size=%uMB\r\n",
               BITSTREAM_DDR_ADDR, (unsigned)(BITSTREAM_DDR_SIZE >> 20));

    /* 当前配置的 app / bit 文件及大小 */
    if (g_runtime_cfg.app_name[0]) {
        xil_printf("app:          %s (%u bytes)\r\n",
                   g_runtime_cfg.app_name,
                   (unsigned)get_file_size(g_runtime_cfg.app_name));
    } else {
        xil_printf("app:          (none)\r\n");
    }
    if (g_runtime_cfg.bit_name[0]) {
        xil_printf("bit:          %s (%u bytes)\r\n",
                   g_runtime_cfg.bit_name,
                   (unsigned)get_file_size(g_runtime_cfg.bit_name));
    } else {
        xil_printf("bit:          (none)\r\n");
    }
    return 0;
}

/* ============================== status ============================== */
int cmd_status(int argc, char *argv[])
{
    (void)argc; (void)argv;
    xil_printf("cfg: app=%s bit=%s delay=%us auto=%u\r\n",
               g_runtime_cfg.app_name,
               g_runtime_cfg.bit_name[0] ? g_runtime_cfg.bit_name : "(none)",
               (unsigned)g_runtime_cfg.boot_delay_seconds,
               (unsigned)g_runtime_cfg.auto_boot_flag);
    return 0;
}

/* ============================== ls ============================== */
int cmd_ls(int argc, char *argv[])
{
    const char *path = (argc >= 2) ? argv[1] : "/";
    FX_LOCAL_PATH local;
    CHAR  name[FX_MAX_LONG_NAME_LEN + 1];
    UINT  status;

    status = fx_directory_local_path_set(&g_fx_media, &local, (CHAR *)path);
    if (status != FX_SUCCESS) {
        xil_printf("ls %s failed (0x%x)\r\n", path, status);
        return 0;
    }

    status = fx_directory_first_entry_find(&g_fx_media, name);
    while (status == FX_SUCCESS) {
        xil_printf("  %s\r\n", name);
        status = fx_directory_next_entry_find(&g_fx_media, name);
    }

    fx_directory_local_path_clear(&g_fx_media);
    return 0;
}

/* ============================== cat ============================== */
int cmd_cat(int argc, char *argv[])
{
    if (argc < 2) {
        xil_printf("usage: cat <file>\r\n");
        return 0;
    }

    UINT status = fx_file_open(&g_fx_media, &s_file, argv[1], FX_OPEN_FOR_READ);
    if (status != FX_SUCCESS) {
        xil_printf("cat: open %s failed (0x%x)\r\n", argv[1], status);
        return 0;
    }

    UCHAR buf[512];
    ULONG actual;
    while (fx_file_read(&s_file, buf, sizeof(buf), &actual) == FX_SUCCESS && actual > 0) {
        ULONG i;
        for (i = 0; i < actual; i++) {
            xil_printf("%c", buf[i]);
        }
    }

    fx_file_close(&s_file);
    xil_printf("\r\n");
    return 0;
}

/* ============================== rm ============================== */
int cmd_rm(int argc, char *argv[])
{
    if (argc < 2) {
        xil_printf("usage: rm <file>\r\n");
        return 0;
    }

    /* 安全边界：拒删 BOOT.BIN */
    if (name_eq_ci(argv[1], "boot.bin")) {
        xil_printf("ERROR: refuse to delete BOOT.BIN\r\n");
        return 0;
    }

    /* 删 boot.cfg / 当前 app / bit 需二次确认 */
    int need_confirm = 0;
    if (name_eq_ci(argv[1], "boot.cfg"))
    	need_confirm = 1;
    if (name_eq_ci(argv[1], g_runtime_cfg.app_name))
    	need_confirm = 1;
    if (g_runtime_cfg.bit_name[0] && name_eq_ci(argv[1], g_runtime_cfg.bit_name))
        need_confirm = 1;

    if (need_confirm) {
        if (!shell_confirm("Delete critical file, confirm? [y/N] ")) {
            xil_printf("cancelled\r\n");
            return 0;
        }
    }

    UINT status = fx_file_delete(&g_fx_media, argv[1]);
    if (status != FX_SUCCESS) {
        xil_printf("rm %s failed (0x%x)\r\n", argv[1], status);
    } else {
        xil_printf("deleted %s\r\n", argv[1]);
    }
    return 0;
}

/* ============================== mv ============================== */
int cmd_mv(int argc, char *argv[])
{
    if (argc < 3) {
        xil_printf("usage: mv <old> <new>\r\n");
        return 0;
    }
    UINT status = fx_file_rename(&g_fx_media, argv[1], argv[2]);
    if (status != FX_SUCCESS) {
        xil_printf("mv failed (0x%x)\r\n", status);
    }
    return 0;
}

/* ============================== cfg ============================== */
static void cfg_show(void)
{
    xil_printf("app=%s\r\n",        g_runtime_cfg.app_name);
    xil_printf("bit=%s\r\n",        g_runtime_cfg.bit_name);
    xil_printf("boot_delay=%u\r\n", (unsigned)g_runtime_cfg.boot_delay_seconds);
    xil_printf("auto_boot=%s\r\n",  g_runtime_cfg.auto_boot_flag ? "yes" : "no");
}

static void cfg_set(char *expr)
{
    char *eq = strchr(expr, '=');
    if (eq == NULL) {
        xil_printf("bad format, usage: cfg set <key>=<val>\r\n");
        return;
    }
    *eq = '\0';
    char *k = expr;
    char *v = eq + 1;

    if (strcmp(k, "app") == 0) {
        strncpy(g_runtime_cfg.app_name, v, BOOT_CFG_APP_NAME_MAX - 1);
        g_runtime_cfg.app_name[BOOT_CFG_APP_NAME_MAX - 1] = '\0';
    } else if (strcmp(k, "bit") == 0) {
        if (*v == '\0' || strcmp(v, "none") == 0) {
            g_runtime_cfg.bit_name[0] = '\0';
        } else {
            strncpy(g_runtime_cfg.bit_name, v, BOOT_CFG_BIT_NAME_MAX - 1);
            g_runtime_cfg.bit_name[BOOT_CFG_BIT_NAME_MAX - 1] = '\0';
        }
    } else if (strcmp(k, "boot_delay") == 0) {
        g_runtime_cfg.boot_delay_seconds = (uint32_t)strtoul(v, NULL, 10);
    } else if (strcmp(k, "auto_boot") == 0) {
        g_runtime_cfg.auto_boot_flag = (uint8_t)(strcmp(v, "yes") == 0 ? 1 : 0);
    } else {
        xil_printf("unknown key: %s\r\n", k);
        xil_printf("valid keys: app, bit, boot_delay, auto_boot\r\n");
        return;
    }
    xil_printf("OK\r\n");
}

int cmd_cfg(int argc, char *argv[])
{
    if (argc == 1 || strcmp(argv[1], "show") == 0) {
        cfg_show();
    } else if (strcmp(argv[1], "set") == 0) {
        if (argc < 3) {
            xil_printf("usage: cfg set <key>=<val>\r\n");
            xil_printf("  app=<file>  bit=<file|none>  boot_delay=<sec>  auto_boot=<yes|no>\r\n");
            return 0;
        }
        cfg_set(argv[2]);
    } else if (strcmp(argv[1], "save") == 0) {
        int rc = boot_config_save(&g_runtime_cfg);
        xil_printf("cfg %s\r\n", rc == 0 ? "saved" : "save failed");
    } else {
        xil_printf("usage: cfg [show | set <key>=<val> | save]\r\n");
    }
    return 0;
}

/* ============================== boot ============================== */
int cmd_boot(int argc, char *argv[])
{
    const char *app = (argc >= 2) ? argv[1] : g_runtime_cfg.app_name;
    const char *bit = (argc >= 3) ? argv[2] :
                      (g_runtime_cfg.bit_name[0] ? g_runtime_cfg.bit_name : NULL);

    int rc = boot_run(app, bit);
    /* boot_run 返回 = 失败（成功不会返回）*/
    xil_printf("boot failed (%d)\r\n", rc);
    return 0;
}

/* ============================== mem ============================== */
int cmd_mem(int argc, char *argv[])
{
    if (argc < 2) {
        xil_printf("usage: mem <addr> [n]\r\n");
        return 0;
    }

    uint32_t addr = (uint32_t)strtoul(argv[1], NULL, 0);
    int n = (argc >= 3) ? (int)strtoul(argv[2], NULL, 0) : 16;
    volatile uint32_t *p = (volatile uint32_t *)(addr & ~3u);
    int i;

    for (i = 0; i < n; i++) {
        if ((i & 3) == 0)
            xil_printf("\r\n0x%08X: ", (unsigned)(addr + (uint32_t)i * 4u));
        xil_printf("%08X ", (unsigned)p[i]);
    }
    xil_printf("\r\n");
    return 0;
}

/* ============================== test ============================== */
int cmd_test(int argc, char *argv[])
{
    if (argc < 3) {
        xil_printf("usage: test app <file> | test bitstream <file>\r\n");
        return 0;
    }

    if (strcmp(argv[1], "app") == 0) {
        uint32_t size = 0;
        app_err_t rc = app_load_file(argv[2], &size);
        xil_printf("test app %s: %s (load=0x%08X, %u bytes)\r\n",
                   argv[2], rc == APP_OK ? "OK" : "FAIL",
                   APP_LOAD_ADDR, size);
    } else if (strcmp(argv[1], "bitstream") == 0) {
        uint32_t size = 0;
        bit_err_t rc = bitstream_load_file(argv[2], &size);
        if (rc != BIT_OK) {
            xil_printf("test bitstream %s: FAIL (load %d)\r\n", argv[2], rc);
            return 0;
        }
        rc = bitstream_program((uint8_t *)BITSTREAM_DDR_ADDR, size);
        xil_printf("test bitstream %s: %s (%u bytes)\r\n",
                   argv[2], rc == BIT_OK ? "OK" : "FAIL", size);
    } else {
        xil_printf("unknown test target: %s\r\n", argv[1]);
    }
    return 0;
}

/* ============================== reset ============================== */
int cmd_reset(int argc, char *argv[])
{
    (void)argc; (void)argv;
    xil_printf("Resetting...\r\n");

#define SLCR_UNLOCK_ADDR      (*(volatile uint32_t *)0xF8000008u)
#define SLCR_PSS_RST_CTRL     (*(volatile uint32_t *)0xF8000200u)
    SLCR_UNLOCK_ADDR  = 0x0000DF0Du;
    SLCR_PSS_RST_CTRL = 1u;
    while (1);
#undef SLCR_UNLOCK_ADDR
#undef SLCR_PSS_RST_CTRL
}

/* ============================== ymodem (stub) ============================== */
int cmd_ymodem(int argc, char *argv[])
{
    (void)argc; (void)argv;
    xil_printf("ymodem: not implemented yet\r\n");
    return 0;
}


/* ============================== 命令导出 ============================== */
SHELL_EXPORT_CMD(info,   cmd_info,   show SSBL version/layout);
SHELL_EXPORT_CMD(status, cmd_status, show current boot config);
SHELL_EXPORT_CMD(ls,     cmd_ls,     ls [dir]);
SHELL_EXPORT_CMD(cat,    cmd_cat,    cat <file>);
SHELL_EXPORT_CMD(rm,     cmd_rm,     rm <file>);
SHELL_EXPORT_CMD(mv,     cmd_mv,     mv <old> <new>);
SHELL_EXPORT_CMD(cfg,    cmd_cfg,    cfg show/set/save);
SHELL_EXPORT_CMD(boot,   cmd_boot,   boot [app] [bit]);
SHELL_EXPORT_CMD(mem,    cmd_mem,    mem <addr> [n]);
SHELL_EXPORT_CMD(test,   cmd_test,   test app|bitstream <file>);
SHELL_EXPORT_CMD(reset,  cmd_reset,  soft reset);
SHELL_EXPORT_CMD(ymodem, cmd_ymodem, ymodem rx <file>);


/* ============================== 测试变量 ============================== */
int my_cnt = 0;
SHELL_EXPORT_VAR_INT(cnt,	my_cnt, 	"a test counter");



/*********************************** (END OF FILE) ***********************************/
