/*
*********************************************************************************************************
*
*	模块名称 : CLI 命令实现（spec §6.2，Phase 7 / Task 7.6）
*	文件名称 : cli_commands.c
*	版    本 : V1.0
*	说    明 : Phase 7 实现：help/info/status/ls/cat/rm/mv/cfg/boot/mem/test/reset
*	           Phase 8 实现：ymodem（当前为 stub）
*
*********************************************************************************************************
*/

#include "cli_command_table.h"
#include "cli.h"
#include "readline.h"
#include "storage.h"
#include "boot_config.h"
#include "boot_selector.h"
#include "handoff.h"
#include "image_header.h"
#include "xil_printf.h"
#include <string.h>
#include <stdlib.h>

/* AppTaskStart 在线程上下文 boot_config_load 写入；status/cfg/boot 共享 */
extern boot_cfg_t g_runtime_cfg;

/* 大小写不敏感比较（FAT 文件名不区分大小写，避免依赖 <strings.h>） */
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

/* ============================== help ============================== */
int cli_cmd_help(int argc, char **argv)
{
    unsigned int i, n;
    const cli_command_t *cmds = cli_get_commands(&n);
    (void)argc; (void)argv;

    xil_printf("Commands:\r\n");
    for (i = 0; i < n; i++) {
        xil_printf("  %-8s %s\r\n", cmds[i].name, cmds[i].help);
    }
    return 0;
}

/* ============================== info ============================== */
int cli_cmd_info(int argc, char **argv)
{
    (void)argc; (void)argv;
    xil_printf("SSBL version: Phase 7\r\n");
    xil_printf("media:        SD (FileX FAT, P2)\r\n");
    xil_printf("load addr:    app=0x01000000  ssbl=0x00100000\r\n");
    xil_printf("staging area: 0x02000000 size=10MB\r\n");
    return 0;
}

/* ============================== status ============================== */
int cli_cmd_status(int argc, char **argv)
{
    (void)argc; (void)argv;
    /* Phase 10 才有 boot.log；本 Phase 先打印当前 cfg */
    xil_printf("cfg: app=%s bit=%s delay=%us auto=%u\r\n",
               g_runtime_cfg.app_name,
               g_runtime_cfg.bit_path ? g_runtime_cfg.bit_path : "(none)",
               (unsigned)g_runtime_cfg.boot_delay_seconds,
               (unsigned)g_runtime_cfg.auto_boot);
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
    char buf[512];
    int n;

    if (argc < 2) {
        xil_printf("usage: cat <file>\r\n");
        return 0;
    }
    int rc = storage_file_open(argv[1], STORAGE_OPEN_READ);
    if (rc != STORAGE_OK) {
        xil_printf("open %s failed (%d)\r\n", argv[1], rc);
        return 0;
    }
    while ((n = storage_file_read(buf, sizeof(buf))) > 0) {
        int i;
        for (i = 0; i < n; i++) xil_printf("%c", buf[i]);
    }
    storage_file_close();
    xil_printf("\r\n");
    return 0;
}

