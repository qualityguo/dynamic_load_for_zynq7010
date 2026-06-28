#ifndef __BOOT_CONFIG_H_
#define __BOOT_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/
#include <stdint.h>

/************************** Constant Definitions *****************************/
#define BOOT_CFG_APP_NAME_MAX   64
#define BOOT_CFG_BIT_NAME_MAX   64

/**************************** Type Definitions *******************************/
typedef struct {
	char		app_name[BOOT_CFG_APP_NAME_MAX];			// App名称
	char		bit_name[BOOT_CFG_BIT_NAME_MAX];			// Bit名称
	uint32_t	boot_delay_seconds;							// boot延时(单位s)
//	uint8_t		app_load_flag;								// 0 = 不加载app; 1 = 加载app
//	uint8_t 	bit_load_flag;								// 0 = 不加载bit; 1 = 加载bit
	uint8_t		auto_boot_flag;								// 0 = 不自动加载; 1 = 自动加载
//	uint8_t		cfg_loaded;									// 0 = 配置未加载; 1 = 配置已加载
}boot_cfg_t;

/************************** Function Prototypes ******************************/
int	boot_config_load(boot_cfg_t *cfg);
int boot_config_save(boot_cfg_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* __BOOT_CONFIG_H_ */
