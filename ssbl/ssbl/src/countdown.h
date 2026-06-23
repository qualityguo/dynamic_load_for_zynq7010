/*
*********************************************************************************************************
*
*	模块名称 : boot_delay 窗口管理接口（spec §6.1 + §5.3，Phase 7 / Task 7.4）
*	文件名称 : countdown.h
*	版    本 : V1.0
*	说    明 : auto_boot=1 时：等 boot_delay 秒，期间监听 CLI_TRIGGER_FLAG_ANY——
*	           触发则进 CLI，超时则 boot_selector_run()。auto_boot=0 不创建本线程，
*	           直接进 CLI（spec §5.3）。
*
*********************************************************************************************************
*/

#ifndef SSBL_COUNTDOWN_H
#define SSBL_COUNTDOWN_H

#include "tx_api.h"

extern TX_THREAD *countdown_thread;

/* 线程上下文调一次：按 g_runtime_cfg.auto_boot 决定创建/跳过 countdown */
void countdown_create(void);

#endif /* SSBL_COUNTDOWN_H */
