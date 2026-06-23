#!/usr/bin/env python3
"""pack_app.py round-trip 自测：随机 payload → pack → 校验（spec §12.1，Phase 5）。

用法：python test_pack_app.py
"""
import os
import struct
import subprocess
import sys
import tempfile
import zlib

sys.path.insert(0, os.path.dirname(__file__))
from pack_app import (MAGIC, VERSION, IMAGE_TYPE_THREADX, DEFAULT_LOAD_ADDR,
                      HEADER_NO_CRC_FMT, HEADER_NO_CRC_LEN, HEADER_TOTAL_LEN,
                      compute_header_crc)


def test_roundtrip():
    payload = bytes([(i * 7) & 0xFF for i in range(4096)])
    with tempfile.TemporaryDirectory() as d:
        raw = os.path.join(d, "app.raw")
        binned = os.path.join(d, "app.bin")
        with open(raw, "wb") as f:
            f.write(payload)
        rc = subprocess.call([sys.executable,
                              os.path.join(os.path.dirname(__file__), "pack_app.py"),
                              raw, binned])
        assert rc == 0
        with open(binned, "rb") as f:
            data = f.read()
        # 解析 header
        assert len(data) == HEADER_TOTAL_LEN + len(payload)
        fields = struct.unpack(HEADER_NO_CRC_FMT, data[:HEADER_NO_CRC_LEN])
        magic, version, image_type, load_addr, size, payload_crc = fields
        assert magic == MAGIC
        assert version == VERSION
        assert image_type == IMAGE_TYPE_THREADX
        assert load_addr == DEFAULT_LOAD_ADDR
        assert size == len(payload)
        assert payload_crc == (zlib.crc32(payload) & 0xFFFFFFFF)
        # header_crc 自校验（header_crc32 位于偏移 24..28，之后 28..32 为 reserved）
        header_crc_in = struct.unpack("<I", data[HEADER_NO_CRC_LEN:HEADER_NO_CRC_LEN + 4])[0]
        assert header_crc_in == compute_header_crc(data[:HEADER_NO_CRC_LEN])
        # payload 内容一致
        assert data[HEADER_TOTAL_LEN:] == payload
    print("test_roundtrip: PASS")


def test_empty_payload():
    """空 payload 的 CRC 应为 0（与 crc32_compute('',0)=0 对齐）。"""
    with tempfile.TemporaryDirectory() as d:
        raw = os.path.join(d, "empty.raw")
        binned = os.path.join(d, "empty.bin")
        with open(raw, "wb") as f:
            f.write(b"")
        rc = subprocess.call([sys.executable,
                              os.path.join(os.path.dirname(__file__), "pack_app.py"),
                              raw, binned])
        assert rc == 0
        with open(binned, "rb") as f:
            data = f.read()
        assert len(data) == HEADER_TOTAL_LEN
        fields = struct.unpack(HEADER_NO_CRC_FMT, data[:HEADER_NO_CRC_LEN])
        size = fields[4]
        payload_crc = fields[5]
        assert size == 0
        assert payload_crc == 0x00000000
    print("test_empty_payload: PASS")


if __name__ == "__main__":
    test_roundtrip()
    test_empty_payload()
    print("All tests passed.")
