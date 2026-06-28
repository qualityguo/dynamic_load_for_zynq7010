/***************************** Include Files *********************************/
#include "ssbl.h"
#include "boot_config.h"

/************************** Constant Definitions *****************************/
#define BOOT_CFG_PATH        "boot.cfg"
#define BOOT_CFG_TMP_PATH    "boot.cfg.tmp"
#define BOOT_CFG_MAX_BYTES   2048
#define BOOT_CFG_LINE_MAX    256

/************************** Function Prototypes ******************************/
static void parse_text(boot_cfg_t *cfg, char *buf, uint32_t len);
static int apply_kv(boot_cfg_t *cfg, char *line);

/************************** Variable Definitions *****************************/
FX_MEDIA  g_fx_media;
FX_FILE   g_fx_file;
static char fx_buf[BOOT_CFG_MAX_BYTES];

int	boot_config_load(boot_cfg_t *cfg)
{
	UINT status;
	ULONG bw;

	// 1. 清空配置信息
	memset(cfg, 0x00, sizeof(*cfg));
	memset(fx_buf, 0x00, BOOT_CFG_MAX_BYTES);

	// 2. 打开配置文件
	status = fx_file_open(&g_fx_media, &g_fx_file, BOOT_CFG_PATH, FX_OPEN_FOR_READ);
	if (status != FX_SUCCESS)
	{
		ssbl_printf(LOG_ERR, "fx_file_open error!\r\n");
		return -1;
	}

	// 3. 移动到起始位置
	status = fx_file_seek(&g_fx_file, 0);
	if (status != FX_SUCCESS)
	{
		ssbl_printf(LOG_ERR, "fx_file_seek error!\r\n");
		return -1;
	}

	// 4. 读取配置文件
	status = fx_file_read(&g_fx_file, fx_buf, BOOT_CFG_MAX_BYTES, &bw);
	ssbl_printf(LOG_INFO, "fx_file_read len = %d\r\n", bw);
	if (status != FX_SUCCESS)
	{
		ssbl_printf(LOG_ERR, "fx_file_read error!\r\n");
		return -1;
	}

	// 5. 关闭配置文件
	status = fx_file_close(&g_fx_file);
	if (status != FX_SUCCESS)
	{
		ssbl_printf(LOG_ERR, "fx_file_close error!\r\n");
		return -1;
	}

	// 6. 进入解析流程
	fx_buf[bw] = '\0';
	parse_text(cfg, fx_buf, bw);

	ssbl_printf(LOG_INFO, "app=%s bit=%s delay=%us auto=%u\r\n",
				cfg->app_name, cfg->bit_name, cfg->boot_delay_seconds, cfg->auto_boot_flag);

	return 0;
}

int boot_config_save(boot_cfg_t *cfg)
{
	int len = 0;
	UINT status;

	// 1. 清空buf
	memset(fx_buf, 0x00, sizeof(fx_buf));

	// 2. 格式化
	len = snprintf(fx_buf, sizeof(fx_buf),
	        "# Zynq SSBL boot config\r\n"
	        "app = %s\r\n"
	        "bit = %s\r\n"
	        "boot_delay = %u\r\n"
	        "auto_boot = %s\r\n",
	        cfg->app_name,
	        cfg->bit_name,
	        (unsigned)cfg->boot_delay_seconds,
	        cfg->auto_boot_flag ? "yes" : "no");
	if (len <= 0 || (uint32_t)len >= sizeof(fx_buf)) {
		return -1;
	}

	// 3. 写.tmp
	status =  fx_file_create(&g_fx_media, BOOT_CFG_TMP_PATH);
	if (status != FX_SUCCESS)
	{
		ssbl_printf(LOG_ERR, "fx_file_create error!\r\n");
		// 如果是因为存在.tmp文件导致创建失败，则删除并重试一次
		status = fx_file_delete(&g_fx_media, BOOT_CFG_PATH);
		if (status != FX_SUCCESS)
		{
			ssbl_printf(LOG_ERR, "fx_file_delete error!\r\n");		// 允许删除失败
		}
		status =  fx_file_create(&g_fx_media, BOOT_CFG_TMP_PATH);
		if (status != FX_SUCCESS)
		{
			ssbl_printf(LOG_ERR, "fx_file_create retry error!\r\n");
			return -1;
		}
	}
	status = fx_file_open(&g_fx_media, &g_fx_file, BOOT_CFG_TMP_PATH, FX_OPEN_FOR_WRITE);
	if (status != FX_SUCCESS)
	{
		ssbl_printf(LOG_ERR, "fx_file_open error!\r\n");
		return -1;
	}
	status =  fx_file_write(&g_fx_file, fx_buf, len);
	if (status != FX_SUCCESS)
	{
		ssbl_printf(LOG_ERR, "fx_file_write error!\r\n");
		return -1;
	}
	status = fx_file_close(&g_fx_file);
	if (status != FX_SUCCESS)
	{
		ssbl_printf(LOG_ERR, "fx_file_close error!\r\n");
		return -1;
	}

	// 4. 删除重命名
	status = fx_file_delete(&g_fx_media, BOOT_CFG_PATH);
	if (status != FX_SUCCESS)
	{
		ssbl_printf(LOG_ERR, "fx_file_delete error!\r\n");		// 允许删除失败
	}
	status = fx_file_rename(&g_fx_media, BOOT_CFG_TMP_PATH, BOOT_CFG_PATH);
	if (status != FX_SUCCESS)
	{
		ssbl_printf(LOG_ERR, "fx_file_rename error!\r\n");
		// 重命名失败直接删除tmp文件
		status = fx_file_delete(&g_fx_media, BOOT_CFG_TMP_PATH);
		if (status != FX_SUCCESS)
		{
			ssbl_printf(LOG_ERR, "fx_file_delete error!\r\n");		// 允许删除失败
		}
		return -1;
	}

	return 0;

}


