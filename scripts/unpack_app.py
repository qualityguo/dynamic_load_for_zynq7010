#!/usr/bin/env python3
"""app.bin → app.raw + 打印 header 字段（CI 校验/调试，spec §3.1）。

用法：python unpack_app.py <in.bin> [out.raw]
"""
import struct
import zlib
import sys

from pack_app import (MAGIC, HEADER_NO_CRC_FMT, HEADER_NO_CRC_LEN,
                      HEADER_TOTAL_LEN, compute_header_crc)


def unpack(in_path: str, out_path: str = None) -> int:
    with open(in_path, "rb") as f:
        data = f.read()
    if len(data) < HEADER_TOTAL_LEN:
        print(f"ERROR: file too small ({len(data)} bytes)", file=sys.stderr)
        return 1
    header_no_crc = data[:HEADER_NO_CRC_LEN]
    header_crc_in = struct.unpack("<I", data[HEADER_NO_CRC_LEN:HEADER_NO_CRC_LEN + 4])[0]
    header_crc_calc = compute_header_crc(header_no_crc)
    fields = struct.unpack(HEADER_NO_CRC_FMT, header_no_crc)
    magic, version, image_type, load_addr, size, payload_crc = fields

    print(f"magic       = 0x{magic:08X} {'OK' if magic == MAGIC else 'BAD'}")
    print(f"version     = {version}")
    print(f"image_type  = {image_type}")
    print(f"load_addr   = 0x{load_addr:08X}")
    print(f"image_size  = {size}")
    print(f"payload_crc = 0x{payload_crc:08X}")
    print(f"header_crc  = 0x{header_crc_in:08X} "
          f"{'OK' if header_crc_in == header_crc_calc else 'BAD (calc=0x%08X)' % header_crc_calc}")

    payload = data[HEADER_TOTAL_LEN:]
    if len(payload) != size:
        print(f"ERROR: size mismatch (header={size}, actual={len(payload)})",
              file=sys.stderr)
        return 1
    payload_crc_calc = zlib.crc32(payload) & 0xFFFFFFFF
    if payload_crc_calc != payload_crc:
        print(f"ERROR: payload CRC mismatch (calc=0x{payload_crc_calc:08X})",
              file=sys.stderr)
        return 1

    if out_path:
        with open(out_path, "wb") as f:
            f.write(payload)
        print(f"Payload written to {out_path}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    out = sys.argv[2] if len(sys.argv) > 2 else None
    sys.exit(unpack(sys.argv[1], out))
