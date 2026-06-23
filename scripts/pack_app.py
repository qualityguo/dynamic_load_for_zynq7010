#!/usr/bin/env python3
"""把 app.raw 加上 32 字节 header 得到 app.bin（spec §13.3，Phase 5 / Task 5.3）。

用法：python pack_app.py <in.raw> <out.bin> [load_addr=0x01000000] [image_type=1]

header 布局与 boot/.../loader/image_header.h 的 image_header_t 逐字节一致
（#pragma pack(1)，7 个 uint32 = 32 字节）。
"""
import struct
import zlib
import sys

MAGIC = 0x5A424F4F   # little-endian 字节序读作 "OOBZ"；与 image_header.h 一致
VERSION = 1
IMAGE_TYPE_THREADX = 1
DEFAULT_LOAD_ADDR = 0x01000000

HEADER_NO_CRC_FMT = "<IIIIII"   # 6 个 uint32 = 24 字节（不含 header_crc32）
HEADER_NO_CRC_LEN = 24
HEADER_TOTAL_LEN = 32


def compute_header_crc(buf_no_crc: bytes) -> int:
    """对 header 前 24 字节算 CRC32，与 SSBL crc32_compute / Python zlib 一致。"""
    return zlib.crc32(buf_no_crc) & 0xFFFFFFFF


def pack(raw_path: str, out_path: str,
         load_addr: int = DEFAULT_LOAD_ADDR,
         image_type: int = IMAGE_TYPE_THREADX) -> int:
    with open(raw_path, "rb") as f:
        raw = f.read()
    size = len(raw)
    payload_crc = zlib.crc32(raw) & 0xFFFFFFFF

    header_no_crc = struct.pack(
        HEADER_NO_CRC_FMT,
        MAGIC, VERSION, image_type, load_addr, size, payload_crc,
    )
    header_crc = compute_header_crc(header_no_crc)
    # header_crc32 + reserved(0)，共 8 字节，凑齐 32 字节 header
    header = header_no_crc + struct.pack("<II", header_crc, 0)

    assert len(header) == HEADER_TOTAL_LEN
    with open(out_path, "wb") as f:
        f.write(header)
        f.write(raw)
    print(f"Packed: {out_path} (payload={size} bytes, "
          f"load=0x{load_addr:08X}, crc=0x{payload_crc:08X})")
    return 0


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    raw_path = argv[1]
    out_path = argv[2]
    load_addr = int(argv[3], 0) if len(argv) > 3 else DEFAULT_LOAD_ADDR
    image_type = int(argv[4], 0) if len(argv) > 4 else IMAGE_TYPE_THREADX
    return pack(raw_path, out_path, load_addr, image_type)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
