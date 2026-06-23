/*
*********************************************************************************************************
*
*	模块名称 : CRC32（spec §13.3，Phase 5 / Task 5.4）
*	文件名称 : crc32.c
*	版    本 : V1.0
*	说    明 : 与 Python zlib.crc32 结果一致（标准 CRC32，多项式 0xEDB88320，
*	           初值 0xFFFFFFFF，末尾异或 0xFFFFFFFF）。len=0 直接返回 0，
*	           与 pack_app.py 对空 payload 写入的 crc=0 对齐。
*
*********************************************************************************************************
*/

#include "crc32.h"

static uint32_t crc_table[256];
static int      crc_table_init = 0;

static void ensure_table(void)
{
    if (crc_table_init) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        crc_table[i] = c;
    }
    crc_table_init = 1;
}

uint32_t crc32_compute(const void *buf, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)buf;

    /* 空 payload 的 CRC 约定为 0（与 pack_app.py 一致），避免 0xFFFFFFFF ^ 0xFFFFFFFF
     * 与 zlib.crc32(b'')=0 的语义冲突。 */
    if (len == 0) {
        return 0u;
    }

    ensure_table();
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        crc = crc_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}
