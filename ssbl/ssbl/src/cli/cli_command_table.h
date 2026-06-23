/*
*********************************************************************************************************
*
*	模块名称 : 命令注册宏表（spec §6.2 + §6.3 #5，Phase 7 / Task 7.1）
*	文件名称 : cli_command_table.h
*	版    本 : V1.0
*	说    明 : 一行一命令。实现函数签名：
*	             int cli_cmd_xxx(int argc, char **argv);
*	           返回 0 = 继续 CLI；非 0 = 退出 CLI（用于 boot 命令 jump 走）。
*
*********************************************************************************************************
*/

#ifndef SSBL_CLI_COMMAND_TABLE_H
#define SSBL_CLI_COMMAND_TABLE_H

typedef int (*cli_cmd_fn)(int argc, char **argv);

typedef struct {
    const char  *name;
    cli_cmd_fn   fn;
    const char  *help;
} cli_command_t;

/* 命令实现（cli_commands.c 提供） */
int cli_cmd_help    (int argc, char **argv);
int cli_cmd_info    (int argc, char **argv);
int cli_cmd_status  (int argc, char **argv);
int cli_cmd_ls      (int argc, char **argv);
int cli_cmd_cat     (int argc, char **argv);
int cli_cmd_ymodem  (int argc, char **argv);   /* Phase 8 */
int cli_cmd_rm      (int argc, char **argv);
int cli_cmd_mv      (int argc, char **argv);
int cli_cmd_cfg_show(int argc, char **argv);
int cli_cmd_boot    (int argc, char **argv);
int cli_cmd_mem     (int argc, char **argv);
int cli_cmd_test    (int argc, char **argv);
int cli_cmd_reset   (int argc, char **argv);

/* 宏表主体（cli.c 用初始化列表展开） */
#define CLI_COMMAND_TABLE                          \
    { "help",     cli_cmd_help,     "list commands"           }, \
    { "info",     cli_cmd_info,     "show SSBL version/layout" }, \
    { "status",   cli_cmd_status,   "last boot status"        }, \
    { "ls",       cli_cmd_ls,       "ls [dir]"                }, \
    { "cat",      cli_cmd_cat,      "cat <file>"              }, \
    { "ymodem",   cli_cmd_ymodem,   "ymodem rx <file>"        }, \
    { "rm",       cli_cmd_rm,       "rm <file>"               }, \
    { "mv",       cli_cmd_mv,       "mv <old> <new>"          }, \
    { "cfg",      cli_cmd_cfg_show, "cfg show / set / save"   }, \
    { "boot",     cli_cmd_boot,     "boot [app] [bit]"        }, \
    { "mem",      cli_cmd_mem,      "mem <addr> [n]"          }, \
    { "test",     cli_cmd_test,     "test bitstream|app <f>"  }, \
    { "reset",    cli_cmd_reset,    "soft reset"              }

#endif /* SSBL_CLI_COMMAND_TABLE_H */
