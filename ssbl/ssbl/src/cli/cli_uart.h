/*
*********************************************************************************************************
*
*	模块名称 : CLI UART 收发 + 魔数检测接口（spec §6.3 #4，Phase 7 / Task 7.2）
*	文件名称 : cli_uart.h
*	版    本 : V1.0
*	说    明 : 复用 BSP uart1 device（中断驱动 + 环形缓冲）。底层 device_read 非阻塞，
*	           故 getchar/rx_chunk 在此封装为阻塞（轮询 + tx_thread_sleep）。
*
*********************************************************************************************************
*/

#ifndef SSBL_CLI_UART_H
#define SSBL_CLI_UART_H

#include <stdint.h>

/* 找到 uart1 device 并缓存句柄（线程上下文调一次） */
void cli_uart_init(void);

/* 阻塞读 1 字节（0..255），返回 -1 = 设备不可用 */
int  cli_uart_getchar(void);

/* YMODEM 用：阻塞读 N 字节到 buf，返回实际读到的字节数（=len）或 -1 */
int  cli_uart_rx_chunk(uint8_t *buf, int len);

/* 非阻塞：把 rx 环形缓冲里现有的字节全部抽走，喂给 0xDEADBEEF 滑窗。
 * 命中魔数返回 1，否则 0。由 trigger 监控线程在 boot_delay 窗口内周期调用
 * （CLI 起来后不再调，避免抢走用户输入）。 */
int  cli_uart_poll_magic(void);

/* 可选：魔数命中回调（保留接口，当前 trigger 直接用返回值，回调可为 NULL） */
void cli_uart_attach_magic_detector(void (*on_magic)(void));

#endif /* SSBL_CLI_UART_H */
