/*
*********************************************************************************************************
*
*	模块名称 : boot.cfg 解析结果结构（spec §5，Phase 6 / Task 6.1）
*	文件名称 : boot_config.h
*	版    本 : V1.0
*	说    明 : boot.cfg 是 SSBL 启动选择的唯一配置源（INI 文本格式，落 SD 卡
*	           P2 数据分区根目录）。本头定义解析结果 boot_cfg_t 与三个 API：
*	             boot_config_load    读 boot.cfg，解析失败/不存在则用默认值
*	             boot_config_save    把内存 cfg 原子写回（.tmp + rename）
*	             boot_config_defaults 返回默认配置
*
*	           bit 空值的内部统一表示：bit_path == NULL 表示“不下载 bitstream”
*	           （spec §5.2 规定的四种外部写法都映射到 NULL）。
*
*********************************************************************************************************
*/

#ifndef SSBL_BOOT_CONFIG_H
#define SSBL_BOOT_CONFIG_H

#include <stdint.h>

#define BOOT_CFG_APP_NAME_MAX   64
#define BOOT_CFG_BIT_NAME_MAX   64

typedef struct {
    char     app_name[BOOT_CFG_APP_NAME_MAX];        /* cfg 的 app= 值（文件名）            */
    char    *bit_path;                              /* cfg 的 bit= 值；NULL = 不下载       */
    char     bit_path_storage[BOOT_CFG_BIT_NAME_MAX];/* bit 路径存储（bit_path 指向这里）  */
    uint32_t boot_delay_seconds;                     /* cfg 的 boot_delay= 值（秒）         */
    uint8_t  auto_boot;                             /* cfg 的 auto_boot= 值（0/1）         */
    uint8_t  cfg_loaded;                            /* 1 = 从文件加载成功；0 = 用默认值    */
} boot_cfg_t;

/*
*	解析 boot.cfg，结果写入 cfg。
*	返 回 值 : 0 = OK（从文件解析或用默认值）；负数 = 严重错误（介质量坏等）
*	           任何解析歧义都按 spec §5.3 退回默认值，不返回错误。
*/
int  boot_config_load(boot_cfg_t *cfg);

/*
*	把内存中的 cfg 写回 boot.cfg（CLI 的 cfg save 用，spec §6.2 + §6.3 #6）。
*	走 .tmp + rename 原子写，断电不会留下半截 boot.cfg。
*/
int  boot_config_save(const boot_cfg_t *cfg);

/*
*	返回默认配置（boot.cfg 不存在/损坏时用，spec §5.3）
*/
void boot_config_defaults(boot_cfg_t *cfg);

#endif /* SSBL_BOOT_CONFIG_H */
