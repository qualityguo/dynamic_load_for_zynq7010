> **章节导航**：
  [← 上一章：storage 抽象层 + FileX SD 挂载](phase-04-storage-filex.md) ｜ [↑ 返回总述](../2026-06-22-zynq-multistage-boot.md) ｜ [下一章：boot.cfg 解析 + boot_selector 自动选 app →](phase-06-boot-cfg.md)

> **Phase 元信息**
> - 对应 Spec：`§9.5, §13.3`
> - 里程碑：`M5`
> - 交付物：app_template + pack_app.py + handoff
> - 分章文件：`phases/phase-05-app-template-handoff.md`

> **接口定位**：本章对其他 Phase 的依赖与对外暴露的符号，见
> [总述章 · 模块依赖矩阵](../2026-06-22-zynq-multistage-boot.md#3-模块依赖矩阵)。
> 新增功能时，先查该清单确认接口落点，再回本章扩展。

---
## Phase 5：app_template + pack_app.py + handoff（M5）

**目标**：建立 `boot/app_template/`（基于 hello_threadx，ORIGIN=0x1000000）；写 `scripts/pack_app.py` 给 app.raw 加 32 字节 header；SSBL 实现 `image_loader` 读 app.bin、校验、拷到 DDR、跳转。本 Phase 验证 SSBL → app 完整 handoff。

**Spec 引用**：§4.1（header）、§9.1–§9.5（handoff）、§13.3（pack_app.py）、§13.4（handoff_exit.S）。

### Task 5.1：建 app_template 工程（ORIGIN=0x1000000）

**Files:**
- Create: `boot/app_template/` 目录树
- Create: `boot/app_template/app.ld`
- Create: `boot/app_template/src/main.c`
- Create: `boot/app_template/README.md`

- [ ] **Step 1：建目录**

```bash
mkdir -p boot/app_template/src
cp -rf external/threadx/ThreadX boot/app_template/ThreadX
cp -rf external/bsp             boot/app_template/BSP
cp -rf external/utils           boot/app_template/utils
```

- [ ] **Step 2：写 app.ld（ORIGIN=0x1000000）**

复制 `external/lscript_hello_threadx.ld` 为 `boot/app_template/app.ld`，仅改 MEMORY.ps7_ddr_0：

```ld
MEMORY
{
   /* ★ 与 ssbl.ld 的差异：ORIGIN=0x1000000，LENGTH=0x800000（8MB）*/
   ps7_ddr_0 : ORIGIN = 0x1000000, LENGTH = 0x800000
   /* 其余 qspi/ram 段不变 */
}
```

- [ ] **Step 3：写 src/main.c（app 版，区别于 SSBL）**

文件 `boot/app_template/src/main.c`：

```c
/* boot/app_template/src/main.c — 主应用模板
 * Phase 5 最小版：仅闪烁 LED1（区别于 SSBL 的 LED0），证明 app 真的接管。
 * 复制本目录改造 main.c 即得新 app。
 */

#include "xil_printf.h"
#include "includes.h"

#define APP_TASK_START_PRIO 2u
#define APP_TASK_LED_PRIO   20u

static TX_THREAD start_tcb;
static uint64_t  start_stk[4096/8];
static TX_THREAD led_tcb;
static uint64_t  led_stk[1024/8];

static void app_start(ULONG arg);
static void app_led(ULONG arg);

int main(void)
{
    xil_printf("\r\n[APP] Hello from app @0x1000000\r\n");
    bsp_init();
    board_init();
    tx_kernel_enter();
    while (1);
}

void tx_application_define(void *first_unused_memory)
{
    (void)first_unused_memory;
    tx_thread_create(&start_tcb, "app_start", app_start, 0,
                     &start_stk[0], 4096, APP_TASK_START_PRIO, APP_TASK_START_PRIO,
                     TX_NO_TIME_SLICE, TX_AUTO_START);
}

static void app_start(ULONG arg)
{
    (void)arg;
    xil_printf("[APP] start task, creating LED1 task\r\n");
    tx_thread_create(&led_tcb, "app_led", app_led, 0,
                     &led_stk[0], 1024, APP_TASK_LED_PRIO, APP_TASK_LED_PRIO,
                     TX_NO_TIME_SLICE, TX_AUTO_START);
    tx_thread_terminate(&start_tcb);
}

static void app_led(ULONG arg)
{
    (void)arg;
    /* 用 LED1 区别于 SSBL 的 LED0，肉眼可分*/
    struct device *pled1 = device_find("led1");
    uint8_t val = 1;
    if (!pled1) {
        xil_printf("[APP] WARN: led1 not found\r\n");
        tx_thread_suspend(tx_thread_identify());
    }
    while (1) {
        device_write(pled1, &val, 1);
        val = (val == 1) ? 0 : 1;
        tx_thread_sleep(100);    /* 1s 周期，区别于 SSBL 的 2s */
    }
}
```

- [ ] **Step 4：写 README.md**

文件 `boot/app_template/README.md`：

```markdown
# app_template — 主应用模板

基于 hello_threadx，ORIGIN=`0x1000000`、LENGTH=`0x800000`（8MB）。

## 复制改造新 app

```bash
cp -r boot/app_template boot/my_app
# 改 boot/my_app/src/main.c 的业务逻辑
# 改名为 my_app.bin（pack_app.py 后）
```

## JTAG 独立调试

不通过 SSBL，直接 JTAG 下载 app.elf 到 `0x1000000` 跑：
- 先 `ps7_init`，再 `dow app.elf`，`con`；
- 期望 LED1 闪烁（1s 周期），区别于 SSBL 的 LED0（2s 周期）。
```

- [ ] **Step 5：Vitis 编译 + JTAG 验证 app.elf 独立可跑**

按 Phase 1 Task 1.2/1.3 流程，对 app_template 跑一遍：
1. Vitis 建 app Application Project，源码用 `boot/app_template/`；
2. Build → `app.elf`；
3. JTAG `dow app.elf` → `con`；
4. 期望串口见 `[APP] Hello from app @0x1000000`，LED1 1s 闪烁。

- [ ] **Step 6：提交**

```bash
git add boot/app_template/
git commit -m "Phase 5.1: app_template at ORIGIN=0x1000000, JTAG-verified standalone"
```

---

### Task 5.2：写 image_header.h

**Files:**
- Create: `boot/ssbl/include/image_header.h`

- [ ] **Step 1：写 image_header.h（spec §4.1）**

文件 `boot/ssbl/include/image_header.h`：

```c
/* boot/ssbl/include/image_header.h — 32 字节镜像 header（spec §4.1）*/
#ifndef SSBL_IMAGE_HEADER_H
#define SSBL_IMAGE_HEADER_H

#include <stdint.h>

#define IMAGE_MAGIC         0x5A424F4Fu   /* little-endian 字节序读作 "OOBZ" */
#define IMAGE_HEADER_SIZE   32
#define IMAGE_VERSION_1     1

/* image_type 取值 */
#define IMAGE_TYPE_THREADX      1
#define IMAGE_TYPE_BARE_METAL   2
#define IMAGE_TYPE_RESERVED     3

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;          /* 偏移 0x00：固定 IMAGE_MAGIC */
    uint32_t version;        /* 偏移 0x04：当前 = 1 */
    uint32_t image_type;     /* 偏移 0x08：IMAGE_TYPE_* */
    uint32_t load_addr;      /* 偏移 0x0C：加载地址（应 = 0x01000000）*/
    uint32_t image_size;     /* 偏移 0x10：payload 字节数（不含 header）*/
    uint32_t crc32;          /* 偏移 0x14：payload 的 CRC32 */
    uint32_t header_crc32;   /* 偏移 0x18：header[0x00..0x17] 的 CRC32 */
} image_header_t;
#pragma pack(pop)

/* 校验函数（image_loader.c 提供）*/
int image_header_validate(const image_header_t *hdr);
uint32_t image_crc32(const void *buf, uint32_t len);

#endif /* SSBL_IMAGE_HEADER_H */
```

- [ ] **Step 2：提交**

```bash
git add boot/ssbl/include/image_header.h
git commit -m "Phase 5.2: image_header_t struct (spec §4.1)"
```

---

### Task 5.3：写 scripts/pack_app.py（spec §13.3）

**Files:**
- Create: `scripts/pack_app.py`
- Create: `scripts/unpack_app.py`
- Create: `scripts/test_pack_app.py`

- [ ] **Step 1：写 pack_app.py**

文件 `scripts/pack_app.py`：

```python
#!/usr/bin/env python3
"""把 app.raw 加上 32 字节 header 得到 app.bin（spec §13.3）。
用法：python pack_app.py <in.raw> <out.bin> [load_addr=0x01000000] [image_type=1]
"""
import struct
import zlib
import sys

MAGIC = 0x5A424F4F   # little-endian 字节序读作 "OOBZ"；与 image_header.h 一致
VERSION = 1
IMAGE_TYPE_THREADX = 1
DEFAULT_LOAD_ADDR = 0x01000000

HEADER_NO_CRC_FMT = "<IIIIII"   # 6 个 uint32 = 24 字节
HEADER_NO_CRC_LEN = 24
HEADER_TOTAL_LEN = 32


def compute_header_crc(buf_no_crc: bytes) -> int:
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
    header = header_no_crc + struct.pack("<I", header_crc)

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
```

- [ ] **Step 2：写 unpack_app.py（CI/调试用）**

文件 `scripts/unpack_app.py`：

```python
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
    header_crc_in = struct.unpack("<I", data[HEADER_NO_CRC_LEN:HEADER_TOTAL_LEN])[0]
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
```

- [ ] **Step 3：写 test_pack_app.py（round-trip 自测，spec §12.1）**

文件 `scripts/test_pack_app.py`：

```python
#!/usr/bin/env python3
"""pack_app.py round-trip 自测：随机 payload → pack → unpack → 校验。"""
import os
import struct
import subprocess
import sys
import tempfile
import zlib

sys.path.insert(0, os.path.dirname(__file__))
from pack_app import (MAGIC, VERSION, IMAGE_TYPE_THREADX, DEFAULT_LOAD_ADDR,
                      HEADER_NO_CRC_FMT, HEADER_NO_CRC_LEN, HEADER_TOTAL_LEN)


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
        # payload 内容一致
        assert data[HEADER_TOTAL_LEN:] == payload
    print("test_roundtrip: PASS")


if __name__ == "__main__":
    test_roundtrip()
    print("All tests passed.")
```

- [ ] **Step 4：跑测试**

```bash
cd /e/桌面/docs/Code/scripts
python test_pack_app.py
```

期望：`test_roundtrip: PASS`、`All tests passed.`

- [ ] **Step 5：跑 CLI smoke**

```bash
# 造一个假 raw，pack 再 unpack
python -c "import sys; sys.stdout.buffer.write(bytes(1024))" > /tmp/fake.raw
python pack_app.py /tmp/fake.raw /tmp/fake.bin
python unpack_app.py /tmp/fake.bin
```

期望：unpack 输出所有字段 OK。

- [ ] **Step 6：提交**

```bash
git add scripts/pack_app.py scripts/unpack_app.py scripts/test_pack_app.py
git commit -m "Phase 5.3: pack_app.py + unpack_app.py + round-trip test (spec §13.3, §12.1)"
```

---

### Task 5.4：写 crc32.c（轻量实现）

**Files:**
- Create: `boot/ssbl/loader/crc32.c`
- Create: `boot/ssbl/loader/crc32.h`

- [ ] **Step 1：写 crc32.h**

```c
/* boot/ssbl/loader/crc32.h */
#ifndef SSBL_CRC32_H
#define SSBL_CRC32_H

#include <stdint.h>

uint32_t crc32_compute(const void *buf, uint32_t len);

#endif
```

- [ ] **Step 2：写 crc32.c（IEEE 802.3 多项式 0xEDB88320，与 zlib 一致）**

```c
/* boot/ssbl/loader/crc32.c — 与 Python zlib.crc32 结果一致（spec §13.3）*/
#include "crc32.h"

static uint32_t crc_table[256];
static int crc_table_init = 0;

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
    ensure_table();
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        crc = crc_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}
```

- [ ] **Step 3：在 image_header.h 同目录或 loader 里建 host 端 unit test**

文件 `boot/ssbl/loader/test_crc32_host.c`（host gcc 编译，spec §12.1 PC 单元测试）：

```c
/* test_crc32_host.c — 用 host gcc 编译跑：gcc test_crc32_host.c crc32.c -o test && ./test
 * 校验：与 Python zlib.crc32 一致。
 */
#include <stdio.h>
#include <string.h>
#include "crc32.h"

int main(void)
{
    /* 测试向量 1：空 */
    uint32_t r0 = crc32_compute("", 0);
    printf("crc32('')  = 0x%08X (expect 0x00000000)\n", r0);
    if (r0 != 0x00000000u) return 1;

    /* 测试向量 2："123456789" → 标准 0xCBF43926 */
    uint32_t r1 = crc32_compute("123456789", 9);
    printf("crc32('123456789') = 0x%08X (expect 0xCBF43926)\n", r1);
    if (r1 != 0xCBF43926u) return 1;

    /* 测试向量 3：256 字节递增 */
    uint8_t buf[256];
    for (int i = 0; i < 256; i++) buf[i] = (uint8_t)i;
    uint32_t r2 = crc32_compute(buf, 256);
    printf("crc32(0..255) = 0x%08X (expect 0x29058C73)\n", r2);
    if (r2 != 0x29058C73u) return 1;

    printf("All CRC32 tests PASS\n");
    return 0;
}
```

- [ ] **Step 4：跑 host test**

```bash
cd boot/ssbl/loader
gcc test_crc32_host.c crc32.c -o test_crc32
./test_crc32
```

期望：三个测试 PASS。期望值已标注（标准 CRC32 测试向量）。

- [ ] **Step 5：与 Python zlib 跨语言校对（额外保险）**

```bash
python -c "import zlib; print(hex(zlib.crc32(bytes(range(256))) & 0xFFFFFFFF))"
```

期望输出：`0x29058c73`，与 Step 4 第三测一致。

- [ ] **Step 6：提交**

```bash
git add boot/ssbl/loader/crc32.c boot/ssbl/loader/crc32.h boot/ssbl/loader/test_crc32_host.c
git commit -m "Phase 5.4: lightweight CRC32 matching Python zlib, host-tested"
```

---

### Task 5.5：写 image_loader.c

**Files:**
- Create: `boot/ssbl/loader/image_loader.c`

- [ ] **Step 1：写 image_loader.c（spec §9.5）**

```c
/* boot/ssbl/loader/image_loader.c — 读 app.bin → 校验 header → 拷 payload 到 DDR
 *
 * 流程（spec §9.5）：
 *   1. storage_file_open(app.bin)
 *   2. 读 32 字节 header
 *   3. image_header_validate：magic + header_crc + version
 *   4. 读 payload（image_size 字节）
 *   5. crc32_compute(payload) == header.crc32
 *   6. memcpy(payload → header.load_addr)  ← header 不进 DDR
 *   7. 返回 load_addr（payload[0] 即 reset 向量）
 */

#include "storage.h"
#include "image_header.h"
#include "crc32.h"
#include "xil_printf.h"
#include <string.h>

#define HDR_BUF_SIZE   IMAGE_HEADER_SIZE
#define CHUNK_SIZE     4096

int image_header_validate(const image_header_t *hdr)
{
    if (hdr->magic != IMAGE_MAGIC) {
        xil_printf("[loader] magic bad: 0x%08X\r\n", hdr->magic);
        return -1;
    }
    if (hdr->version != IMAGE_VERSION_1) {
        xil_printf("[loader] version unsupported: %u\r\n", hdr->version);
        return -1;
    }
    /* header_crc32 覆盖 header[0x00..0x17]，输入是 header 结构体的前 24 字节 */
    uint32_t calc = crc32_compute(hdr, HEADER_NO_CRC_LEN);
    if (calc != hdr->header_crc32) {
        xil_printf("[loader] header_crc bad: calc=0x%08X in=0x%08X\r\n",
                   calc, hdr->header_crc32);
        return -1;
    }
    return 0;
}

/* 把 app.bin 整文件拷到 DDR staging（header+payload 一起读，便于边读边校验）。
 * 返回 0=OK，负数=错误码；成功时 *out_load_addr 写入 load_addr。
 */
int image_loader_load(const char *path, uint32_t *out_load_addr)
{
    int rc;
    image_header_t hdr;
    uint8_t hdr_buf[HDR_BUF_SIZE];

    rc = storage_file_open(path, STORAGE_OPEN_READ);
    if (rc != STORAGE_OK) {
        xil_printf("[loader] open %s failed (%d)\r\n", path, rc);
        return rc;
    }

    /* 读 header */
    int n = storage_file_read(hdr_buf, HDR_BUF_SIZE);
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

    /* 把 payload 流式拷到 DDR load_addr */
    uint8_t *dst = (uint8_t *)hdr.load_addr;
    uint32_t remaining = hdr.image_size;
    uint32_t crc_acc = 0xFFFFFFFFu;   /* 与 crc32_compute 内部一致 */
    uint8_t chunk[CHUNK_SIZE];

    while (remaining > 0) {
        uint32_t want = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
        int got = storage_file_read(chunk, want);
        if (got != (int)want) {
            xil_printf("[loader] payload short read (%d != %u)\r\n", got, want);
            storage_file_close();
            return STORAGE_ERR_IO;
        }
        memcpy(dst, chunk, want);
        /* 增量 CRC（与 crc32_compute 内部累加方式一致）*/
        /* 为保持单次 crc32_compute 的语义，下文用整段 payload 重算 */
        dst += want;
        remaining -= want;
    }
    storage_file_close();

    /* payload 已完整在 DDR，重算 CRC（load_addr 是 DDR 有效地址，可读）*/
    uint32_t calc = crc32_compute((void *)hdr.load_addr, hdr.image_size);
    if (calc != hdr.crc32) {
        xil_printf("[loader] payload crc bad: calc=0x%08X in=0x%08X\r\n",
                   calc, hdr.crc32);
        return STORAGE_ERR_INVAL;
    }

    *out_load_addr = hdr.load_addr;
    xil_printf("[loader] %s OK: load=0x%08X size=%u crc=0x%08X\r\n",
               path, hdr.load_addr, hdr.image_size, hdr.crc32);
    return STORAGE_OK;
}
```

> **Note**：上例的 `crc_acc` 变量未用（为可读性保留），实际 CRC 在拷贝完成后一次性重算。若 SSBL 想避免"重算时再扫一遍 DDR"的 I/O 开销，可改增量 CRC，但需要把 crc32.c 暴露 `crc32_update(start_value, buf, len)` 函数——本 Phase 暂用整段重算，Phase 8 优化。

- [ ] **Step 2：在 image_header.h 暴露 HEADER_NO_CRC_LEN（若未声明）**

在 `boot/ssbl/include/image_header.h` 中，`HEADER_NO_CRC_LEN` 已声明。补一个对外可见的常量：

```c
#define HEADER_NO_CRC_LEN  24    /* header[0x00..0x17] 长度 */
```

- [ ] **Step 3：提交**

```bash
git add boot/ssbl/loader/image_loader.c boot/ssbl/include/image_header.h
git commit -m "Phase 5.5: image_loader reads/validates/copies app.bin to DDR (spec §9.5)"
```

---

### Task 5.6：写 handoff_exit.S 与 jump_to_app

**Files:**
- Create: `boot/ssbl/handoff/handoff_exit.S`
- Create: `boot/ssbl/handoff/handoff.c`
- Create: `boot/ssbl/handoff/handoff.h`

- [ ] **Step 1：写 handoff_exit.S（spec §13.4 完整版）**

文件 `boot/ssbl/handoff/handoff_exit.S`：

```asm
/* boot/ssbl/handoff/handoff_exit.S — SSBL → app 跳转：纯破坏性清理
 * spec §13.4。r0 = app entry address (= load_addr；payload[0] 即 reset 向量，
 * header 已剥离、不在 DDR，故不加 0x20）。
 */

    .section .text.handoff_exit
    .arm
    .global handoff_exit
    .type   handoff_exit, %function

handoff_exit:

    /* 1. 禁所有中断 */
    cpsid   ifa

    /* 2-5. 停 Private Timer + GIC 由 C 侧 handoff.c 提前完成
     *      （XScuGic_Stop + 写 Private Timer 控制寄存器）
     *      这里只做 cache/MMU 相关 */

    /* 6. Flush D-cache（clean + invalidate，写回 DDR） */
    bl      Xil_DCacheFlush

    /* 7. Flush L2 (PL310)：clean + invalidate 一次 */
    bl      Xil_L2CacheFlush

    /* 8. Invalidate I-cache + BTB + TLB */
    bl      Xil_ICacheInvalidate

    /* 9. Disable I-cache / D-cache */
    mrc     p15, 0, r1, c1, c0, 0      /* SCTLR */
    bic     r1, r1, #(1<<12)|(1<<2)    /* 清 I(C12) 和 C(C2) 位 */
    mcr     p15, 0, r1, c1, c0, 0
    isb

    /* 10. Disable MMU（必须在 cache 操作之后） */
    mrc     p15, 0, r1, c1, c0, 0      /* SCTLR */
    bic     r1, r1, #1                 /* 清 M 位 */
    mcr     p15, 0, r1, c1, c0, 0
    isb

    /* 11. invalidate TLB 再一次 */
    mov     r2, #0
    mcr     p15, 0, r2, c8, c7, 0      /* TLBIALL */
    dsb
    isb

    /* 12. 跳转到 app 入口（app 的 _vector_table[0] = reset = load_addr 本身） */
    bx      r0

    .size   handoff_exit, .-handoff_exit
```

- [ ] **Step 2：写 handoff.c（spec §13.4 C 侧）**

文件 `boot/ssbl/handoff/handoff.c`：

```c
/* boot/ssbl/handoff/handoff.c — SSBL → app 跳转 C 接口（spec §9.2 + §13.4）
 *
 * 调用次序：
 *   1. 停所有非自身 ThreadX 线程
 *   2. tx_thread_relinquish 让调度器跑一轮
 *   3. 关 IRQ/FIQ/Async（汇编侧做，这里不重复）
 *   4. 停 Private Timer
 *   5. 停 GIC
 *   6. 调 handoff_exit（汇编）做 cache/MMU 清理 + bx 跳转
 */

#include "handoff.h"
#include "tx_api.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xscugic.h"

extern TX_THREAD  *cli_thread;
extern TX_THREAD  *countdown_thread;
extern TX_THREAD  *trigger_thread;
extern XScuGic     xInterruptController;   /* 由 BSP 提供 */

/* PTIMER 寄存器（Zynq PS Private Timer）*/
#define PTIMER_BASE      0xF8F00600u
#define PTIMER_CONTROL   (*(volatile uint32_t *)(PTIMER_BASE + 0x08))
#define PTIMER_ISR       (*(volatile uint32_t *)(PTIMER_BASE + 0x0C))

extern void handoff_exit(uint32_t entry_addr) __attribute__((noreturn));

void jump_to_app(uint32_t app_load_addr)
{
    /* 1. 停 ThreadX 业务线程 */
    if (cli_thread)       tx_thread_terminate(cli_thread);
    if (countdown_thread) tx_thread_terminate(countdown_thread);
    if (trigger_thread)   tx_thread_terminate(trigger_thread);

    /* 2. 让调度器跑一轮，确保终止生效 */
    tx_thread_relinquish();

    /* 4. 停 Private Timer */
    PTIMER_CONTROL = 0;    /* 清 enable */
    PTIMER_ISR = 1;        /* 清挂起（write 1 to ISR）*/

    /* 5. 停 GIC */
    XScuGic_Stop(&xInterruptController);

    /* 6. 汇编做 cache/MMU 清理 + 跳转。
     *    app 入口 = load_addr（payload[0] 即 reset 向量）。
     *    header 已剥离、不在 DDR，故不加 0x20。*/
    handoff_exit(app_load_addr);    /* 不返回 */
}
```

- [ ] **Step 3：写 handoff.h**

文件 `boot/ssbl/handoff/handoff.h`：

```c
#ifndef SSBL_HANDOFF_H
#define SSBL_HANDOFF_H

#include <stdint.h>

/* 跳转到 app。app_load_addr = app 在 DDR 的起始地址（payload[0] = reset 向量）。
 * 本函数不返回。
 */
void jump_to_app(uint32_t app_load_addr) __attribute__((noreturn));

#endif
```

- [ ] **Step 4：提交**

```bash
git add boot/ssbl/handoff/
git commit -m "Phase 5.6: handoff_exit.S (asm cleanup) + handoff.c (C entry), spec §13.4"
```

---

### Task 5.7：端到端验证 handoff（SSBL 加载 app.bin 并跳转）

**说明**：本任务**临时**修改 SSBL 的 storage_test_thread，改为"load app.bin + jump"——Phase 6 会用真正的 boot_selector 替代。

- [ ] **Step 1：pack app.bin**

```bash
cd /e/桌面/docs/Code
arm-none-eabi-objcopy -O binary boot/app_template/_vitis_ws/app/Debug/app.elf /tmp/app.raw
python scripts/pack_app.py /tmp/app.raw /tmp/app.bin
# 拷到 SD 卡 P2 数据分区（spec §5.5：app 落 P2，不落 P1）
cp /tmp/app.bin <P2盘符>:/app_current.bin
```

- [ ] **Step 2：临时改 storage_test_thread 为 load+jump**

在 `boot/ssbl/src/storage_test.c` 的 `storage_test_thread` 末尾加：

```c
    /* Phase 5 临时：加载 app.bin 并跳转 */
    uint32_t load_addr = 0;
    extern int image_loader_load(const char *path, uint32_t *out_load_addr);
    extern void jump_to_app(uint32_t app_load_addr);

    int rc2 = image_loader_load("app_current.bin", &load_addr);
    if (rc2 == STORAGE_OK) {
        xil_printf("[SSBL] handoff to app @0x%08X\r\n", load_addr);
        jump_to_app(load_addr);    /* 不返回 */
    } else {
        xil_printf("[SSBL] load app_current.bin failed (%d)\r\n", rc2);
    }
```

- [ ] **Step 3：Build SSBL，烧 BOOT.bin 到 SD P1（含新 ssbl.elf）**

```bash
# 直接用 ssbl.elf 打包（spec §13.2，无需 objcopy）
pwsh scripts/build_boot_bin.ps1
# BOOT.bin 落 P1（spec §5.5：P1 仅放 BOOT.BIN）
cp bif/BOOT.BIN <P1盘符>:/
```

- [ ] **Step 4：上电观察**

期望串口：
```
FSBL (slim) ...
[SSBL] Hello from SSBL @0x100000
[SSBL] storage media opened
[SSBL] storage_test: listing root
  BOOT.BIN
  app_current.bin
  boot.cfg
[SSBL] boot.cfg contents:
...
[loader] app_current.bin OK: load=0x01000000 size=... crc=0x...
[SSBL] handoff to app @0x01000000
[APP] Hello from app @0x1000000
[APP] start task, creating LED1 task
```
随后 LED1 1s 闪烁（区别于 SSBL 的 LED0 2s）。

- [ ] **Step 5：失败排查清单**

| 现象 | 可能原因 |
|---|---|
| `[loader] magic bad` | pack_app.py 与 image_header.h MAGIC 不一致；header 被错位读取 |
| `[loader] payload crc bad` | crc32_compute 与 Python zlib 不一致（回查 Task 5.4 跨语言校对） |
| handoff 后 hard fault | entry 加了 0x20；MMU 没关；cache 没 flush；reset.S 与 app.ld 不匹配 |
| app 起来但 LED 不闪 | led1 未注册（同 Phase 1） |

- [ ] **Step 6：提交（标记 Phase 5 完成，临时代码下个 Phase 替换）**

```bash
git add boot/ssbl/src/storage_test.c
git commit -m "Phase 5.7: end-to-end SSBL→app handoff verified (temp load+jump in storage_test)"
```

---
