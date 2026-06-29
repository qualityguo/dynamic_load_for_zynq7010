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

/* ============================== ymodem ============================== */

#define YM_DDR_BASE      0x02000000u
#define YM_DDR_MAX_SZ    (8u * 1024u * 1024u)			/* 8 MB */

/* YMODEM 接收状态 */
static struct {
	uint32_t offset;					/* 已写入DDR的字节数 */
	uint32_t total;						/* 包0声明的文件大小 */
	char     filename[256];				/* 包0携带的文件名 */
	int      loaded;					/* 1=数据在DDR中, 可save */
} g_ym_recv;

/* ---- YMODEM 回调函数 ---- */

/* 包0到达: 解析文件名和大小, 初始化接收状态 */
static enum ym_code ym_cb_begin(struct ym_ctx *ctx, uint8_t *buf, uint32_t len)
{
	uint32_t i;

	(void)ctx;

	/* Block0 payload 格式: "filename\0 filesize_decimal\0 padding..." */
	i = 0;
	while (i < len && i < sizeof(g_ym_recv.filename) - 1 && buf[i] != '\0')
		i++;
	memcpy(g_ym_recv.filename, buf, i);
	g_ym_recv.filename[i] = '\0';

	/* 文件大小在 filename\0 之后 */
	g_ym_recv.total = 0;
	if (i + 1 < len)
		g_ym_recv.total = (uint32_t)atoi((const char *)(buf + i + 1));

	g_ym_recv.offset = 0;
	g_ym_recv.loaded = 0;

	xil_printf("YMODEM: receiving '%s' (%u bytes)...\r\n",
			   g_ym_recv.filename, (unsigned)g_ym_recv.total);

	return YM_CODE_ACK;
}

/* 数据包到达: 拷贝到DDR暂存区 */
static enum ym_code ym_cb_data(struct ym_ctx *ctx, uint8_t *buf, uint32_t len)
{
	(void)ctx;

	if (g_ym_recv.offset + len > YM_DDR_MAX_SZ) {
		xil_printf("YMODEM: buffer overflow! offset=%u len=%u\r\n",
				   (unsigned)g_ym_recv.offset, (unsigned)len);
		return YM_CODE_CAN;
	}

	memcpy((void *)(YM_DDR_BASE + g_ym_recv.offset), buf, len);
	g_ym_recv.offset += len;

	return YM_CODE_ACK;
}

/* 传输结束: 标记已加载 */
static enum ym_code ym_cb_end(struct ym_ctx *ctx, uint8_t *buf, uint32_t len)
{
	(void)ctx;
	(void)buf;
	(void)len;

	g_ym_recv.loaded = 1;
	xil_printf("YMODEM: received %u bytes\r\n", (unsigned)g_ym_recv.offset);

	return YM_CODE_ACK;
}

/* ---- shell 命令处理 ---- */

int cmd_ymodem(int argc, char *argv[])
{
	if (argc < 2) {
		xil_printf("usage:\r\n");
		xil_printf("  ym load    - receive file via YMODEM to DDR\r\n");
		xil_printf("  ym save    - save received file to storage\r\n");
		return -1;
	}

	/* ym load: 启动YMODEM接收, 数据暂存到DDR */
	if (name_eq_ci(argv[1], "load")) {
		struct UART_Device *dev;
		struct ym_ctx ctx;
		int err;

		dev = Get_UART_Device((char *)"zynq_uart1");
		if (dev == NULL) {
			xil_printf("ym load: UART device not found\r\n");
			return -1;
		}

		g_ym_recv.offset    = 0;
		g_ym_recv.total     = 0;
		g_ym_recv.loaded    = 0;
		g_ym_recv.filename[0] = '\0';

		xil_printf("ym load: waiting for YMODEM transfer (CRC mode)...\r\n");

		err = ymodem_recv_on_uart(&ctx, dev,
								  ym_cb_begin, ym_cb_data, ym_cb_end, 60);
		if (err != 0) {
			xil_printf("ym load: failed (err=0x%X)\r\n", (unsigned)(-err));
			return err;
		}

		xil_printf("ym load: OK, '%s' in DDR @ 0x%08X (%u bytes)\r\n",
				   g_ym_recv.filename, (unsigned)YM_DDR_BASE,
				   (unsigned)g_ym_recv.offset);
		return 0;
	}

	/* ym save: 将DDR暂存数据写入存储介质文件 */
	if (name_eq_ci(argv[1], "save")) {
		UINT status;

		if (!g_ym_recv.loaded) {
			xil_printf("ym save: no file loaded, run 'ym load' first\r\n");
			return -1;
		}
		if (g_ym_recv.offset == 0) {
			xil_printf("ym save: nothing to save\r\n");
			return -1;
		}

		/* 检查存储空间是否足够 */
		{
			ULONG free_bytes = 0;
			status = fx_media_space_available(&g_fx_media, &free_bytes);
			if (status != FX_SUCCESS) {
				xil_printf("ym save: cannot query free space (0x%X)\r\n", (unsigned)status);
				return -1;
			}
			if (g_ym_recv.offset > free_bytes) {
				xil_printf("ym save: file too large! (%u bytes > %u bytes free)\r\n",
						   (unsigned)g_ym_recv.offset, (unsigned)free_bytes);
				return -1;
			}
			xil_printf("ym save: writing %u bytes (%u bytes free)\r\n",
					   (unsigned)g_ym_recv.offset, (unsigned)free_bytes);
		}

		/* 若文件已存在则先删除 */
		fx_file_delete(&g_fx_media, g_ym_recv.filename);

		/* 创建并打开文件 */
		status = fx_file_create(&g_fx_media, g_ym_recv.filename);
		if (status != FX_SUCCESS) {
			xil_printf("ym save: fx_file_create failed (0x%X)\r\n", (unsigned)status);
			return -1;
		}
		status = fx_file_open(&g_fx_media, &s_file,
							  g_ym_recv.filename, FX_OPEN_FOR_WRITE);
		if (status != FX_SUCCESS) {
			xil_printf("ym save: fx_file_open failed (0x%X)\r\n", (unsigned)status);
			return -1;
		}

		/* 写入DDR暂存区的全部数据 */
		status = fx_file_write(&s_file, (void *)YM_DDR_BASE, g_ym_recv.offset);
		if (status != FX_SUCCESS) {
			xil_printf("ym save: fx_file_write failed (0x%X)\r\n", (unsigned)status);
			fx_file_close(&s_file);
			return -1;
		}

		status = fx_file_close(&s_file);
		if (status != FX_SUCCESS) {
			xil_printf("ym save: fx_file_close failed (0x%X)\r\n", (unsigned)status);
			return -1;
		}

		fx_media_flush(&g_fx_media);

		xil_printf("ym save: wrote %u bytes to '%s'\r\n",
				   (unsigned)g_ym_recv.offset, g_ym_recv.filename);

		g_ym_recv.loaded = 0;
		return 0;
	}

	xil_printf("ym: unknown subcommand '%s'\r\n", argv[1]);
	xil_printf("usage: ym load | ym save\r\n");
	return -1;
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
SHELL_EXPORT_CMD(ym,     cmd_ymodem, ym load | ym save);


/* ============================== 测试变量 ============================== */
int my_cnt = 0;
SHELL_EXPORT_VAR_INT(cnt,	my_cnt, 	"a test counter");



/*********************************** (END OF FILE) ***********************************/
