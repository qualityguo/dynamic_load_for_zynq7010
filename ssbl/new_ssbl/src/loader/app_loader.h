/*
*********************************************************************************************************
*
*	模块名称 : 裸 app 加载 API
*	文件名称 : app_loader.h
*	版    本 : V1.0
*	说    明 : 把“裸 app”（无脚本添加头的扁平 .bin）原始字节读入 DDR 指定地址。
*	           不解析头、不校验 CRC、不跳转——只负责把字节摆到 APP_LOAD_ADDR。
*	           跳转由调用方用 loader/handoff.c 的 jump_to_app(APP_LOAD_ADDR) 完成。
*
*	           约束：APP_LOAD_ADDR 必须等于 app 链接/运行的地址（flat bin 入口 = 加载地址）。
*
*********************************************************************************************************
*/

#ifndef __APP_LOADER_H__
#define __APP_LOADER_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 可配置项（编译期，可用 -D 覆盖）────────────────────────────────── */
/* 裸 app 加载并运行的 DDR 地址（flat bin 入口 = 此地址）。
 * 避开 SSBL(0x00100000)/bit staging(0x00800000)。须等于 app 的链接地址。*/
#ifndef APP_LOAD_ADDR
#define APP_LOAD_ADDR     0x01000000u
#endif
#ifndef APP_MAX_SIZE
#define APP_MAX_SIZE      (4u << 20)   /* 4MB 上限 */
#endif
/* ──────────────────────────────────────────────────────────────────── */

/* 错误码 */
typedef enum {
    APP_OK          =  0,
    APP_ERR_INVAL   = -1,   /* path == NULL 或空串 */
    APP_ERR_IO      = -2,   /* FileX open/read/close 失败 */
    APP_ERR_EMPTY   = -3,   /* 文件为空（0 字节）*/
    APP_ERR_TOO_BIG = -4,   /* 超出 APP_MAX_SIZE */
} app_err_t;

/* 把裸 app（无头 .bin）原始字节读入 APP_LOAD_ADDR。不解析、不校验、不跳转。
 * 跳转用现有 jump_to_app(APP_LOAD_ADDR)（loader/handoff.c，内部已做 cache 清理）。
 * 形参 path     : 介质上的 app 文件路径，如 "app.bin"
 * 形参 out_size : 成功时写入读到的字节数；不需要可传 NULL
 * 返回    : APP_OK 成功；负值见 app_err_t */
app_err_t  app_load_file(const char *path, uint32_t *out_size);

#ifdef __cplusplus
}
#endif

#endif /* __APP_LOADER_H__ */
