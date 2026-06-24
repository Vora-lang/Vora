#!/usr/bin/env python3
"""
build-website-docs.py — 将 USER_GUIDE.md 转换为网站 HTML 页面

Usage:
    python scripts/build-website-docs.py [--check]

    Without --check: 生成 D:/Vora-lang.github.io/docs/language.html
    With --check:    检查生成的 HTML 是否过期（用于 CI）

Requires: pip install markdown (Python-Markdown library)
          如果未安装，脚本会尝试使用内置的简易 Markdown 转换器。

Dependency-free fallback: 内置简易转换器处理标准 Markdown 语法
  - 标题 (##, ###)
  - 代码块 (```)
  - 内联代码 (`)
  - 表格 (| ... |)
  - 加粗/斜体
  - 无序/有序列表
  - 链接
  - 段落
"""

import os
import re
import sys
from pathlib import Path

# ── Paths ──────────────────────────────────────────────────────────────────
REPO_ROOT = Path(__file__).resolve().parent.parent
USER_GUIDE = REPO_ROOT / "USER_GUIDE.md"
WEBSITE_ROOT = REPO_ROOT.parent / "Vora-lang.github.io"
OUTPUT_FILE = WEBSITE_ROOT / "docs" / "language.html"

# ── Vora syntax highlighting ───────────────────────────────────────────────

VORA_KEYWORDS = {
    "let", "const", "func", "return", "if", "else", "while", "for",
    "in", "break", "continue", "throw", "try", "catch", "finally",
    "Obj", "this", "super", "import", "export", "from", "as",
    "yield", "true", "false", "null",
}

VORA_BUILTINS = {
    "print", "type", "len", "clock", "assert", "int", "float",
    "toString", "input", "range", "bin", "oct", "hex", "iter", "next",
    "abs", "sqrt", "sin", "cos", "min", "max",
    "jsonParse", "jsonStringify",
    "random_int", "random_float",
}

def highlight_vora_code(code: str) -> str:
    """Apply HTML spans for Vora syntax highlighting."""
    lines = []
    for line in code.split("\n"):
        result = []
        i = 0
        while i < len(line):
            # Comments
            if line[i:i+2] == "//":
                result.append(f'<span class="cmt">{escape_html(line[i:])}</span>')
                break
            # Strings (double quote)
            if line[i] == '"':
                j = i + 1
                while j < len(line):
                    if line[j] == '\\':
                        j += 2
                        continue
                    if line[j] == '"':
                        j += 1
                        break
                    j += 1
                result.append(f'<span class="str">{escape_html(line[i:j])}</span>')
                i = j
                continue
            # Strings (single quote)
            if line[i] == "'":
                j = i + 1
                while j < len(line):
                    if line[j] == '\\':
                        j += 2
                        continue
                    if line[j] == "'":
                        j += 1
                        break
                    j += 1
                result.append(f'<span class="str">{escape_html(line[i:j])}</span>')
                i = j
                continue
            # Numbers
            m = re.match(r'\b(\d+\.?\d*(?:[eE][+-]?\d+)?)\b', line[i:])
            if m:
                result.append(f'<span class="num">{m.group(1)}</span>')
                i += len(m.group(1))
                continue
            # Words (keywords / builtins / identifiers)
            m = re.match(r'\b([a-zA-Z_]\w*)\b', line[i:])
            if m:
                word = m.group(1)
                if word in VORA_KEYWORDS:
                    result.append(f'<span class="kw">{word}</span>')
                elif word in VORA_BUILTINS:
                    result.append(f'<span class="fn">{word}</span>')
                else:
                    result.append(word)
                i += len(word)
                continue
            # Operators and punctuation
            result.append(escape_html(line[i]))
            i += 1
        lines.append("".join(result))
    return "\n".join(lines)


def escape_html(text: str) -> str:
    """Escape HTML special characters."""
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


# ── Simple Markdown → HTML converter (no external dependencies) ────────────

