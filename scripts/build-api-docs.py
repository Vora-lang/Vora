#!/usr/bin/env python3
"""
build-api-docs.py — Generate API reference from Doxygen comments in Vora headers.

Parses all .h files under src/, extracts /** ... */ and /// Doxygen comments,
class/struct/enum/function declarations, and generates a clean HTML API reference
matching the Vora website design.
"""

import os
import re
import sys
from pathlib import Path
from dataclasses import dataclass, field
from typing import Optional

REPO_ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = REPO_ROOT / "src"
WEBSITE_ROOT = REPO_ROOT.parent / "Vora-lang.github.io"
OUTPUT_DIR = WEBSITE_ROOT / "api"
OUTPUT_FILE = OUTPUT_DIR / "index.html"

# ── Parsing ────────────────────────────────────────────────────────────────────

@dataclass
class DocBlock:
    brief: str = ""
    details: str = ""
    params: list = field(default_factory=list)
    returns: str = ""
    notes: list = field(default_factory=list)
    see_also: list = field(default_factory=list)

@dataclass
class ApiEntry:
    name: str = ""
    kind: str = ""  # class, struct, enum, function, method, typedef, namespace
    signature: str = ""
    doc: DocBlock = field(default_factory=DocBlock)
    members: list = field(default_factory=list)
    file_path: str = ""

def parse_doxygen_block(text: str) -> DocBlock:
    """Parse a Doxygen comment block into structured documentation."""
    doc = DocBlock()
    text = re.sub(r'^/\*\*|\*/$', '', text).strip()
    lines = text.split('\n')

    # Strip leading * and whitespace
    cleaned = []
    for line in lines:
        line = line.strip()
        line = re.sub(r'^\*\s?', '', line)  # Remove leading *
        cleaned.append(line)

    text = '\n'.join(cleaned).strip()

    # Extract @brief
    m = re.search(r'@brief\s+(.+?)(?=\n\s*@|\n\s*\n|$)', text, re.DOTALL)
    if m:
        doc.brief = m.group(1).strip().replace('\n', ' ')

    # Extract @details
    m = re.search(r'@details\s+(.+?)(?=\n\s*@|\Z)', text, re.DOTALL)
    if m:
        doc.details = m.group(1).strip()

    # Extract @param
    for m in re.finditer(r'@param\s+(\S+)\s+(.+?)(?=\n\s*@|\n\s*\n|$)', text, re.DOTALL):
        doc.params.append((m.group(1), m.group(2).strip()))

    # Extract @tparam
    for m in re.finditer(r'@tparam\s+(\S+)\s+(.+?)(?=\n\s*@|\n\s*\n|$)', text, re.DOTALL):
        doc.params.append(('<' + m.group(1) + '>', m.group(2).strip()))

    # Extract @return / @returns
    m = re.search(r'@returns?\s+(.+?)(?=\n\s*@|\Z)', text, re.DOTALL)
    if m:
        doc.returns = m.group(1).strip()

    # Extract @note
    for m in re.finditer(r'@note\s+(.+?)(?=\n\s*@|\Z)', text, re.DOTALL):
        doc.notes.append(m.group(1).strip())

    # Extract @see
    for m in re.finditer(r'@see\s+(.+?)(?=\n\s*@|\n$|\Z)', text, re.DOTALL):
        doc.see_also.append(m.group(1).strip())

    # If no @brief, use the first line
    if not doc.brief:
        first_line = cleaned[0].strip() if cleaned else ""
        if first_line and not first_line.startswith('@'):
            doc.brief = first_line

    return doc

