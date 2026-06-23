/*
*********************************************************************************************************
*
*	模块名称 : 镜像加载器（spec §9.5，Phase 5 / Task 5.5）
*	文件名称 : image_loader.c
*	版    本 : V1.0
*	说    明 : 读 app.bin → 校验 32 字节 header → 流式拷 payload 到 DDR load_addr
*	           → 重算 payload CRC → 返回 load_addr（payload[0] 即 reset 向量，
*	           header 不进 DDR）。
*
*	流程（spec §9.5）：
*	   1. storage_file_open(app.bin)
*	   2. 读 32 字节 header
*	   3. image_header_validate：magic + header_crc + version
*	   4. 流式读 payload（image_size 字节）拷到 DDR
*	   5. crc32_compute(payload) == header.crc32
*	   6. 返回 load_addr
*
*********************************************************************************************************
*/

#include "storage.h"
#include "image_header.h"
#include "crc32.h"
#include "xil_printf.h"
#include <string.h>

#define HDR_BUF_SIZE    IMAGE_HEADER_SIZE     /* 32 */
#define CHUNK_SIZE      4096

/*
*********************************************************************************************************
*                                校验 header：magic / version / header_crc32
*********************************************************************************************************
*/
int image_header_validate(const image_header_t *hdr)
{
    if (hdr->magic != IMAGE_MAGIC) {
        xil_printf("[loader] magic bad: 0x%08X\r\n", (unsigned)hdr->magic);
        return -1;
    }
    if (hdr->version != IMAGE_VERSION_1) {
        xil_printf("[loader] version unsupported: %u\r\n", (unsigned)hdr->version);
        return -1;
    }
    /* header_crc32 覆盖 header[0x00..0x17]（前 24 字节，不含 header_crc32 与 reserved）*/
    uint32_t calc = crc32_compute(hdr, HEADER_NO_CRC_LEN);
    if (calc != hdr->header_crc32) {
        xil_printf("[loader] header_crc bad: calc=0x%08X in=0x%08X\r\n",
                   (unsigned)calc, (unsigned)hdr->header_crc32);
        return -1;
    }
    return 0;
}

/*
*********************************************************************************************************
*                                把 app.bin 整文件拷到 DDR staging 并校验
*	形    参 : path       app.bin 在 P2 数据分区的路径
*	           out_load_addr  成功时写入 header.load_addr
*	返 回 值 : STORAGE_OK(0) = OK；负数 = 错误码
*********************************************************************************************************
*/
int image_loader_load(const char *path, uint32_t *out_load_addr)
{
    image_header_t hdr;
    uint8_t        hdr_buf[HDR_BUF_SIZE];
    int            rc, n;

    rc = storage_file_open(path, STORAGE_OPEN_READ);
    if (rc != STORAGE_OK) {
        xil_printf("[loader] open %s failed (%d)\r\n", path, rc);
        return rc;
    }

    /* 读 header（32 字节）*/
    n = storage_file_read(hdr_buf, HDR_BUF_SIZE);
    if (n != HDR_BUF_SIZE) {
        xil_printf("[loader] header short read (%d)\r\n", n);
        storage_file_close();
        return STORAGE_ERR_IO;
    }
    memcpy(&hdr, hdr_buf, sizeof(hdr));

    if (image_header_validate(&hdr) != 0) {
        storage_file_close();
        return STORAGE_ERR_INVAL;
    }

    xil_printf("[loader] %s: load=0x%08X size=%u\r\n",
               path, (unsigned)hdr.load_addr, (unsigned)hdr.image_size);

    /* 把 payload 流式拷到 DDR load_addr。
     * chunk 用 static 而非栈：调用链 boot_selector_run → image_loader_load 运行在
     * AppTaskStart（8KB 栈）上，4KB 栈上缓冲风险大，故改静态。单线程、无重入，安全。*/
    uint8_t  *dst       = (uint8_t *)hdr.load_addr;
    uint32_t  remaining = hdr.image_size;
    static uint8_t chunk[CHUNK_SIZE];

    while (remaining > 0) {
        uint32_t want = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
        int got = storage_file_read(chunk, want);
        if (got != (int)want) {
            xil_printf("[loader] payload short read (%d != %u)\r\n", got, (unsigned)want);
            storage_file_close();
            return STORAGE_ERR_IO;
        }
        memcpy(dst, chunk, want);
        dst       += want;
        remaining -= want;
    }
    storage_file_close();

    /* payload 已完整在 DDR，一次性重算 CRC（load_addr 为 DDR 有效地址，可读）*/
    uint32_t calc = crc32_compute((void *)hdr.load_addr, hdr.image_size);
    if (calc != hdr.crc32) {
        xil_printf("[loader] payload crc bad: calc=0x%08X in=0x%08X\r\n",
                   (unsigned)calc, (unsigned)hdr.crc32);
        return STORAGE_ERR_INVAL;
    }

    *out_load_addr = hdr.load_addr;
    xil_printf("[loader] %s OK: crc=0x%08X\r\n", path, (unsigned)hdr.crc32);
    return STORAGE_OK;
}
