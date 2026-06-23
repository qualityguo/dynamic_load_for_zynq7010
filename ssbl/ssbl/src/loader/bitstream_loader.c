/*
*********************************************************************************************************
*
*	模块名称 : bitstream 动态加载（spec §4.2，Phase 9）
*	文件名称 : bitstream_loader.c
*	版    本 : V1.0
*	说    明 : Phase 6 stub。Phase 9 实现：FileX 读 .bit → XDcfg_Transfer 到 PL
*	           （经 devcfg / PCAP）。当前 cfg 配了 bit 会告警继续（spec §5.3），
*	           返回 -1 让 boot_selector 走“bit 失败告警但不中止”路径。
*
*********************************************************************************************************
*/

#include <stddef.h>

int bit_loader_download(const char *path)
{
    (void)path;
    return -1;   /* Phase 9 实现 */
}

/***************************** (END OF FILE) *********************************/
