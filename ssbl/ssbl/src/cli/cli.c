/*
*********************************************************************************************************
*
*	模块名称 : CLI 主线程 + 命令分发器（spec §6.1 + §6.3 #1，Phase 7 / Task 7.1）
*	文件名称 : cli.c
*	版    本 : V1.0
*	说    明 : 主循环：readline → tokenize（空格分隔，最多 8 token）→ 查命令表
*	           → 调实现函数 → 打提示符。命令返回非 0 视为要求退出 CLI
*	           （boot 命令 jump_to_app 后自然终止）。
*
*********************************************************************************************************
*/

#include "cli.h"
#include "readline.h"
#include "xil_printf.h"
#include <stdint.h>
#include <string.h>

/* 命令表（CLI_COMMAND_TABLE 宏展开） */
static const cli_command_t g_commands[] = {
    CLI_COMMAND_TABLE
};
#define N_COMMANDS  (sizeof(g_commands) / sizeof(g_commands[0]))

/* CLI 线程对象。cli_thread 全局由 handoff.c 拥有（jump_to_app 前终止它），
 * 本文件只赋值、不定义——cli.h 里 extern 声明即可，勿重复定义。 */
static TX_THREAD s_cli_tcb;
static uint64_t  s_cli_stk[8192 / 8];

const cli_command_t *cli_get_commands(unsigned int *count)
{
    if (count) *count = (unsigned int)N_COMMANDS;
    return g_commands;
}

static void print_prompt(void)
{
    xil_printf("\r\nssbl> ");
}

static const cli_command_t *find_cmd(const char *name)
{
    unsigned int i;
    for (i = 0; i < N_COMMANDS; i++) {
        if (strcmp(g_commands[i].name, name) == 0) {
            return &g_commands[i];
        }
    }
    return NULL;
}

static void cli_main(ULONG arg)
{
    (void)arg;
    char line[128];

    xil_printf("\r\n[CLI] enter. Type 'help' for commands.\r\n");
    print_prompt();

    while (1) {
        int n = readline(line, sizeof(line));
        if (n <= 0) {
            print_prompt();
            continue;
        }

        /* tokenize：空格/制表符分隔，最多 8 个 token */
        char *argv[8];
        int argc = 0;
        char *tok = strtok(line, " \t");
        while (tok && argc < 8) {
            argv[argc++] = tok;
            tok = strtok(NULL, " \t");
        }
        if (argc == 0) {
            print_prompt();
            continue;
        }

        const cli_command_t *cmd = find_cmd(argv[0]);
        if (!cmd) {
            xil_printf("unknown command: %s\r\n", argv[0]);
            print_prompt();
            continue;
        }

        int rc = cmd->fn(argc, argv);
        if (rc != 0) {
            /* 命令要求退出 CLI（boot 命令已 jump_to_app，不会真到这） */
            break;
        }
        print_prompt();
    }

    xil_printf("[CLI] exiting (should not reach)\r\n");
    tx_thread_suspend(tx_thread_identify());
}

void cli_create(void)
{
    UINT s = tx_thread_create(&s_cli_tcb, "cli", cli_main, 0,
                              &s_cli_stk[0], sizeof(s_cli_stk),
                              10, 10, TX_NO_TIME_SLICE, TX_DONT_START);
    if (s == TX_SUCCESS) {
        cli_thread = &s_cli_tcb;
    }
}

void cli_activate(void)
{
    if (cli_thread) {
        tx_thread_resume(cli_thread);
    }
}

/***************************** (END OF FILE) *********************************/
