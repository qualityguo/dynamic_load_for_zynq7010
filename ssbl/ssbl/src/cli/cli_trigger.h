/*
*********************************************************************************************************
*
*	模块名称 : CLI 触发器接口（spec §6.1 + §6.3 #4，Phase 7 / Task 7.3）
*	文件名称 : cli_trigger.h
*	版    本 : V1.0
*	说    明 : 双路触发：GPIO 按键（key0 电平采样 + 消抖）+ UART 魔数 0xDEADBEEF。
*	           countdown 线程 tx_event_flags_get 等待 FLAG_ANY；任一路命中即进 CLI。
*
*********************************************************************************************************
*/

#ifndef SSBL_CLI_TRIGGER_H
#define SSBL_CLI_TRIGGER_H

#include "tx_api.h"

/* 触发事件标志位 */
#define CLI_TRIGGER_FLAG_GPIO   (1u << 0)
#define CLI_TRIGGER_FLAG_UART   (1u << 1)
#define CLI_TRIGGER_FLAG_ANY    (CLI_TRIGGER_FLAG_GPIO | CLI_TRIGGER_FLAG_UART)

extern TX_EVENT_FLAGS_GROUP g_cli_trigger_flags;

/* 线程上下文调一次：创建事件标志组（必须在 trigger 线程 / countdown 之前） */
void cli_trigger_init(void);

/* 线程上下文调一次：创建并启动 trigger 监控线程 */
void cli_trigger_create(void);

/* 线程上下文调一次：终止 trigger 监控线程。
 * 必须在 CLI 真正读取 UART 前调用——否则 trigger 线程会继续用
 * cli_uart_poll_magic() 抽空环形缓冲，抢走用户的 CLI 输入。 */
void cli_trigger_stop(void);

#endif /* SSBL_CLI_TRIGGER_H */