def extract_declaration(lines: list, start_idx: int) -> str:
    """Extract a C++ declaration starting from a given line."""
    decl = ""
    i = start_idx
    brace_depth = 0
    in_template = 0
    saw_semicolon = False

    while i < len(lines):
        line = lines[i]
        stripped = line.strip()

        decl += line + "\n"

        # Track braces for inline method bodies
        brace_depth += stripped.count('{') - stripped.count('}')
        in_template += stripped.count('<') - stripped.count('>')

        # End condition
        if brace_depth <= 0:
            if '{' in stripped and '}' not in stripped:
                continue
            # Check for end: ; or { at top level
            if stripped.endswith(';'):
                saw_semicolon = True
                break
            if stripped == '{' or (stripped.endswith('{') and not stripped.startswith('//')):
                # Method body — find matching }
                i += 1
                while i < len(lines) and brace_depth > 0:
                    l = lines[i]
                    decl += l + "\n"
                    brace_depth += l.count('{') - l.count('}')
                    i += 1
                break

        if saw_semicolon and not stripped:
            break

        i += 1
        if i - start_idx > 30:  # Safety limit
            break

    return decl.strip()

def parse_headers(src_dir: Path) -> list:
    """Parse all headers and extract API entries."""
    entries = []

    for h_file in sorted(src_dir.rglob("*.h")):
        rel_path = h_file.relative_to(src_dir).as_posix()

        try:
            content = h_file.read_text(encoding='utf-8', errors='replace')
        except Exception:
            continue

        lines = content.split('\n')

        # Extract @file block
        file_doc = DocBlock()
        file_text = content[:content.find('\n\n#pragma') if '\n\n#pragma' in content else content.find('\n\n#include')]
        m = re.search(r'/\*\*\s*\n\s*\*\s*@file.*?\*/\s*\n', file_text, re.DOTALL)
        if m:
            file_doc = parse_doxygen_block(m.group(0))

        if not file_doc.brief:
            m = re.search(r'@brief\s+(.+?)(?=\n\s*@|\n\s*\n|\Z)', content, re.DOTALL)
            if m:
                file_doc.brief = m.group(1).strip()

        i = 0
        while i < len(lines):
            line = lines[i].strip()

            # Skip preprocessor, includes, blank
            if not line or line.startswith('#') or line.startswith('//') and not line.startswith('///'):
                i += 1
                continue

            # Collect preceding Doxygen comment
            doc = DocBlock()
            if line.startswith('///') or line.startswith('/**'):
                doc_start = i
                if line.startswith('/**'):
                    doc_text = ""
                    while i < len(lines):
                        doc_text += lines[i] + "\n"
                        if '*/' in lines[i]:
                            i += 1
                            break
                        i += 1
                    doc = parse_doxygen_block(doc_text)
                else:
                    doc_text = ""
                    while i < len(lines) and lines[i].strip().startswith('///'):
                        doc_text += lines[i].strip()[3:].strip() + "\n"
                        i += 1
                    doc.brief = doc_text.strip()

                if i >= len(lines):
                    break
                line = lines[i].strip()

            # Detect declarations
            # class / struct
            m_cls = re.match(r'(?:template\s*<[^>]*>\s*)?(class|struct)\s+(\w+)', line)
            if m_cls:
                kind = m_cls.group(1)
                name = m_cls.group(2)

                # Check if it's a forward declaration (just ;)
                decl_text = extract_declaration(lines, i)
                if decl_text.endswith(';') and '{' not in decl_text.split(';')[0]:
                    entries.append(ApiEntry(name=name, kind=f"{kind} (forward)", signature=decl_text, doc=doc, file_path=rel_path))
                else:
                    # Check for base classes
                    m_inherit = re.search(r':\s*public\s+(.+)', decl_text)
                    bases = m_inherit.group(1).strip() if m_inherit else ""

                    members = []
                    # Scan for method declarations
                    j = i + 1
                    brace_depth = 1
                    while j < len(lines) and brace_depth > 0:
                        l = lines[j].strip()
                        brace_depth += l.count('{') - l.count('}')

                        # Check for Doxygen comment before method
                        method_doc = DocBlock()
                        if l.startswith('///'):
                            doc_text = ""
                            while j < len(lines) and lines[j].strip().startswith('///'):
                                doc_text += lines[j].strip()[3:].strip() + "\n"
                                j += 1
                            method_doc.brief = doc_text.strip()
                            if j < len(lines):
                                l = lines[j].strip()

                        m_method = re.match(r'(?:virtual\s+)?(?:static\s+)?(?:const\s+)?(?:explicit\s+)?[\w:<>,*&\s]+\s+(\w+)\s*\(', l)
                        if m_method and brace_depth == 1 and not l.startswith('//'):
                            method_name = m_method.group(1)
                            if method_name not in ('if', 'while', 'for', 'switch'):
                                members.append({'name': method_name, 'sig': l.strip() + ';', 'doc': method_doc})

                        j += 1

                    entries.append(ApiEntry(
                        name=name, kind=kind, signature=decl_text.split('\n')[0].rstrip('{').strip(),
                        doc=doc, members=members, file_path=rel_path
                    ))

                # Skip to after declaration
                if kind in ('class', 'struct') and '{' in decl_text:
                    brace_count = 1
                    while i < len(lines) and brace_count > 0:
                        brace_count += lines[i].count('{') - lines[i].count('}')
                        i += 1
                else:
                    i += 1
                continue

            # enum
            m_enum = re.match(r'(?:enum\s+class\s+|enum\s+)(\w+)', line)
            if m_enum:
                name = m_enum.group(1)
                entries.append(ApiEntry(name=name, kind='enum', signature=line, doc=doc, file_path=rel_path))

            # Function declaration (standalone, non-method)
            m_func = re.match(r'(?:inline\s+)?(?:static\s+)?(?:const\s+)?[\w:<>,*&\s]+\s+(\w+)\s*\(', line)
            if m_func and 'class' not in line and 'struct' not in line and 'namespace' not in line:
                name = m_func.group(1)
                if name not in ('if', 'while', 'for', 'switch', 'return'):
                    entries.append(ApiEntry(name=name, kind='function', signature=line.rstrip('{').strip(), doc=doc, file_path=rel_path))

            # namespace
            m_ns = re.match(r'namespace\s+(\w+)', line)
            if m_ns:
                entries.append(ApiEntry(name=m_ns.group(1), kind='namespace', signature=line, doc=DocBlock(), file_path=rel_path))

            i += 1

    return entries

