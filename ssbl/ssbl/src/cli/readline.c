/*
*********************************************************************************************************
*
*	模块名称 : readline 最小实现（spec §3.2，Phase 7 / Task 7.2）
*	文件名称 : readline.c
*	版    本 : V1.0
*	说    明 : 基于 cli_uart_getchar（阻塞）的行编辑：回显、退格/DEL、CR/LF 结束。
*	           其它控制字符忽略。方向键/历史留待 Phase 8 之后。
*
*********************************************************************************************************
*/

#include "readline.h"
#include "cli_uart.h"
#include "xil_printf.h"

int readline(char *buf, int maxlen)
{
    int n = 0;

    if (!buf || maxlen <= 0) return -1;

    while (n < maxlen - 1) {
        int c = cli_uart_getchar();   /* 阻塞，返回 0..255 或 -1 */
        if (c < 0) return -1;

        if (c == '\r' || c == '\n') {
            xil_printf("\r\n");
            buf[n] = '\0';
            return n;
        }
        if (c == 0x7F || c == 0x08) {   /* DEL 或 BS */
            if (n > 0) {
                n--;
                xil_printf("\b \b");
            }
            continue;
        }
        if (c < 0x20) continue;   /* 其它控制字符忽略 */

        buf[n++] = (char)c;
        xil_printf("%c", c);      /* 回显 */
    }

    buf[maxlen - 1] = '\0';
    return n;
}

/***************************** (END OF FILE) *********************************/