def md_to_html(md_text: str) -> str:
    """
    Convert Markdown to HTML.
    Handles: headings, code blocks (fenced + indented), inline code,
    tables, bold/italic, links, images, lists, paragraphs, horizontal rules.
    """
    lines = md_text.split("\n")
    output = []
    i = 0
    in_code_block = False
    code_lang = ""
    code_lines = []
    in_table = False
    table_rows = []
    in_list = False
    list_tag = ""
    in_blockquote = False

    def flush_code_block():
        nonlocal code_lang, code_lines
        code = "\n".join(code_lines)
        if code_lang.lower() == "vora":
            code = highlight_vora_code(code)
        else:
            code = escape_html(code)
        if code_lang:
            cls = ' class="shell"' if code_lang in ("bash", "sh", "shell") else ""
        else:
            cls = ""
        output.append(f'<div class="code"><pre>{code}</pre></div>')
        code_lang = ""
        code_lines = []

    def flush_table():
        nonlocal table_rows
        if not table_rows:
            return
        html = '<table>\n'
        for ri, row in enumerate(table_rows):
            tag = "th" if ri == 0 else "td"
            html += "<tr>"
            for cell in row:
                html += f"<{tag}>{cell.strip()}</{tag}>"
            html += "</tr>\n"
        html += "</table>"
        output.append(html)
        table_rows = []

    def flush_list():
        nonlocal in_list, list_tag
        if in_list:
            output.append(f"</{list_tag}>")
        in_list = False
        list_tag = ""

    def flush_blockquote():
        nonlocal in_blockquote
        if in_blockquote:
            output.append("</blockquote>")
        in_blockquote = False

    def process_inline(text: str) -> str:
        """Process inline Markdown: **bold**, *italic*, `code`, [link](url), ![img](url)."""
        # Inline code
        text = re.sub(r'`([^`]+)`', r'<code>\1</code>', text)
        # Bold
        text = re.sub(r'\*\*(.+?)\*\*', r'<strong>\1</strong>', text)
        # Italic
        text = re.sub(r'\*(.+?)\*', r'<em>\1</em>', text)
        # Links
        text = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', r'<a href="\2">\1</a>', text)
        # Images
        text = re.sub(r'!\[([^\]]*)\]\(([^)]+)\)', r'<img src="\2" alt="\1">', text)
        return text

    while i < len(lines):
        line = lines[i]

        # Fenced code block
        if line.startswith("```"):
            if in_code_block:
                flush_code_block()
                in_code_block = False
            else:
                flush_list()
                flush_table()
                flush_blockquote()
                in_code_block = True
                code_lang = line[3:].strip()
                code_lines = []
            i += 1
            continue

        if in_code_block:
            code_lines.append(line)
            i += 1
            continue

        # Indented code block (4 spaces / tab)
        if line.startswith("    ") or line.startswith("\t"):
            flush_list()
            flush_table()
            output.append(f'<div class="code"><pre>{escape_html(line[4:] if line.startswith("    ") else line[1:])}</pre></div>')
            i += 1
            continue

        # Horizontal rule
        if re.match(r'^---+$', line.strip()):
            flush_list()
            flush_table()
            flush_blockquote()
            output.append("<hr>")
            i += 1
            continue

        # Table — detect header separator line
        if re.match(r'^\|.*\|$', line.strip()) and i + 1 < len(lines) and re.match(r'^\|[\s\-:|]+\|$', lines[i + 1].strip()):
            flush_list()
            flush_blockquote()
            in_table = True
            table_rows = []
            # Header row
            cells = [c for c in line.split("|") if c.strip() or c == ""]
            cells = [c for c in cells if c != "" or cells.index(c) in [idx for idx, x in enumerate(cells) if True]]
            table_rows.append([process_inline(c.strip()) for c in line.split("|")[1:-1]])
            i += 2
            # Data rows
            while i < len(lines) and re.match(r'^\|.*\|$', lines[i].strip()):
                table_rows.append([process_inline(c.strip()) for c in lines[i].split("|")[1:-1]])
                i += 1
            flush_table()
            in_table = False
            continue

        # Heading
        m = re.match(r'^(#{1,6})\s+(.+)$', line)
        if m:
            flush_list()
            flush_table()
            flush_blockquote()
            level = len(m.group(1))
            text = process_inline(m.group(2))
            # Remove anchor links from heading text for display
            anchor_match = re.match(r'^(.+?)\s*/\s*.+$', text)
            heading_id = re.sub(r'[^a-zA-Z0-9一-鿿-]', '-', m.group(2).lower())
            heading_id = re.sub(r'-+', '-', heading_id).strip('-')
            output.append(f'<h{level} id="{heading_id}">{text}</h{level}>')
            i += 1
            continue

        # Blockquote
        if line.startswith("> "):
            flush_list()
            flush_table()
            if not in_blockquote:
                output.append("<blockquote>")
                in_blockquote = True
            content = process_inline(line[2:])
            # Version tag styling
            if "引入版本" in content or "Since:" in content:
                output.append(f'<p class="version-tag">{content}</p>')
            else:
                output.append(f"<p>{content}</p>")
            i += 1
            # Continue blockquote if next line is also >
            while i < len(lines) and lines[i].startswith("> "):
                output.append(f"<p>{process_inline(lines[i][2:])}</p>")
                i += 1
            continue

        # Unordered list
        m_ul = re.match(r'^(\s*)[-*]\s+(.+)$', line)
        if m_ul:
            flush_table()
            flush_blockquote()
            if not in_list or list_tag != "ul":
                flush_list()
                output.append("<ul>")
                in_list = True
                list_tag = "ul"
            output.append(f"<li>{process_inline(m_ul.group(2))}</li>")
            i += 1
            continue

        # Ordered list
        m_ol = re.match(r'^(\s*)\d+\.\s+(.+)$', line)
        if m_ol:
            flush_table()
            flush_blockquote()
            if not in_list or list_tag != "ol":
                flush_list()
                output.append("<ol>")
                in_list = True
                list_tag = "ol"
            output.append(f"<li>{process_inline(m_ol.group(2))}</li>")
            i += 1
            continue

        # Empty line
        if not line.strip():
            flush_list()
            flush_blockquote()
            if output and output[-1] != "<br>":
                output.append("<br>")
            i += 1
            continue

        # Paragraph
        flush_list()
        flush_blockquote()
        output.append(f"<p>{process_inline(line)}</p>")
        i += 1

    # Flush remaining state
    flush_code_block()
    flush_table()
    flush_list()
    flush_blockquote()

    return "\n".join(output)


