/*
*********************************************************************************************************
*
*	模块名称 : 双路触发监控线程（spec §6.1 + §6.3 #4，Phase 7 / Task 7.3）
*	文件名称 : cli_trigger.c
*	版    本 : V1.0
*	说    明 : 每 tick（10ms）做两件事：
*	             1) 采样 key0 电平，消抖后置 CLI_TRIGGER_FLAG_GPIO；
*	             2) cli_uart_poll_magic() 抽 UART 字节喂魔数滑窗，命中置
*	                CLI_TRIGGER_FLAG_UART。
*	           任一路命中即置标志并自杀——避免 CLI 起来后继续抢走 UART 输入。
*
*	           注意：key0 底层读返回当前电平（按下=1，松开=0，见 key_driver.c），
*	           与 vendor 一致，故采用电平采样 + 软件消抖（而非依赖 GPIO 中断 notify）。
*
*********************************************************************************************************
*/

#include "cli_trigger.h"
#include "cli_uart.h"
#include "includes.h"
#include "xil_printf.h"

TX_EVENT_FLAGS_GROUP g_cli_trigger_flags;

#define GPIO_DEBOUNCE_TICKS  3    /* 3 ticks = 30ms（tick=10ms） */
#define TRIGGER_PRIO         11

static struct device *s_key = NULL;   /* MIO 按键 key0 */

static TX_THREAD s_trigger_tcb;
static uint64_t  s_trigger_stk[4096 / 8];
/* trigger_thread 全局由 handoff.c 拥有（jump_to_app 前终止它）；本文件赋值/清空。
 * 用它兼作"是否已创建"标志，省去额外标志位。 */
extern TX_THREAD *trigger_thread;

static void trigger_thread_entry(ULONG arg)
{
    (void)arg;
    uint8_t val = 0, prev = 0, debounce = 0;
    int triggered = 0;

    s_key = device_find("key0");

    while (!triggered) {
        /* ---- GPIO 按键：电平采样 + 消抖 ---- */
        if (s_key) {
            device_read(s_key, &val, 1);
            if (val != prev) {
                debounce = 0;
                prev = val;
            } else if (val == 1) {   /* 按住（active low → 1） */
                if (++debounce == GPIO_DEBOUNCE_TICKS) {
                    tx_event_flags_set(&g_cli_trigger_flags,
                                       CLI_TRIGGER_FLAG_GPIO, TX_OR);
                    triggered = 1;
                }
            }
        }

        /* ---- UART 魔数：抽字节喂滑窗 ---- */
        if (!triggered && cli_uart_poll_magic()) {
            tx_event_flags_set(&g_cli_trigger_flags,
                               CLI_TRIGGER_FLAG_UART, TX_OR);
            triggered = 1;
        }

        if (!triggered) {
            tx_thread_sleep(1);   /* 1 tick = 10ms */
        }
    }

    /* 触发完成，本线程使命结束，自杀（不再碰 UART，把输入让给 CLI） */
    tx_thread_terminate(tx_thread_identify());
}

void cli_trigger_init(void)
{
    tx_event_flags_create(&g_cli_trigger_flags, "cli_trigger_flags");
}

void cli_trigger_create(void)
{
    UINT s = tx_thread_create(&s_trigger_tcb, "trigger", trigger_thread_entry, 0,
                              &s_trigger_stk[0], sizeof(s_trigger_stk),
                              TRIGGER_PRIO, TRIGGER_PRIO,
                              TX_NO_TIME_SLICE, TX_AUTO_START);
    if (s == TX_SUCCESS) {
        trigger_thread = &s_trigger_tcb;   /* 供 handoff / stop 使用 */
    }
}

void cli_trigger_stop(void)
{
    /* 终止监控线程（已自杀则 terminate 返回非 OK，无副作用）。
     * 关键：必须在 CLI 读 UART 前停止，否则 poll_magic 会抽空输入。
     * 清空 trigger_thread，让 handoff 的判空保护也同步失效。 */
    if (trigger_thread) {
        tx_thread_terminate(trigger_thread);
        trigger_thread = NULL;
    }
}

/***************************** (END OF FILE) *********************************/
