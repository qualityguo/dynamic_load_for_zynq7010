/*
*********************************************************************************************************
*
*	模块名称 : CLI UART 收发 + 0xDEADBEEF 魔数滑窗检测（spec §6.3 #4，Phase 7 / Task 7.2）
*	文件名称 : cli_uart.c
*	版    本 : V1.0
*	说    明 : 复用 BSP uart1 device。底层 uart_read 是非阻塞的（从环形缓冲读，空则返回 0），
*	           因此本文件把 getchar / rx_chunk 封装成阻塞（无数据时 tx_thread_sleep(1) 让出）。
*
*	           魔数检测放在 boot_delay 窗口：trigger 监控线程每 tick 调 cli_uart_poll_magic()，
*	           把环形缓冲里的字节抽进 4 字节滑窗；命中 0xDEADBEEF 即触发进 CLI。
*	           CLI 激活后 trigger 线程自杀，不再抽字节，readline 自由读取。
*
*********************************************************************************************************
*/

#include "cli_uart.h"
#include "includes.h"
#include "xil_printf.h"

#define MAGIC_WORD  0xDEADBEEFu

static struct device *s_uart = NULL;
static void (*s_on_magic)(void) = NULL;
static uint32_t s_magic_window = 0;

void cli_uart_init(void)
{
    s_uart = device_find("uart1");
}

int cli_uart_getchar(void)
{
    uint8_t c;
    if (!s_uart) return -1;

    while (1) {
        if (device_read(s_uart, &c, 1) == 1) {
            return (int)c;
        }
        tx_thread_sleep(1);   /* 无数据则让出，10ms 后再试 */
    }
}

int cli_uart_rx_chunk(uint8_t *buf, int len)
{
    int total = 0;

    if (!s_uart || !buf || len <= 0) return -1;

    while (total < len) {
        int n = device_read(s_uart, buf + total, (size_t)(len - total));
        if (n <= 0) {
            tx_thread_sleep(1);
            continue;
        }
        total += n;
    }
    return total;
}

int cli_uart_poll_magic(void)
{
    uint8_t c;
    int matched = 0;

    if (!s_uart) return 0;

    /* 把环形缓冲里现有字节全部抽走，喂滑窗。YMODEM/二进制流不在本路径。 */
    while (device_read(s_uart, &c, 1) == 1) {
        s_magic_window = (s_magic_window << 8) | (uint32_t)c;
        if (s_magic_window == MAGIC_WORD) {
            matched = 1;
            if (s_on_magic) s_on_magic();
        }
    }
    return matched;
}

void cli_uart_attach_magic_detector(void (*on_magic)(void))
{
    s_on_magic = on_magic;
}

/***************************** (END OF FILE) *********************************/