/* ============================== rm ============================== */
int cli_cmd_rm(int argc, char **argv)
{
    char ans[8];

    if (argc < 2) {
        xil_printf("usage: rm <file>\r\n");
        return 0;
    }

    /* spec §6.3 #7：安全边界 —— 拒删 BOOT.bin */
    if (name_eq_ci(argv[1], "boot.bin") == 0) {
        xil_printf("ERROR: refuse to delete BOOT.bin\r\n");
        return 0;
    }

    /* 删 boot.cfg / 当前 app/bit 需二次确认 */
    int need_confirm = 0;
    if (name_eq_ci(argv[1], "boot.cfg") == 0) need_confirm = 1;
    if (name_eq_ci(argv[1], g_runtime_cfg.app_name) == 0) need_confirm = 1;
    if (g_runtime_cfg.bit_path && name_eq_ci(argv[1], g_runtime_cfg.bit_path) == 0) {
        need_confirm = 1;
    }
    if (need_confirm) {
        xil_printf("Delete critical file %s? [y/N] ", argv[1]);
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
                   (unsigned)g_runtime_cfg.boot_delay_seconds,
                   g_runtime_cfg.auto_boot ? "yes" : "no");
        return 0;
    }
    if (strcmp(argv[1], "set") == 0) {
        char *k, *v, *eq;
        if (argc < 3) {
            xil_printf("usage: cfg set <key>=<val>\r\n");
            return 0;
        }
        /* 解析 argv[2] 形如 "app=app_b.bin" */
        eq = strchr(argv[2], '=');
        if (!eq) { xil_printf("bad format\r\n"); return 0; }
        *eq = '\0';
        k = argv[2];
        v = eq + 1;

        if (strcmp(k, "app") == 0) {
            strncpy(g_runtime_cfg.app_name, v, BOOT_CFG_APP_NAME_MAX - 1);
            g_runtime_cfg.app_name[BOOT_CFG_APP_NAME_MAX - 1] = '\0';
        } else if (strcmp(k, "bit") == 0) {
            if (*v == '\0' || strcmp(v, "none") == 0) {
                g_runtime_cfg.bit_path = NULL;
            } else {
                strncpy(g_runtime_cfg.bit_path_storage, v,
                        BOOT_CFG_BIT_NAME_MAX - 1);
                g_runtime_cfg.bit_path_storage[BOOT_CFG_BIT_NAME_MAX - 1] = '\0';
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

/* ============================== boot ============================== */
int cli_cmd_boot(int argc, char **argv)
{
    uint32_t load_addr = 0;
    /* boot / boot <app> / boot <app> <bit> */
    const char *app = (argc >= 2) ? argv[1] : g_runtime_cfg.app_name;
    const char *bit = (argc >= 3) ? argv[2] : g_runtime_cfg.bit_path;

    int rc = boot_selector_load_only(app, bit, &load_addr);
    if (rc != STORAGE_OK) {
        xil_printf("boot load failed (%d)\r\n", rc);
        return 0;   /* 继续 CLI */
    }

    /* spec §6.3 #2：boot 在 cli_thread 内联 jump_to_app（不返回） */
    xil_printf("[boot] handoff to app @0x%08X\r\n", (unsigned)load_addr);
    jump_to_app(load_addr);   /* noreturn */
}

/* ============================== mem ============================== */
int cli_cmd_mem(int argc, char **argv)
{
    uint32_t addr;
    int n, i;
    volatile uint32_t *p;

    if (argc < 2) { xil_printf("usage: mem <addr> [n]\r\n"); return 0; }

    addr = (uint32_t)strtoul(argv[1], NULL, 0);
    n    = (argc >= 3) ? (int)strtoul(argv[2], NULL, 0) : 16;
    p    = (volatile uint32_t *)(addr & ~3u);

    for (i = 0; i < n; i++) {
        if ((i & 3) == 0) xil_printf("\r\n0x%08X: ", (unsigned)(addr + (uint32_t)i * 4u));
        xil_printf("%08X ", (unsigned)p[i]);
    }
    xil_printf("\r\n");
    return 0;
}

/* ============================== test ============================== */
int cli_cmd_test(int argc, char **argv)
{
    if (argc < 3) {
        xil_printf("usage: test bitstream <file> | test app <file>\r\n");
        return 0;
    }
    if (strcmp(argv[1], "app") == 0) {
        /* 仅加载 + 校验 header/CRC，不跳转 */
        uint32_t load_addr = 0;
        int rc = boot_selector_load_only(argv[2], NULL, &load_addr);
        xil_printf("test app %s: %s (load=0x%08X)\r\n",
                   argv[2], rc == 0 ? "OK" : "FAIL", (unsigned)load_addr);
    } else if (strcmp(argv[1], "bitstream") == 0) {
        /* 真正的 bit 下载 Phase 9 才实现（当前为 stub） */
        extern int bit_loader_download(const char *);
        int rc = bit_loader_download(argv[2]);
        xil_printf("test bitstream %s: %s\r\n", argv[2], rc == 0 ? "OK" : "FAIL/Phase9");
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

    /* Zynq SLCR：先解锁再写 PSS_RST_CTRL 触发软复位（spec §6.2 reset 命令） */
#define SLCR_UNLOCK_ADDR      (*(volatile uint32_t *)0xF8000008u)
#define SLCR_PSS_RST_CTRL     (*(volatile uint32_t *)0xF8000200u)
    SLCR_UNLOCK_ADDR  = 0x0000DF0Du;   /* 解锁 SLCR 保护寄存器 */
    SLCR_PSS_RST_CTRL = 1u;            /* 写 1 触发 PS 软复位，不返回 */
    while (1);
}

/* ============================== ymodem (Phase 8) ============================== */
int cli_cmd_ymodem(int argc, char **argv)
{
    (void)argc; (void)argv;
    xil_printf("ymodem: not implemented yet (Phase 8)\r\n");
    return 0;
}

/***************************** (END OF FILE) *********************************/
