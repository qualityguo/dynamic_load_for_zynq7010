#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
将单个 7000 行实现计划按 Phase 切分为独立分章文件。

输入：docs/superpowers/plans/2026-06-22-zynq-multistage-boot.md（原文件）
输出：docs/superpowers/plans/phases/phase-NN-*.md（13 个分章）

切分规则：
  - 以顶层标题 "## Phase N：..." 为分章锚点
  - 每个分章从其 "## Phase N" 标题起，到下一个 "## Phase N+1" 标题前为止
  - "## 计划总览" 之前的内容（Front Matter）不进任何分章
  - "## 计划自审 / 执行建议 / 计划完整" 等尾部章节不进任何分章（由总述/校验章覆盖）
  - 每个 Phase 末尾的 "---" 分隔符归属该 Phase

每个分章文件头部自动加：
  - 导航块（上一章/下一章/返回总述）
  - 该 Phase 在总览表中的元信息（spec 对应、里程碑、交付物）
"""

import re
import sys
from pathlib import Path

SRC = Path("docs/superpowers/plans/2026-06-22-zynq-multistage-boot.md")
DST_DIR = Path("docs/superpowers/plans/phases")

# Phase 元信息表（从总览表手工提取，供分章头部引用）
# (phase_num, slug, title_zh, spec_ref, milestone, deliverable)
PHASES = [
    (0,  "scaffold",            "顶层目录骨架与 vendor 拷贝",
     "§3", "—",  "顶层目录骨架 + vendor 拷贝脚本"),
    (1,  "ssbl-skeleton",       "SSBL 工程骨架",
     "§8", "M1", "SSBL 工程骨架（基于 hello_threadx，ORIGIN=0x100000）"),
    (2,  "fsbl",                "精简 FSBL",
     "§7", "M2", "精简 FSBL（裁剪现有 zynq_fsbl）"),
    (3,  "fsbl-ssbl-chain",     "FSBL → SSBL 链通",
     "§13.2", "M3", "FSBL → SSBL 链通（boot.bif + BOOT.BIN）"),
    (4,  "storage-filex",       "storage 抽象层 + FileX SD 挂载",
     "§11", "M4", "storage 抽象层 + FileX SD 卡挂载"),
    (5,  "app-template-handoff","app_template + pack_app.py + handoff",
     "§9.5, §13.3", "M5", "app_template + pack_app.py + handoff"),
    (6,  "boot-cfg",            "boot.cfg 解析 + boot_selector 自动选 app",
     "§5", "M6", "boot.cfg 解析 + boot_selector 自动选 app"),
    (7,  "cli",                 "CLI 子系统",
     "§6", "M7", "CLI 子系统（触发 + 命令集）"),
    (8,  "ymodem-upgrade",      "YMODEM 两阶段升级",
     "§6.3", "M8", "YMODEM 两阶段升级（DDR 暂存 + 原子提交）"),
    (9,  "bitstream",           "bitstream 动态加载",
     "§4.2", "M9", "bitstream 动态加载（FileX + PCAP）"),
    (10, "error-handling",      "错误处理 + LED 错误码 + OCM 标记",
     "§10", "M10", "错误处理 + LED 错误码 + boot.log/OCM 标记"),
    (11, "qspi",                "QSPI 迁移",
     "§11.2", "M11", "QSPI 迁移（FileX + LevelX NOR）"),
    (12, "network-web",         "NetXDuo 集成 + Web 配置页 + 网络升级",
     "网络（新增）", "M12", "NetXDuo 集成 + Web 配置页 + 网络升级（复用现有 hello_netxduo 移植）"),
]

# 尾部章节锚点（这些 ## 标题之后的内容不归属任何 Phase）
TAIL_ANCHORS = ["## 计划自审", "## 执行建议", "## 计划完整"]


def build_header(num, slug, title, spec_ref, milestone, deliverable,
                 prev_info, next_info):
    """构建分章文件的统一头部：导航 + 元信息 + 接口提示。"""
    nav_lines = []
    nav_lines.append("> **章节导航**：")
    nav_parts = []
    if prev_info:
        nav_parts.append(f"[← 上一章：{prev_info[2]}](phase-{prev_info[0]:02d}-{prev_info[1]}.md)")
    nav_parts.append("[↑ 返回总述](../2026-06-22-zynq-multistage-boot.md)")
    if next_info:
        nav_parts.append(f"[下一章：{next_info[2]} →](phase-{next_info[0]:02d}-{next_info[1]}.md)")
    nav_lines.append("  " + " ｜ ".join(nav_parts))
    nav_lines.append("")

    meta_lines = [
        f"> **Phase 元信息**",
        f"> - 对应 Spec：`{spec_ref}`",
        f"> - 里程碑：`{milestone}`",
        f"> - 交付物：{deliverable}",
        f"> - 分章文件：`phases/phase-{num:02d}-{slug}.md`",
        "",
    ]

    iface_lines = [
        "> **接口定位**：本章对其他 Phase 的依赖与对外暴露的符号，见",
        "> [总述章 · 模块依赖矩阵](../2026-06-22-zynq-multistage-boot.md#3-模块依赖矩阵)。",
        "> 新增功能时，先查该矩阵确认接口落点，再回本章扩展。",
        "",
        "---",
        "",
    ]

    return "\n".join(nav_lines + meta_lines + iface_lines)


def split():
    if not SRC.exists():
        print(f"ERROR: 源文件不存在：{SRC}", file=sys.stderr)
        sys.exit(1)

    text = SRC.read_text(encoding="utf-8")
    lines = text.split("\n")

    # 找每个 "## Phase N" 的起始行号
    phase_starts = []  # [(line_idx, phase_num)]
    for i, line in enumerate(lines):
        if line.startswith("## Phase "):
            m = re.match(r"^## Phase (\d+)[：:]", line)
            if m:
                phase_starts.append((i, int(m.group(1))))

    # 找尾部锚点（第一个出现的）
    tail_start = None
    for i, line in enumerate(lines):
        if any(line.startswith(a) for a in TAIL_ANCHORS):
            tail_start = i
            break

    if not phase_starts:
        print("ERROR: 未找到任何 Phase 标题", file=sys.stderr)
        sys.exit(1)

    print(f"找到 {len(phase_starts)} 个 Phase，尾部锚点在行 {tail_start}")

    # 每个 Phase 的内容范围：[start, next_start 或 tail_start)
    phase_ranges = []
    for idx, (start, num) in enumerate(phase_starts):
        if idx + 1 < len(phase_starts):
            end = phase_starts[idx + 1][0]
        else:
            end = tail_start if tail_start else len(lines)
        phase_ranges.append((num, start, end))

    DST_DIR.mkdir(parents=True, exist_ok=True)

    total_out_lines = 0
    for idx, (num, start, end) in enumerate(phase_ranges):
        # 找元信息
        meta = next(p for p in PHASES if p[0] == num)
        _, slug, title, spec_ref, milestone, deliverable = meta
        prev_info = PHASES[idx - 1] if idx > 0 else None
        next_info = PHASES[idx + 1] if idx + 1 < len(PHASES) else None

        header = build_header(num, slug, title, spec_ref, milestone, deliverable,
                              prev_info, next_info)

        body = "\n".join(lines[start:end])
        # 去掉 body 末尾多余空行，保留一个
        body = body.rstrip("\n") + "\n"

        out_path = DST_DIR / f"phase-{num:02d}-{slug}.md"
        content = header + body
        out_path.write_text(content, encoding="utf-8")

        out_lines = content.count("\n")
        total_out_lines += out_lines
        print(f"  Phase {num:02d}: {out_path.name}  ({end - start} 源行 → {out_lines} 输出行)")

    print(f"\n13 个分章已写入 {DST_DIR}/")
    print(f"分章合计输出行数：{total_out_lines}（含每章 ~12 行头部模板）")


if __name__ == "__main__":
    split()
