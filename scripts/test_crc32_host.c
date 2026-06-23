/*
*********************************************************************************************************
*
*	模块名称 : CRC32 host 端单元测试（spec §12.1，Phase 5 / Task 5.4）
*	文件名称 : test_crc32_host.c
*	版    本 : V1.0
*	说    明 : 用 host gcc 编译跑，校验 crc32_compute 与 Python zlib.crc32 一致。
*	           本文件是 PC 单元测试，**不在固件工程里编译**（有自己的 main），
*	           故放在 scripts/ 而非 src/loader/。
*	           编译（在仓库根目录）：
*	             gcc scripts/test_crc32_host.c ssbl/ssbl/src/loader/crc32.c \
*	                 -Issbl/ssbl/src/loader -o test_crc32 && ./test_crc32
*
*********************************************************************************************************
*/

#include <stdio.h>
#include <string.h>
#include "crc32.h"

int main(void)
{
    /* 测试向量 1：空 —— 与 zlib.crc32(b'')=0 一致 */
    uint32_t r0 = crc32_compute("", 0);
    printf("crc32('')  = 0x%08X (expect 0x00000000)\n", r0);
    if (r0 != 0x00000000u) return 1;

    /* 测试向量 2："123456789" → 标准 0xCBF43926 */
    uint32_t r1 = crc32_compute("123456789", 9);
    printf("crc32('123456789') = 0x%08X (expect 0xCBF43926)\n", r1);
    if (r1 != 0xCBF43926u) return 1;

    /* 测试向量 3：256 字节递增 0..255 → 0x29058C73（与 Python zlib 对齐）*/
    uint8_t buf[256];
    for (int i = 0; i < 256; i++) buf[i] = (uint8_t)i;
    uint32_t r2 = crc32_compute(buf, 256);
    printf("crc32(0..255) = 0x%08X (expect 0x29058C73)\n", r2);
    if (r2 != 0x29058C73u) return 1;

    printf("All CRC32 tests PASS\n");
    return 0;
}
