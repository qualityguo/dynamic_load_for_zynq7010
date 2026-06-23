/*
*********************************************************************************************************
*
*	模块名称 : CRC32（spec §13.3，Phase 5 / Task 5.4）
*	文件名称 : crc32.h
*	版    本 : V1.0
*	说    明 : IEEE 802.3 多项式 0xEDB88320，结果与 Python zlib.crc32 一致，
*	           供 image_loader.c 校验 payload/header 使用。
*
*********************************************************************************************************
*/

#ifndef SSBL_CRC32_H
#define SSBL_CRC32_H

#include <stdint.h>

/* 对 buf[0..len) 计算 CRC32（len=0 返回 0）。 */
uint32_t crc32_compute(const void *buf, uint32_t len);

#endif /* SSBL_CRC32_H */
