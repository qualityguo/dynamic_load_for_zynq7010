/*
*********************************************************************************************************
*
*	模块名称 : 裸 app 加载 API
*	文件名称 : app_loader.c
*	版    本 : V1.0
*	说    明 : 仅把裸 app（无头 .bin）原始字节读入 DDR 的 APP_LOAD_ADDR。
*	           不解析、不校验、不跳转。跳转由 handoff.c 的 jump_to_app() 负责。
*
*	           与 bitstream_loader 同一风格：可配置 DDR 地址 + load_file(path, out_size)
*	           + 类型化错误码 + 直接 FileX。裸 app 无头，故无 FORMAT/CRC 等校验。
*
*********************************************************************************************************
*/

#include "ssbl.h"
#include "app_loader.h"

/* FileX 全局介质（boot_config.c 定义）*/
extern FX_MEDIA  g_fx_media;

/* 私有 FileX 文件句柄 */
static FX_FILE  s_fx_file;


/*
*********************************************************************************************************
*	公共 API
*********************************************************************************************************
*/

/* 把裸 app 原始字节读入 APP_LOAD_ADDR。成功时 *out_size = 读到的字节数。
 * 跳转前的 D-cache 清理由 jump_to_app() 完成，此处不重复。*/
app_err_t app_load_file(const char *path, uint32_t *out_size)
{
	uint8_t *dst   = (uint8_t *)APP_LOAD_ADDR;
	uint32_t total = 0;
	UINT     status;
	ULONG    br;

	if (out_size != NULL) *out_size = 0u;
	if (path == NULL || path[0] == '\0') return APP_ERR_INVAL;

	/* 1. 打开 app 文件。*/
	status = fx_file_open(&g_fx_media, &s_fx_file, (CHAR *)path, FX_OPEN_FOR_READ);
	if (status != FX_SUCCESS) {
		ssbl_printf(LOG_ERR, "fx_file_open %s failed (0x%08X)\r\n",
		            path, (unsigned)status);
		return APP_ERR_IO;
	}

	/* 2. 分块读到 APP_LOAD_ADDR（不解析、不校验）。*/
	for (;;) {
		ULONG want;
		if (total >= APP_MAX_SIZE) {          /* 读满上限仍没 EOF → 文件过大 */
			fx_file_close(&s_fx_file);
			ssbl_printf(LOG_ERR, "%s too big (>%u)\r\n",
			            path, (unsigned)APP_MAX_SIZE);
			return APP_ERR_TOO_BIG;
		}
		want = APP_MAX_SIZE - total;
		status = fx_file_read(&s_fx_file, dst + total, want, &br);
		if (status != FX_SUCCESS) {
			ssbl_printf(LOG_ERR, "fx_file_read error (0x%08X)\r\n", (unsigned)status);
			fx_file_close(&s_fx_file);
			return APP_ERR_IO;
		}
		if (br == 0u) break;                  /* EOF */
		total += (uint32_t)br;
	}
	fx_file_close(&s_fx_file);

	if (total == 0u) {
		ssbl_printf(LOG_ERR, "%s empty / unreadable\r\n", path);
		return APP_ERR_EMPTY;
	}
	ssbl_printf(LOG_INFO, "%s loaded: %u bytes @0x%08X\r\n",
	            path, (unsigned)total, (unsigned)(uintptr_t)dst);

	if (out_size != NULL) *out_size = total;
	return APP_OK;
}

/***************************** (END OF FILE) *********************************/
