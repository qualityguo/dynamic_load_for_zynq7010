/***************************** Include Files *********************************/
#include "boot_run.h"
#include "ssbl.h"


int boot_run(const char* app_path, const char *bit_path)
{
	/* 1. 先配 PL（如有 bitstream）*/
	if (bit_path && bit_path[0]) {
		uint32_t bit_size = 0;
		bit_err_t brc = bitstream_load_file(bit_path, &bit_size);
		if (brc != BIT_OK) {
			ssbl_printf(LOG_ERR, "load bit %s failed (%d)\r\n", bit_path, brc);
			return -1;
		}
		brc = bitstream_program((uint8_t *)BITSTREAM_DDR_ADDR, bit_size);
		if (brc != BIT_OK) {
			ssbl_printf(LOG_ERR, "program bit %s failed (%d)\r\n", bit_path, brc);
			return -2;
		}
		ssbl_printf(LOG_INFO, "boot: bit %s programmed (%u bytes)\r\n", bit_path, bit_size);
	}

	/* 2. 加载 app 到 APP_LOAD_ADDR */
	uint32_t app_size = 0;
	app_err_t arc = app_load_file(app_path, &app_size);
	if (arc != APP_OK) {
		ssbl_printf(LOG_ERR, "load app %s failed (%d)\r\n", app_path, arc);
		return -3;
	}

	/* 3. 交权（成功不返回）*/
	ssbl_printf(LOG_INFO, "handoff to app @0x%08X (%u bytes)\r\n",
			   APP_LOAD_ADDR, app_size);
	jump_to_app(APP_LOAD_ADDR);
}
