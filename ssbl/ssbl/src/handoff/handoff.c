/*
*********************************************************************************************************
*
*	模块名称 : SSBL → app 跳转 C 接口（spec §9.2 + §13.4，Phase 5 / Task 5.6）
*	文件名称 : handoff.c
*	版    本 : V1.0
*	说    明 : jump_to_app 完成 ThreadX 业务线程收尾、停 Private Timer、停 GIC，
*	           再调汇编 handoff_exit 做 cache/MMU 清理并跳转。本函数不返回。
*
*	调用次序（spec §13.4）：
*	   1. 停所有非自身 ThreadX 线程
*	   2. tx_thread_relinquish 让调度器跑一轮
*	   3. 关 IRQ/FIQ/Async（汇编侧做）
*	   4. 停 Private Timer
*	   5. 停 GIC
*	   6. handoff_exit（汇编）cache/MMU 清理 + bx 跳转
*
*	注：cli_thread / countdown_thread / trigger_thread 是 Phase 7 CLI 子系统的
*	   线程，本 Phase 尚未定义。用弱符号（weak）声明：未定义时解析为 NULL，
*	   下面的判空保护即可跳过；Phase 7 提供强定义后自动生效。
*
*********************************************************************************************************
*/

#include "handoff.h"
#include "tx_api.h"
#include "bsp_init.h"          /* xInterruptController + xscugic.h            */
#include "xil_printf.h"
#include <stdint.h>

/* Phase 7 CLI 子系统的线程指针。Phase 5 尚无这些线程，初始化为 NULL。
 * 注意：不能用 weak 未定义符号——未定义的 weak 数据符号其 **地址** 被链接器
 * 解析为 0，于是 `if (cli_thread)` 会去读地址 0x00000000 的内容；在 Zynq
 * SSBL 的 MMU 映射下 0 地址未必可读，会触发 Data Abort 让 jump_to_app
 * 在第一条语句就挂死。改用强符号并初始化为 NULL（位于普通 .bss，读取安全），
 * Phase 7 模块创建线程后直接赋值即可。*/
TX_THREAD *cli_thread       = NULL;
TX_THREAD *countdown_thread = NULL;
TX_THREAD *trigger_thread   = NULL;

/* PS Private Timer 寄存器（Zynq）*/
#define PTIMER_BASE      0xF8F00600u
#define PTIMER_CONTROL   (*(volatile uint32_t *)(PTIMER_BASE + 0x08))
#define PTIMER_ISR       (*(volatile uint32_t *)(PTIMER_BASE + 0x0C))

/* 汇编：cache/MMU 清理 + bx 跳转，不返回 */
extern void handoff_exit(uint32_t entry_addr) __attribute__((noreturn));

/*
*********************************************************************************************************
*                                跳转到 app
*	形    参 : app_load_addr  app 在 DDR 的起始地址（payload[0] = reset 向量）
*********************************************************************************************************
*/
void jump_to_app(uint32_t app_load_addr)
{
    /* 1. 停 ThreadX 业务线程（Phase 7 线程未定义时为 NULL，自动跳过）*/
    if (cli_thread)       tx_thread_terminate(cli_thread);
    if (countdown_thread) tx_thread_terminate(countdown_thread);
    if (trigger_thread)   tx_thread_terminate(trigger_thread);

    /* 2. 让调度器跑一轮，确保终止生效 */
    tx_thread_relinquish();

    /* 4. 停 Private Timer（清 enable + 清挂起）*/
    PTIMER_CONTROL = 0;
    PTIMER_ISR     = 1;        /* write 1 to ISR 清挂起 */

    /* 5. 停 GIC */
    XScuGic_Stop(&xInterruptController);

    xil_printf("[handoff] cleanup done, jumping to 0x%08X\r\n",
               (unsigned)app_load_addr);

    /* 6. 汇编做 cache/MMU 清理 + 跳转。app 入口 = load_addr（payload[0]=reset）。
     *    header 已剥离、不在 DDR，故不加 0x20。*/
    handoff_exit(app_load_addr);    /* 不返回 */
}