# ── Website page template ──────────────────────────────────────────────────

def build_page(content_html: str) -> str:
    """Wrap content HTML in the full website page template."""
    return f'''<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1.0" />
<title>语言参考 — Vora 文档</title>
<link rel="icon" href="../vora_logo.svg" type="image/svg+xml" />
<style>
    *, *::before, *::after {{ margin: 0; padding: 0; box-sizing: border-box; }}
    :root {{
        --bg: #ffffff; --card: #f5f5f7; --border: #e0e0e0;
        --text: #1d1d1f; --text2: #6e6e73; --kw: #7c3aed; --str: #059669;
        --num: #d97706; --fn: #2563eb; --lit: #ea580c; --cmt: #a1a1aa;
        --radius: 10px;
        --mono: "Cascadia Code", "Fira Code", "JetBrains Mono", "Consolas", monospace;
        --sans: "Inter", "Segoe UI", system-ui, -apple-system, sans-serif;
    }}
    html {{ scroll-behavior: smooth; font-size: 16px; }}
    body {{ font-family: var(--sans); background: var(--bg); color: var(--text); line-height: 1.7; -webkit-font-smoothing: antialiased; }}
    .nav {{ position: sticky; top: 0; z-index: 100; background: rgba(255,255,255,0.85); backdrop-filter: blur(12px); border-bottom: 1px solid var(--border); padding: 0 24px; }}
    .nav-inner {{ max-width: 960px; margin: 0 auto; display: flex; align-items: center; gap: 24px; height: 52px; font-size: 0.9rem; font-weight: 500; }}
    .nav a {{ color: var(--text2); text-decoration: none; transition: color 0.2s; }}
    .nav a:hover {{ color: var(--kw); }}
    .nav .active {{ color: var(--kw); }}
    .section {{ max-width: 960px; margin: 0 auto; padding: 60px 24px; }}
    .section h1 {{ font-size: 2.2rem; font-weight: 800; margin-bottom: 8px; }}
    .section h2 {{ font-size: 1.5rem; font-weight: 700; margin: 48px 0 12px; padding-top: 24px; border-top: 1px solid var(--border); }}
    .section h2:first-of-type {{ border-top: none; padding-top: 0; }}
    .section h3 {{ font-size: 1.15rem; font-weight: 600; margin: 28px 0 8px; }}
    .section-desc {{ color: var(--text2); margin-bottom: 32px; font-size: 1.05rem; }}
    .code {{ background: var(--card); border: 1px solid var(--border); border-radius: var(--radius); padding: 18px 22px; font-family: var(--mono); font-size: 0.88rem; line-height: 1.65; overflow-x: auto; margin-bottom: 20px; color: var(--text); }}
    .shell {{ background: #1e1e2e; color: #cdd6f4; border-radius: var(--radius); padding: 14px 18px; font-family: var(--mono); font-size: 0.85rem; line-height: 1.6; overflow-x: auto; margin-bottom: 20px; }}
    .shell .prompt {{ color: #a6e3a1; }}
    .shell .out {{ color: #bac2de; }}
    code {{ font-family: var(--mono); font-size: 0.88em; }}
    p code, li code, td code {{ background: #f3f0ff; color: var(--kw); padding: 2px 6px; border-radius: 4px; font-size: 0.85em; }}
    .kw {{ color: var(--kw); }} .str {{ color: var(--str); }} .num {{ color: var(--num); }}
    .fn {{ color: var(--fn); }} .lit {{ color: var(--lit); }} .cmt {{ color: var(--cmt); font-style: italic; }}
    .interp {{ color: #db2777; }} .type {{ color: #0d9488; }}
    table {{ width: 100%; border-collapse: collapse; font-size: 0.92rem; margin: 16px 0 32px; }}
    th, td {{ text-align: left; padding: 10px 14px; border-bottom: 1px solid var(--border); }}
    th {{ font-weight: 600; color: var(--text2); font-size: 0.8rem; text-transform: uppercase; letter-spacing: 0.05em; }}
    tr:hover td {{ background: #fafafa; }}
    .note {{ background: #fefce8; border: 1px solid #fde68a; border-radius: var(--radius); padding: 14px 18px; margin: 20px 0; font-size: 0.9rem; color: #92400e; }}
    .note strong {{ color: #d97706; }}
    .version-tag {{ font-size: 0.85rem; color: var(--text2); font-style: italic; margin: 4px 0 16px; }}
    .footer {{ border-top: 1px solid var(--border); padding: 40px 24px; text-align: center; color: var(--text2); font-size: 0.85rem; }}
    .footer a {{ color: var(--kw); text-decoration: none; }}
    .footer a:hover {{ text-decoration: underline; }}
    ul, ol {{ margin: 8px 0 16px 24px; }}
    li {{ margin-bottom: 4px; }}
    hr {{ border: none; border-top: 1px solid var(--border); margin: 32px 0; }}
    blockquote {{ border-left: 3px solid #c4b5fd; padding: 8px 16px; margin: 16px 0; background: #faf5ff; border-radius: 0 var(--radius) var(--radius) 0; }}
    @media (max-width: 768px) {{ .section h1 {{ font-size: 1.6rem; }} .section h2 {{ font-size: 1.3rem; }} }}
</style>
</head>
<body>

<nav class="nav">
    <div class="nav-inner">
        <a href="../index.html" style="font-size:1.2rem;font-weight:800;background:linear-gradient(135deg,#6366f1,#a78bfa);-webkit-background-clip:text;-webkit-text-fill-color:transparent;background-clip:text;text-decoration:none;">Vora</a>
        <a href="../index.html#quickstart">快速开始</a>
        <a href="../index.html#syntax">语法</a>
        <a href="index.html">文档</a>
        <a href="language.html" class="active">语言参考</a>
    </div>
</nav>

<main class="section">
{content_html}
</main>

<footer class="footer">
    <p>Vora · <a href="https://github.com/Vora-lang/Vora" target="_blank" rel="noopener">github.com/Vora-lang/Vora</a></p>
</footer>
</body>
</html>'''


