# -*- coding: utf-8 -*-
"""
ELF -> BIN 转换器（拖拽使用）
把 *.elf 文件拖到本脚本图标上，即用 arm-none-eabi-objcopy 在 elf 同目录下
生成同名 *.bin。
"""

import os
import sys
import subprocess

# objcopy 绝对路径（如换工具链只改这一行）
OBJCOPY = r"arm-none-eabi-objcopy.exe"


def pause_and_exit(code):
    try:
        input("\n按回车退出...")
    except (EOFError, KeyboardInterrupt):
        pass
    sys.exit(code)


def main():
    # 1. 参数检查：拖拽时被拖文件路径在 argv[1]
    if len(sys.argv) < 2:
        print("[错误] 未传入文件，请把 .elf 拖到本脚本图标上。")
        pause_and_exit(1)

    elf_path = sys.argv[1].strip().strip('"')

    # 2. 输入文件存在性
    if not os.path.isfile(elf_path):
        print("[错误] 找不到文件: %s" % elf_path)
        pause_and_exit(1)

    # 3. 仅接受 .elf
    base, ext = os.path.splitext(elf_path)
    if ext.lower() != ".elf":
        print("[错误] 不是 .elf 文件: %s" % elf_path)
        pause_and_exit(1)

    # 4. 输出路径：同目录 + 同名 .bin
    bin_path = base + ".bin"

    # 5. objcopy 存在性
    if not os.path.isfile(OBJCOPY):
        print("[错误] 找不到 objcopy: %s" % OBJCOPY)
        pause_and_exit(1)

    # 6. 调用 objcopy 转换
    cmd = [OBJCOPY, "-O", "binary", elf_path, bin_path]
    print("输入: %s" % elf_path)
    print("输出: %s" % bin_path)
    try:
        subprocess.check_call(cmd)
    except subprocess.CalledProcessError as e:
        print("[失败] objcopy 返回码 %d" % e.returncode)
        pause_and_exit(1)
    except Exception as e:
        print("[失败] %s" % e)
        pause_and_exit(1)

    # 7. 成功
    size = os.path.getsize(bin_path)
    print("[成功] 生成 %s (%d 字节)" % (os.path.basename(bin_path), size))
    pause_and_exit(0)


if __name__ == "__main__":
    main()