# ── HTML Generation ────────────────────────────────────────────────────────────

def escape_html(text: str) -> str:
    return text.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;').replace('"', '&quot;')

def render_method_list(members: list) -> str:
    if not members:
        return ""
    html = '<div class="member-list">'
    for m in members:
        md = m.get('doc', DocBlock())
        html += f'<div class="member-item">'
        html += f'<code class="member-sig">{escape_html(m["sig"])}</code>'
        if md.brief:
            html += f'<span class="member-desc">{escape_html(md.brief)}</span>'
        html += '</div>'
    html += '</div>'
    return html

def render_param_table(params: list) -> str:
    if not params:
        return ""
    rows = ""
    for name, desc in params:
        rows += f'<tr><td class="param-name"><code>{escape_html(name)}</code></td><td>{escape_html(desc)}</td></tr>'
    return f'<table class="param-table"><thead><tr><th>Parameter</th><th>Description</th></tr></thead><tbody>{rows}</tbody></table>'

def render_entry(entry: ApiEntry, level: int = 2) -> str:
    """Render a single API entry as HTML."""
    d = entry.doc

    # Determine icon
    icons = {'class': '🧩', 'struct': '📦', 'enum': '🔢', 'function': '⚡', 'method': '▶️',
             'namespace': '📁', 'class (forward)': '📎', 'typedef': '🏷️'}
    icon = icons.get(entry.kind, '📄')

    kind_label = entry.kind.replace(' (forward)', ' (前置声明)')

    html = f'<div class="api-entry" id="{entry.name}">\n'
    html += f'  <h{level} class="entry-name">{icon} <code>{entry.name}</code> <span class="entry-kind">{kind_label}</span></h{level}>\n'

    # File path badge
    html += f'  <div class="entry-file">📁 <code>src/{escape_html(entry.file_path)}</code></div>\n'

    # Signature
    if entry.signature and entry.kind not in ('namespace',):
        # Strip overly long signatures
        sig = entry.signature
        if len(sig) > 300:
            sig = sig[:297] + '...'
        html += f'  <div class="entry-sig"><pre><code>{escape_html(sig)}</code></pre></div>\n'

    # Brief
    if d.brief:
        html += f'  <p class="entry-brief">{escape_html(d.brief)}</p>\n'

    # Details
    if d.details:
        detail_text = escape_html(d.details)
        # Convert \n\n to <br><br>
        detail_text = detail_text.replace('\n\n', '<br><br>').replace('\n', ' ')
        html += f'  <div class="entry-details"><p>{detail_text}</p></div>\n'

    # Parameters
    if d.params:
        html += f'  <h4>Parameters</h4>\n'
        html += render_param_table(d.params)

    # Return value
    if d.returns:
        html += f'  <p class="entry-returns"><strong>Returns:</strong> {escape_html(d.returns)}</p>\n'

    # Notes
    for note in d.notes:
        html += f'  <div class="note">{escape_html(note)}</div>\n'

    # See also
    if d.see_also:
        refs = ', '.join(f'<a href="#{r}"><code>{r}</code></a>' for r in d.see_also)
        html += f'  <p class="entry-see"><strong>See also:</strong> {refs}</p>\n'

    # Members (for classes)
    if entry.members:
        html += f'  <h4>Methods ({len(entry.members)})</h4>\n'
        html += render_method_list(entry.members)

    html += '</div>\n'
    return html