# ── Main ───────────────────────────────────────────────────────────────────

def main():
    check_only = "--check" in sys.argv

    if not USER_GUIDE.exists():
        print(f"ERROR: {USER_GUIDE} not found", file=sys.stderr)
        sys.exit(1)

    if not WEBSITE_ROOT.exists():
        print(f"ERROR: Website root {WEBSITE_ROOT} not found", file=sys.stderr)
        sys.exit(1)

    md_text = USER_GUIDE.read_text(encoding="utf-8")

    # Remove the YAML-like header line (title) — already in page template
    # Remove the description blockquote
    lines = md_text.split("\n")
    content_lines = []
    skip_until_after_header = False
    found_first_section = False

    for line in lines:
        # Skip the title line and description blockquote before first ##
        if not found_first_section:
            if line.startswith("## "):
                found_first_section = True
                content_lines.append(line)
            continue
        content_lines.append(line)

    md_body = "\n".join(content_lines)
    content_html = md_to_html(md_body)
    page_html = build_page(content_html)

    if check_only:
        if OUTPUT_FILE.exists():
            existing = OUTPUT_FILE.read_text(encoding="utf-8")
            if existing == page_html:
                print("OK: language.html is up to date")
                sys.exit(0)
            else:
                print("OUTDATED: language.html needs update — run: python scripts/build-website-docs.py")
                sys.exit(1)
        else:
            print("MISSING: language.html does not exist — run: python scripts/build-website-docs.py")
            sys.exit(1)

    # Write output
    OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_FILE.write_text(page_html, encoding="utf-8")
    print(f"Generated {OUTPUT_FILE}")
    print(f"   Source: {USER_GUIDE}")


if __name__ == "__main__":
    main()
