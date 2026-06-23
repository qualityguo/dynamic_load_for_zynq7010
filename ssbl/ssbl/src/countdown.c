/*
*********************************************************************************************************
*
*	模块名称 : boot_delay 窗口管理（spec §6.1 + §5.3，Phase 7 / Task 7.4）
*	文件名称 : countdown.c
*	版    本 : V1.0
*	说    明 : 行为：
*	             - boot_cfg.auto_boot == 0：不创建本线程，直接进 CLI（spec §5.3）。
*	             - auto_boot == 1：等 boot_delay_seconds 秒（tick=10ms → sleep(N*100)），
*	               期间监听 CLI_TRIGGER_FLAG_ANY：
*	                 触发 → 激活 cli_thread，本线程结束；
*	                 超时 → boot_selector_run()，成功则 jump 走；失败回退 CLI（spec §5.3）。
*
*********************************************************************************************************
*/

#include "countdown.h"
#include "cli_trigger.h"
#include "cli.h"
#include "boot_config.h"
#include "boot_selector.h"
#include "xil_printf.h"

/* 由 AppTaskStart 在线程上下文 boot_config_load 写入，全局共享 */
extern boot_cfg_t g_runtime_cfg;

static TX_THREAD s_countdown_tcb;
static uint64_t  s_countdown_stk[4096 / 8];
/* countdown_thread 全局由 handoff.c 拥有（jump_to_app 前终止它），
 * 本文件只赋值、不定义——countdown.h 里 extern 声明即可。 */

static void countdown_entry(ULONG arg)
{
    (void)arg;
    ULONG flags = 0;
    UINT  s;

    /* tick=10ms → boot_delay_seconds * 100 ticks */
    ULONG wait_ticks = (ULONG)g_runtime_cfg.boot_delay_seconds * 100u;

    s = tx_event_flags_get(&g_cli_trigger_flags, CLI_TRIGGER_FLAG_ANY,
                           TX_OR_CLEAR, &flags, wait_ticks);

    /* 窗口结束（无论触发还是超时）：立刻停掉 trigger 监控，否则它会继续
     * 用 cli_uart_poll_magic() 抽空 UART，抢走随后的 CLI 输入。 */
    cli_trigger_stop();

    if (s == TX_SUCCESS) {
        /* 触发了，进 CLI */
        xil_printf("[ssbl] trigger 0x%X, entering CLI\r\n", (unsigned)flags);
        cli_activate();
    } else {
        /* 超时（TX_NO_EVENTS 等），自动启动 */
        xil_printf("[ssbl] no trigger, auto-boot\r\n");
        int rc = boot_selector_run();
        if (rc != 0) {
            /* 启动失败 → 回退到 CLI（spec §5.3） */
            xil_printf("[ssbl] boot failed (%d), falling back to CLI\r\n", rc);
            cli_activate();
        }
        /* 成功路径已 jump_to_app，不到这 */
    }

    /* 本线程使命完成，自杀 */
    tx_thread_terminate(tx_thread_identify());
}

void countdown_create(void)
{
    UINT s;

    /* spec §5.3：auto_boot=no 时不创建 countdown、也不启动 trigger
     * （没有倒计时窗口可打断），直接进 CLI；这样 trigger 不会抽空 UART。 */
    if (g_runtime_cfg.auto_boot == 0) {
        xil_printf("[ssbl] auto_boot=no, skip countdown\r\n");
        cli_activate();
        return;
    }

    /* auto_boot=yes：启动 trigger 监控（GPIO + UART 魔数），供本窗口打断 */
    cli_trigger_create();

    s = tx_thread_create(&s_countdown_tcb, "countdown", countdown_entry, 0,
                         &s_countdown_stk[0], sizeof(s_countdown_stk),
                         5, 5, TX_NO_TIME_SLICE, TX_AUTO_START);
    if (s == TX_SUCCESS) {
        countdown_thread = &s_countdown_tcb;
    }
}

/***************************** (END OF FILE) *********************************/
