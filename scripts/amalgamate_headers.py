#!/usr/bin/env python3
"""
scripts/amalgamate_headers.py — 将 Vora 头文件合并为单个 vora.hpp

用法:
    python scripts/amalgamate_headers.py -o vora.hpp

输入:
    src/vora.h  (统一入口)

输出:
    vora.hpp — 单个自包含头文件，无需任何其他 include 路径
                #include "vora.hpp" + link vora_lib.lib 即可嵌入

原理:
    从 vora.h 出发，递归解析所有 #include "..." 指令（只处理本地头文件，
    跳过 <system> 系统头文件），按依赖顺序逐文件内联，去除重复。
"""

import argparse
import os
import re
import sys
from pathlib import Path

# ── 解析 #include "..." ──────────────────────────────────────────────────
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"([^"]+)"')

def read_file_clean(filepath: str) -> str:
    """读取文件内容，去除 BOM、统一换行符。"""
    with open(filepath, encoding="utf-8-sig") as f:  # utf-8-sig 自动跳过 BOM
        return f.read()

def parse_local_includes(content: str) -> list[str]:
    """提取所有本地 #include "..." 路径（不含系统 <...> 头文件）。"""
    result = []
    for line in content.splitlines():
        m = INCLUDE_RE.match(line)
        if m:
            result.append(m.group(1))
    return result


# ── 路径解析 ────────────────────────────────────────────────────────────
def resolve_include(include_path: str, current_file: str, src_dir: str) -> str | None:
    """
    将 #include "xxx.h" 解析为相对于 src_dir 的路径。
    支持两种形式:
      - "common/error_reporter.h"  → 相对 src/
      - "../runtime/value.h"       → 相对当前文件所在目录
    返回相对于 src_dir 的规范路径，若文件不存在则返回 None。
    """
    candidates = [
        # 方式 1: 相对 src/ 根目录
        os.path.normpath(os.path.join(src_dir, include_path)),
        # 方式 2: 相对当前文件所在目录
        os.path.normpath(os.path.join(os.path.dirname(current_file), include_path)),
    ]
    for p in candidates:
        if os.path.isfile(p):
            return os.path.relpath(os.path.abspath(p), os.path.abspath(src_dir))
    return None


# ── 递归收集 ────────────────────────────────────────────────────────────
def collect_headers(entry: str, src_dir: str) -> list[str]:
    """
    从入口文件出发，按拓扑顺序收集所有需要内联的本地头文件。
    返回相对于 src_dir 的路径列表（已去重，依赖在前）。
    """
    seen = set()
    order = []

    def walk(rel_path: str):
        if rel_path in seen:
            return
        seen.add(rel_path)

        filepath = os.path.join(src_dir, rel_path)
        content = read_file_clean(filepath)

        # 先递归处理依赖
        for inc in parse_local_includes(content):
            resolved = resolve_include(inc, filepath, src_dir)
            if resolved:
                walk(resolved)

        order.append(rel_path)

    walk(entry)
    return order


# ── 生成合并文件 ────────────────────────────────────────────────────────
def amalgamate(entry: str, src_dir: str, include_dir: str) -> str:
    """生成合并后的头文件内容。"""
    header_order = collect_headers(entry, src_dir)

    # 收集所有系统头文件
    system_includes = set()
    for rel_path in header_order:
        filepath = os.path.join(src_dir, rel_path)
        content = read_file_clean(filepath)
        for line in content.splitlines():
            m = re.match(r'^\s*#\s*include\s*<([^>]+)>', line)
            if m:
                system_includes.add(m.group(1))

    # 也需要从 third-party include 目录收集
    if include_dir:
        json_hpp = os.path.join(include_dir, "nlohmann", "json.hpp")
        if os.path.isfile(json_hpp):
            header_order.append("__nlohmann_json.hpp")
            # nlohmann json.hpp 是一个单文件库，直接内联
            pass

    lines = []
    lines.append("// vora.hpp — Vora language single-header embedding API")
    lines.append("//")
    lines.append("// 自动生成，请勿手动编辑。")
    lines.append(f"// 来源: {entry}")
    lines.append("//")
    lines.append("// 用法:")
    lines.append('//   #include "vora.hpp"')
    lines.append("//   链接: vora_lib.lib (或 libvora_lib.a)")
    lines.append("//   编译选项: C++17 或更高，MSVC 需 /utf-8")
    lines.append("")
    lines.append("#pragma once")
    lines.append("")
    lines.append("// MSVC: 抑制 UTF-8 源文件警告 + forward-declaration 差异警告")
    lines.append("#if defined(_MSC_VER) && !defined(__clang__)")
    lines.append("#  pragma warning(disable: 4819)")   # UTF-8 BOM / code page
    lines.append("#  pragma warning(disable: 4099)")   # struct vs class mismatch
    lines.append("#endif")
    lines.append("")

    # 系统头文件
    for h in sorted(system_includes):
        lines.append(f"#include <{h}>")
    if system_includes:
        lines.append("")

    # 本地头文件内容
    for rel_path in header_order:
        if rel_path == "__nlohmann_json.hpp":
            # 内联 nlohmann/json.hpp
            lines.append("// =======================================================================")
            lines.append(f"// [vendored] nlohmann/json.hpp")
            lines.append("// =======================================================================")
            json_content = read_file_clean(os.path.join(include_dir, "nlohmann", "json.hpp"))
            lines.append(json_content)
            lines.append("")
            continue

        filepath = os.path.join(src_dir, rel_path)
        content = read_file_clean(filepath)

        lines.append("// =======================================================================")
        lines.append(f"// [{rel_path}]")
        lines.append("// =======================================================================")

        for line in content.splitlines():
            # 跳过 #pragma once
            if line.strip() == "#pragma once":
                continue
            # 跳过本地 #include "..."（已内联）
            if INCLUDE_RE.match(line):
                continue
            lines.append(line)

        lines.append("")

    return "\n".join(lines)


# ── main ─────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="将 Vora 头文件合并为单个 vora.hpp")
    parser.add_argument("-o", "--output", default="vora.hpp",
                        help="输出文件路径 (默认: vora.hpp)")
    args = parser.parse_args()

    # 定位项目根目录
    script_dir = Path(__file__).resolve().parent
    project_root = script_dir.parent
    src_dir = project_root / "src"
    include_dir = project_root / "include"

    entry = "vora.h"
    entry_path = src_dir / entry
    if not entry_path.is_file():
        print(f"错误: 找不到入口文件 {entry_path}", file=sys.stderr)
        sys.exit(1)

    print(f"合并 {entry} 及其依赖...")
    header_order = collect_headers(entry, str(src_dir))
    print(f"  收集到 {len(header_order)} 个本地头文件:")
    for h in header_order:
        print(f"    {h}")

    output = amalgamate(entry, str(src_dir), str(include_dir) if include_dir.is_dir() else None)

    out_path = Path(args.output)
    out_path.write_text(output, encoding="utf-8")
    print(f"\n已生成: {out_path} ({len(output):,} bytes)")


if __name__ == "__main__":
    main()