def group_by_file(entries: list) -> dict:
    """Group entries by source file for organized display."""
    groups = {}
    for e in entries:
        if e.file_path not in groups:
            groups[e.file_path] = []
        groups[e.file_path].append(e)
    return groups

def group_by_module(groups: dict) -> dict:
    """Group file groups by module directory."""
    modules = {}
    for file_path, entries in groups.items():
        module = file_path.split('/')[0] if '/' in file_path else 'root'
        if module not in modules:
            modules[module] = []
        modules[module].append((file_path, entries))
    return modules

# ── Page template ──────────────────────────────────────────────────────────────

def build_page(content_html: str) -> str:
    module_nav = ""
    modules_order = ['lexer', 'ast', 'parser', 'vm', 'runtime', 'gc', 'common', 'json_rpc', 'formatter', 'lsp']

    for mod in modules_order:
        display_name = {
            'lexer': 'Lexer 词法分析', 'ast': 'AST 抽象语法树', 'parser': 'Parser 语法分析',
            'vm': 'VM 虚拟机', 'runtime': 'Runtime 运行时', 'gc': 'GC 垃圾回收',
            'common': 'Common 通用', 'json_rpc': 'JSON-RPC 协议', 'formatter': 'Formatter 格式化',
            'lsp': 'LSP 语义分析'
        }.get(mod, mod)
        module_nav += f'<a href="#mod-{mod}">{display_name}</a>\n'

    return f'''<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1.0" />
<title>C++ API 参考 — Vora 文档</title>
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
    body {{ font-family: var(--sans); background: var(--bg); color: var(--text); line-height: 1.7; }}

    /* ── Nav ─────────────────────────────────────── */
    .nav {{ position: sticky; top: 0; z-index: 100; background: rgba(255,255,255,0.85); backdrop-filter: blur(12px); border-bottom: 1px solid var(--border); padding: 0 24px; }}
    .nav-inner {{ max-width: 1100px; margin: 0 auto; display: flex; align-items: center; gap: 24px; height: 52px; font-size: 0.9rem; font-weight: 500; overflow-x: auto; }}
    .nav a {{ color: var(--text2); text-decoration: none; transition: color 0.2s; white-space: nowrap; }}
    .nav a:hover {{ color: var(--kw); }}
    .nav .active {{ color: var(--kw); }}

    /* ── Layout ──────────────────────────────────── */
    .page {{ display: flex; max-width: 1100px; margin: 0 auto; }}
    .sidebar {{ width: 220px; flex-shrink: 0; position: sticky; top: 52px; height: calc(100vh - 52px); overflow-y: auto; padding: 24px 16px; border-right: 1px solid var(--border); }}
    .sidebar h3 {{ font-size: 0.8rem; text-transform: uppercase; letter-spacing: 0.08em; color: var(--text2); margin-bottom: 12px; }}
    .sidebar a {{ display: block; font-size: 0.85rem; color: var(--text2); text-decoration: none; padding: 4px 0; transition: color 0.2s; }}
    .sidebar a:hover {{ color: var(--kw); }}
    .main {{ flex: 1; padding: 48px 40px; min-width: 0; }}

    /* ── Typography ──────────────────────────────── */
    .main h1 {{ font-size: 2.2rem; font-weight: 800; margin-bottom: 8px; }}
    .main h2 {{ font-size: 1.4rem; font-weight: 700; margin: 48px 0 16px; padding-top: 24px; border-top: 1px solid var(--border); }}
    .main h3 {{ font-size: 1.1rem; font-weight: 600; margin: 32px 0 12px; }}
    .main h4 {{ font-size: 0.95rem; font-weight: 600; margin: 20px 0 8px; color: var(--text2); }}
    .section-desc {{ color: var(--text2); margin-bottom: 32px; font-size: 1.05rem; }}

    /* ── API entries ─────────────────────────────── */
    .api-entry {{ background: var(--card); border: 1px solid var(--border); border-radius: var(--radius); padding: 28px; margin-bottom: 24px; }}
    .entry-name code {{ font-family: var(--mono); color: var(--kw); font-size: 1.05em; }}
    .entry-kind {{ font-size: 0.75rem; color: var(--text2); background: #eee; padding: 2px 8px; border-radius: 20px; margin-left: 8px; }}
    .entry-file {{ font-size: 0.8rem; color: var(--text2); margin-bottom: 12px; }}
    .entry-file code {{ font-size: 0.85em; }}
    .entry-sig {{ background: var(--bg); border: 1px solid var(--border); border-radius: 6px; padding: 14px 18px; margin-bottom: 16px; overflow-x: auto; }}
    .entry-sig pre {{ margin: 0; }}
    .entry-sig code {{ font-family: var(--mono); font-size: 0.82rem; }}
    .entry-brief {{ font-size: 1.02rem; margin-bottom: 12px; }}
    .entry-details {{ font-size: 0.92rem; color: var(--text2); margin-bottom: 16px; }}
    .entry-returns {{ font-size: 0.92rem; margin-bottom: 8px; }}
    .entry-see {{ font-size: 0.88rem; color: var(--text2); }}
    .entry-see a {{ color: var(--kw); text-decoration: none; }}
    .entry-see a:hover {{ text-decoration: underline; }}

    /* ── Member list ─────────────────────────────── */
    .member-list {{ margin-top: 12px; }}
    .member-item {{ padding: 8px 0; border-bottom: 1px solid var(--border); display: flex; gap: 16px; align-items: baseline; }}
    .member-item:last-child {{ border-bottom: none; }}
    .member-sig {{ font-family: var(--mono); font-size: 0.8rem; color: var(--text); white-space: nowrap; }}
    .member-desc {{ font-size: 0.85rem; color: var(--text2); }}

    /* ── Tables ──────────────────────────────────── */
    .param-table {{ width: 100%; border-collapse: collapse; font-size: 0.88rem; margin: 12px 0; }}
    .param-table th {{ text-align: left; padding: 8px 12px; border-bottom: 2px solid var(--border); font-weight: 600; color: var(--text2); font-size: 0.78rem; text-transform: uppercase; }}
    .param-table td {{ padding: 8px 12px; border-bottom: 1px solid var(--border); }}
    .param-name code {{ font-family: var(--mono); color: var(--kw); font-size: 0.85em; }}

    /* ── Note ────────────────────────────────────── */
    .note {{ background: #fefce8; border: 1px solid #fde68a; border-radius: 8px; padding: 10px 14px; margin: 12px 0; font-size: 0.88rem; color: #92400e; }}

    /* ── Module headers ──────────────────────────── */
    .module-header {{ margin-top: 48px; }}
    .module-header h2 {{ border-top: 2px solid var(--kw); padding-top: 20px; }}

    code {{ font-family: var(--mono); font-size: 0.88em; }}
    p code, li code {{ background: #f3f0ff; color: var(--kw); padding: 2px 6px; border-radius: 4px; }}

    /* ── Footer ──────────────────────────────────── */
    .footer {{ border-top: 1px solid var(--border); padding: 40px 24px; text-align: center; color: var(--text2); font-size: 0.85rem; }}
    .footer a {{ color: var(--kw); text-decoration: none; }}
    .footer a:hover {{ text-decoration: underline; }}

    @media (max-width: 768px) {{
        .sidebar {{ display: none; }}
        .main {{ padding: 24px 16px; }}
        .main h1 {{ font-size: 1.6rem; }}
        .member-item {{ flex-direction: column; gap: 4px; }}
    }}
</style>
</head>
<body>

<nav class="nav">
    <div class="nav-inner">
        <a href="../index.html" style="font-size:1.2rem;font-weight:800;background:linear-gradient(135deg,#6366f1,#a78bfa);-webkit-background-clip:text;-webkit-text-fill-color:transparent;background-clip:text;text-decoration:none;">Vora</a>
        <a href="../index.html#quickstart">快速开始</a>
        <a href="../index.html#syntax">语法</a>
        <a href="../docs/index.html">文档</a>
        <a href="../docs/language.html">语言参考</a>
        <a href="../docs/user-guide.html">语言差异</a>
        <a href="index.html" class="active">API 参考</a>
    </div>
</nav>

<div class="page">
    <aside class="sidebar">
        <h3>模块</h3>
        {module_nav}
    </aside>

    <main class="main">
        <h1>📐 C++ API 参考</h1>
        <p class="section-desc">Vora v0.27.0 — 由 Doxygen 注释自动生成的 C++ 源码级 API 文档。<br>涵盖 Lexer、Parser、AST、Compiler、VM、Runtime、GC 等所有核心模块。</p>

        {content_html}
    </main>
</div>

<footer class="footer">
    <p>Vora · <a href="https://github.com/Vora-lang/Vora" target="_blank" rel="noopener">github.com/Vora-lang/Vora</a></p>
    <p style="margin-top:8px">Generated by build-api-docs.py from Doxygen comments in source headers.</p>
</footer>
</body>
</html>'''

