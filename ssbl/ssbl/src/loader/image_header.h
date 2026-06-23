/*
*********************************************************************************************************
*
*	模块名称 : 镜像 header 定义（spec §4.1，Phase 5 / Task 5.2）
*	文件名称 : image_header.h
*	版    本 : V1.0
*	说    明 : app.bin 的 32 字节 header 布局。pack_app.py（PC 端）写入的 header
*	           与本结构体必须逐字节一致（含 #pragma pack(1)），否则 image_loader
*	           校验失败。
*
*********************************************************************************************************
*/

#ifndef SSBL_IMAGE_HEADER_H
#define SSBL_IMAGE_HEADER_H

#include <stdint.h>

/* magic：little-endian 字节序读作 "OOBZ"。pack_app.py 的 MAGIC 必须与之相等 */
#define IMAGE_MAGIC             0x5A424F4Fu
#define IMAGE_HEADER_SIZE       32
#define IMAGE_VERSION_1         1

/* image_type 取值 */
#define IMAGE_TYPE_THREADX      1
#define IMAGE_TYPE_BARE_METAL   2
#define IMAGE_TYPE_RESERVED     3

/* header 中参与 header_crc32 计算的字段长度（前 6 个 uint32 = 24 字节，
 * 不含偏移 0x18 的 header_crc32 自身）。与 pack_app.py HEADER_NO_CRC_LEN 一致 */
#define HEADER_NO_CRC_LEN       24

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;          /* 偏移 0x00：固定 IMAGE_MAGIC                       */
    uint32_t version;        /* 偏移 0x04：当前 = 1                                */
    uint32_t image_type;     /* 偏移 0x08：IMAGE_TYPE_*                            */
    uint32_t load_addr;      /* 偏移 0x0C：加载地址（应 = 0x01000000）             */
    uint32_t image_size;     /* 偏移 0x10：payload 字节数（不含 header）           */
    uint32_t crc32;          /* 偏移 0x14：payload 的 CRC32                        */
    uint32_t header_crc32;   /* 偏移 0x18：header[0x00..0x17] 的 CRC32             */
    uint32_t reserved;       /* 偏移 0x1C：保留，凑齐 32 字节（固定 0，不参与 CRC）*/
} image_header_t;
#pragma pack(pop)

/* 校验函数（image_loader.c 提供）*/
int      image_header_validate(const image_header_t *hdr);
uint32_t image_crc32(const void *buf, uint32_t len);

#endif /* SSBL_IMAGE_HEADER_H */
