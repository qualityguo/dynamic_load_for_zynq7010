/*
*********************************************************************************************************
*
*	模块名称 : CLI 子系统接口（spec §6.2 + §6.3 #1，Phase 7 / Task 7.1）
*	文件名称 : cli.h
*	版    本 : V1.0
*	说    明 : CLI 与 boot_selector 互斥：cli_thread 运行期间不跑 selector（spec §6.3 #1）。
*	           对外只暴露线程创建、激活与命令表查询。
*
*********************************************************************************************************
*/

#ifndef SSBL_CLI_H
#define SSBL_CLI_H

#include "tx_api.h"
#include "cli_command_table.h"

/* CLI 主线程句柄（countdown/trigger 激活用） */
extern TX_THREAD *cli_thread;

/* 在线程上下文调一次：创建 CLI 主线程（TX_DONT_START，等被 cli_activate 唤醒） */
void cli_create(void);

/* 激活 cli_thread（resume），由 countdown/trigger 在触发时调用 */
void cli_activate(void);

/* 供 help 命令遍历：返回命令表首地址，*count 写入命令数 */
const cli_command_t *cli_get_commands(unsigned int *count);

#endif /* SSBL_CLI_H */