# ── Main ────────────────────────────────────────────────────────────────────────

def main():
    print("Parsing Vora headers...")
    entries = parse_headers(SRC_DIR)

    print(f"Found {len(entries)} API entries")

    # Count by kind
    kinds = {}
    for e in entries:
        kinds[e.kind] = kinds.get(e.kind, 0) + 1
    for k, v in sorted(kinds.items()):
        print(f"  {k}: {v}")

    # Group by file
    by_file = group_by_file(entries)
    by_module = group_by_module(by_file)

    # Generate HTML
    content_parts = []

    modules_order = ['lexer', 'ast', 'parser', 'vm', 'runtime', 'gc', 'common', 'json_rpc', 'formatter', 'lsp']

    for mod in modules_order:
        if mod not in by_module:
            continue

        # Module header
        mod_names = {
            'lexer': 'Lexer — 词法分析', 'ast': 'AST — 抽象语法树', 'parser': 'Parser — 语法分析',
            'vm': 'VM — 字节码虚拟机', 'runtime': 'Runtime — 运行时系统',
            'gc': 'GC — 垃圾回收', 'common': 'Common — 通用工具',
            'json_rpc': 'JSON-RPC — 协议层', 'formatter': 'Formatter — 代码格式化',
            'lsp': 'LSP — 语义分析'
        }

        content_parts.append(f'<div class="module-header" id="mod-{mod}">')
        content_parts.append(f'<h2>{mod_names.get(mod, mod)}</h2>')
        content_parts.append(f'<p style="color:var(--text2);margin-bottom:24px"><code>src/{mod}/</code></p>')

        for file_path, file_entries in by_module[mod]:
            content_parts.append(f'<h3>{file_path}</h3>')
            for entry in file_entries:
                content_parts.append(render_entry(entry, level=3 if len(file_entries) > 5 else 3))

        content_parts.append('</div>')

    page_html = build_page('\n'.join(content_parts))

    # Write output
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    OUTPUT_FILE.write_text(page_html, encoding='utf-8')
    print(f"\nGenerated: {OUTPUT_FILE}")
    print(f"Size: {len(page_html):,} bytes")

if __name__ == "__main__":
    main()
