/*
*********************************************************************************************************
*
*	模块名称 : SSBL → app 跳转接口（spec §9.2 + §13.4，Phase 5 / Task 5.6）
*	文件名称 : handoff.h
*	版    本 : V1.0
*	说    明 : jump_to_app 完成 ThreadX/GIC/Timer 收尾后，调汇编 handoff_exit
*	           做纯破坏性 cache/MMU 清理并 bx 到 app 入口。本函数不返回。
*
*********************************************************************************************************
*/

#ifndef SSBL_HANDOFF_H
#define SSBL_HANDOFF_H

#include <stdint.h>

/* 跳转到 app。app_load_addr = app 在 DDR 的起始地址（payload[0] = reset 向量）。
 * header 已剥离、不在 DDR，故入口不加 0x20。本函数不返回。 */
void jump_to_app(uint32_t app_load_addr) __attribute__((noreturn));

#endif /* SSBL_HANDOFF_H */
