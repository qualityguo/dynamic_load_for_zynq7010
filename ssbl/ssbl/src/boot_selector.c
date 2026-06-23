/*
*********************************************************************************************************
*
*	模块名称 : 启动选择器主流程（spec §1.2 + §5.3 + §9.5，Phase 6 / Task 6.4）
*	文件名称 : boot_selector.c
*	版    本 : V1.0
*	说    明 : 自动启动路径（spec §1.2 Stage 2）：
*	               boot_config_load
*	                 → bit_loader_download(bit)   失败仅告警，继续（spec §5.3）
*	                 → image_loader_load(app)     失败拒绝启动 → 回 CLI/idle
*	                 → jump_to_app(load_addr)     不返回
*
*	           容错（spec §5.3）：app 错误 = BOOT_REFUSED，返回负数由调用方处理；
*	           bit 错误 = 告警继续（bitstream 非启动必需）。
*
*********************************************************************************************************
*/

#include "boot_selector.h"
#include "image_header.h"
#include "storage.h"
#include "handoff.h"
#include "xil_printf.h"
#include "tx_api.h"

/* Phase 9 才实现真正的 bit 下载（FileX 读 .bit → XDcfg_Transfer 到 PL）。
 * 本 Phase 用 stub：cfg 配了 bit 会告警但继续（spec §5.3）。*/
extern int bit_loader_download(const char *path);

/*
*********************************************************************************************************
*                                仅加载（不跳转）：CLI test/boot 命令复用
*********************************************************************************************************
*/
int boot_selector_load_only(const char *app_path, const char *bit_path,
                            uint32_t *out_load_addr)
{
    /* 1. bit（如有）：失败仅告警，不中止（spec §5.3）*/
    if (bit_path && bit_path[0] != '\0') {
        xil_printf("[boot] downloading bitstream %s\r\n", bit_path);
        int brc = bit_loader_download(bit_path);
        if (brc != 0) {
            xil_printf("[boot] WARN: bit download failed (%d), continuing\r\n", brc);
        }
    }

    /* 2. app：失败拒绝启动（spec §5.3 BOOT_REFUSED → 回 CLI/idle）*/
    int arc = image_loader_load(app_path, out_load_addr);
    if (arc != STORAGE_OK) {
        xil_printf("[boot] app load failed (%d)\r\n", arc);
        return arc;
    }
    return STORAGE_OK;
}

/*
*********************************************************************************************************
*                                主入口：读 cfg → 加载 → 跳转
*********************************************************************************************************
*/
int boot_selector_run(void)
{
    boot_cfg_t cfg;
    int rc = boot_config_load(&cfg);
    if (rc != 0) {
        /* 介质量坏等严重错误，调用方决定（一般进 CLI）*/
        return rc;
    }

    uint32_t load_addr = 0;
    rc = boot_selector_load_only(cfg.app_name, cfg.bit_path, &load_addr);
    if (rc != STORAGE_OK) {
        return rc;   /* 调用方进 CLI/idle */
    }

    /* 跳转（不返回）*/
    xil_printf("[boot] handoff to app @0x%08X\r\n", (unsigned)load_addr);
    jump_to_app(load_addr);    /* noreturn */
}

/***************************** (END OF FILE) *********************************/