static void parse_text(boot_cfg_t *cfg, char *buf, uint32_t len)
{
	char line[BOOT_CFG_LINE_MAX];
	uint32_t i = 0, line_start = 0;

	while(i < len)
	{
		char c = (i < len) ? buf[i] : '\n';
		if(c == '\n' || c == '\r' || i == len)
		{
			// 拷贝一行数据
			uint32_t llen = i - line_start;
			if (llen >= BOOT_CFG_LINE_MAX)
			{
				llen = BOOT_CFG_LINE_MAX - 1;
			}
			memcpy(line, buf + line_start, llen);
			line[llen] = '\0';

			// 去除行首空白
			char *p = line;
			while (*p == ' ' || *p == '\t') p++;

			// 跳过空行和注释行
			if(*p != '\0' && *p != '#')
			{
				apply_kv(cfg, p);
			}

			line_start = i + 1;
		}
		i++;
	}
}

static int apply_kv(boot_cfg_t *cfg, char *line)
{
	// 1. 截断#后边的注释
	char *hash = strchr(line, '#');
	if (hash) *hash = '\0';

	// 2. 获得key字符串和val字符串
	char *eq = strchr(line, '=');
	if (!eq) return 0;
	*eq = '\0';							// 等号截断
	char *key = line;					// key
	char *val = eq + 1;					// val

	// 3. 去除空白
	while (*key == ' ' || *key == '\t') key++;
	char *k_end = key + strlen(key);
	while (k_end > key && (k_end[-1] == ' ' || k_end[-1] == '\t')) *--k_end = '\0';

	while (*val == ' ' || *val == '\t') val++;
	char *v_end = val + strlen(val);
	while (v_end > val && (v_end[-1] == ' ' || v_end[-1] == '\t' ||
						   v_end[-1] == '\r' || v_end[-1] == '\n')) *--v_end = '\0';

	// 4. 字符串比较
	if (strcmp(key, "app") == 0) {
		strncpy(cfg->app_name, val, BOOT_CFG_APP_NAME_MAX - 1);
		cfg->app_name[BOOT_CFG_APP_NAME_MAX - 1] = '\0';
		return 1;
	}

	if (strcmp(key, "bit") == 0) {
		strncpy(cfg->bit_name, val, BOOT_CFG_BIT_NAME_MAX - 1);
		cfg->bit_name[BOOT_CFG_BIT_NAME_MAX - 1] = '\0';
		return 1;
	}

	if(strcmp(key, "boot_delay") == 0) {
		cfg->boot_delay_seconds = (uint32_t)strtoul(val, NULL, 10);
		return 1;
	}

	if (strcmp(key, "auto_boot") == 0) {
		cfg->auto_boot_flag = (strcmp(val, "yes") == 0) ? 1 : 0;
		return 1;
	}

	return 0;
}

