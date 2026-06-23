/*
*********************************************************************************************************
*
*	模块名称 : 启动选择器主流程（spec §1.2 + §9.5，Phase 6 / Task 6.4）
*	文件名称 : boot_selector.h
*	版    本 : V1.0
*	说    明 : SSBL 的“大脑”：读 boot.cfg → 加载 bit（如有）→ 加载 app → 跳转。
*
*********************************************************************************************************
*/

#ifndef SSBL_BOOT_SELECTOR_H
#define SSBL_BOOT_SELECTOR_H

#include "boot_config.h"

/*
*	启动选择器主入口：
*	   读 boot.cfg → 加载 app → 加载 bit（如有）→ 跳转 app
*	返 回 值 : 成功不返回（已 jump_to_app）；负数 = 关键步骤失败（调用方进 CLI/idle）
*/
int  boot_selector_run(void);

/*
*	仅加载并校验，不跳转（CLI 的 `boot <app>` / `test app <file>` 用，Phase 7）。
*	形    参 : app_path / bit_path 为 NULL 时跳过该项
*	返 回 值 : STORAGE_OK = OK，*out_load_addr 写入 app 加载地址；负数 = 错误码
*/
int  boot_selector_load_only(const char *app_path, const char *bit_path,
                             uint32_t *out_load_addr);

#endif /* SSBL_BOOT_SELECTOR_H */
