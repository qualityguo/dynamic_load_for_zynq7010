# -*- coding: utf-8 -*-
r"""
BOOT.BIN 生成器（双击即可运行）
参考 ssbl\new_ssbl_system\_ide\bootimage\new_ssbl_system.bif 的格式，
用 Vitis 自带的 bootgen 把本目录下的 new_fsbl.elf + new_ssbl.elf
打包成 boot.bin。
"""

import os
import sys
import subprocess

# bootgen.bat 绝对路径（如换 Vitis 版本/路径只改这一行）
BOOTGEN = r"D:\Xilinx2021\Vitis\2021.1\bin\bootgen.bat"

# 目标器件架构（zynq / zynqmp / versal ...）
ARCH = "zynq"

# 本目录下的各组成部分
FSBL_ELF = "new_fsbl.elf"   # 第一级引导（bootloader）
SSBL_ELF = "new_ssbl.elf"   # 第二级引导 / 用户程序
OUTPUT_BIN = "boot.bin"     # 输出镜像
BIF_NAME = "boot.bif"       # 运行时生成的临时 BIF


def pause_and_exit(code):
    try:
        input("\n按回车退出...")
    except (EOFError, KeyboardInterrupt):
        pass
    sys.exit(code)


def main():
    # 1. 以脚本所在目录为工作区，确保路径与其中的 elf 无关当前工作目录
    here = os.path.dirname(os.path.abspath(__file__))

    fsbl_path = os.path.join(here, FSBL_ELF)
    ssbl_path = os.path.join(here, SSBL_ELF)
    bif_path = os.path.join(here, BIF_NAME)
    out_path = os.path.join(here, OUTPUT_BIN)

    # 2. 检查输入 elf
    for p in (fsbl_path, ssbl_path):
        if not os.path.isfile(p):
            print("[错误] 找不到文件: %s" % p)
            pause_and_exit(1)

    # 3. 检查 bootgen
    if not os.path.isfile(BOOTGEN):
        print("[错误] 找不到 bootgen: %s" % BOOTGEN)
        pause_and_exit(1)

    # 4. 生成 BIF（路径统一用正斜杠，bootgen 通用）
    bif_content = (
        "//arch = %s; split = false; format = BIN\n"
        "the_ROM_image:\n"
        "{\n"
        "\t[bootloader]%s\n"
        "\t%s\n"
        "}\n"
    ) % (ARCH, fsbl_path.replace("\\", "/"), ssbl_path.replace("\\", "/"))

    with open(bif_path, "w", encoding="ascii") as f:
        f.write(bif_content)
    print("[BIF] 已写出 %s" % bif_path)

    # 5. 调用 bootgen：-image 输入 BIF，-o 输出 BOOT.BIN，-w on 允许覆盖
    cmd = [BOOTGEN, "-arch", ARCH, "-image", bif_path,
           "-o", out_path, "-w", "on"]
    print("[CMD] " + " ".join('"%s"' % c for c in cmd))
    try:
        subprocess.check_call(cmd)
    except subprocess.CalledProcessError as e:
        print("[失败] bootgen 返回码 %d" % e.returncode)
        pause_and_exit(1)
    except Exception as e:
        print("[失败] %s" % e)
        pause_and_exit(1)

    # 6. 校验输出
    if os.path.isfile(out_path):
        size = os.path.getsize(out_path)
        print("[成功] 生成 %s (%d 字节)" % (OUTPUT_BIN, size))
    else:
        print("[失败] 未找到输出 %s" % out_path)
        pause_and_exit(1)
    pause_and_exit(0)


if __name__ == "__main__":
    main()
