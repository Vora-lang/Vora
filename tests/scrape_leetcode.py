#!/usr/bin/env python3
"""
LeetCode Top 100 爬虫 (v2 — HTML page scraping)
从 leetcode.cn 爬取热题 100 的题目描述，提取内容保存到 markdown 文件。

用法：
  pip install requests beautifulsoup4
  python tests/scrape_leetcode.py
"""

import re
import time
import os
import json
import urllib.request
import urllib.error
import ssl


# The 100 problem slugs
PROBLEMS = [
    ("1", "two-sum", "两数之和", "简单"),
    ("49", "group-anagrams", "字母异位词分组", "中等"),
    ("128", "longest-consecutive-sequence", "最长连续序列", "中等"),
    ("283", "move-zeroes", "移动零", "简单"),
    ("11", "container-with-most-water", "盛最多水的容器", "中等"),
    ("15", "3sum", "三数之和", "中等"),
    ("42", "trapping-rain-water", "接雨水", "困难"),
    ("3", "longest-substring-without-repeating-characters", "无重复字符的最长子串", "中等"),
    ("438", "find-all-anagrams-in-a-string", "找到字符串中所有字母异位词", "中等"),
    ("560", "subarray-sum-equals-k", "和为 K 的子数组", "中等"),
    ("239", "sliding-window-maximum", "滑动窗口最大值", "困难"),
    ("76", "minimum-window-substring", "最小覆盖子串", "困难"),
    ("53", "maximum-subarray", "最大子数组和", "中等"),
    ("56", "merge-intervals", "合并区间", "中等"),
    ("189", "rotate-array", "轮转数组", "中等"),
    ("238", "product-of-array-except-self", "除自身以外数组的乘积", "中等"),
    ("41", "first-missing-positive", "缺失的第一个正数", "困难"),
    ("73", "set-matrix-zeroes", "矩阵置零", "中等"),
    ("54", "spiral-matrix", "螺旋矩阵", "中等"),
    ("48", "rotate-image", "旋转图像", "中等"),
    ("240", "search-a-2d-matrix-ii", "搜索二维矩阵 II", "中等"),
    ("160", "intersection-of-two-linked-lists", "相交链表", "简单"),
    ("206", "reverse-linked-list", "反转链表", "简单"),
    ("234", "palindrome-linked-list", "回文链表", "简单"),
    ("141", "linked-list-cycle", "环形链表", "简单"),
    ("142", "linked-list-cycle-ii", "环形链表 II", "中等"),
    ("21", "merge-two-sorted-lists", "合并两个有序链表", "简单"),
    ("2", "add-two-numbers", "两数相加", "中等"),
    ("19", "remove-nth-node-from-end-of-list", "删除链表的倒数第N个结点", "中等"),
    ("24", "swap-nodes-in-pairs", "两两交换链表中的节点", "中等"),
    ("25", "reverse-nodes-in-k-group", "K个一组翻转链表", "困难"),
    ("138", "copy-list-with-random-pointer", "随机链表的复制", "中等"),
    ("148", "sort-list", "排序链表", "中等"),
    ("23", "merge-k-sorted-lists", "合并K个升序链表", "困难"),
    ("146", "lru-cache", "LRU缓存", "中等"),
    ("94", "binary-tree-inorder-traversal", "二叉树的中序遍历", "简单"),
    ("104", "maximum-depth-of-binary-tree", "二叉树的最大深度", "简单"),
    ("226", "invert-binary-tree", "翻转二叉树", "简单"),
    ("101", "symmetric-tree", "对称二叉树", "简单"),
    ("543", "diameter-of-binary-tree", "二叉树的直径", "简单"),
    ("102", "binary-tree-level-order-traversal", "二叉树的层序遍历", "中等"),
    ("108", "convert-sorted-array-to-binary-search-tree", "将有序数组转换为二叉搜索树", "简单"),
    ("98", "validate-binary-search-tree", "验证二叉搜索树", "中等"),
    ("230", "kth-smallest-element-in-a-bst", "二叉搜索树中第K小的元素", "中等"),
    ("199", "binary-tree-right-side-view", "二叉树的右视图", "中等"),
    ("114", "flatten-binary-tree-to-linked-list", "二叉树展开为链表", "中等"),
    ("105", "construct-binary-tree-from-preorder-and-inorder-traversal", "从前序与中序遍历序列构造二叉树", "中等"),
    ("437", "path-sum-iii", "路径总和 III", "中等"),
    ("236", "lowest-common-ancestor-of-a-binary-tree", "二叉树的最近公共祖先", "中等"),
    ("124", "binary-tree-maximum-path-sum", "二叉树中的最大路径和", "困难"),
    ("200", "number-of-islands", "岛屿数量", "中等"),
    ("994", "rotting-oranges", "腐烂的橘子", "中等"),
    ("207", "course-schedule", "课程表", "中等"),
    ("208", "implement-trie-prefix-tree", "实现 Trie (前缀树)", "中等"),
    ("46", "permutations", "全排列", "中等"),
    ("78", "subsets", "子集", "中等"),
    ("17", "letter-combinations-of-a-phone-number", "电话号码的字母组合", "中等"),
    ("39", "combination-sum", "组合总和", "中等"),
    ("22", "generate-parentheses", "括号生成", "中等"),
    ("79", "word-search", "单词搜索", "中等"),
    ("131", "palindrome-partitioning", "分割回文串", "中等"),
    ("51", "n-queens", "N皇后", "困难"),
    ("35", "search-insert-position", "搜索插入位置", "简单"),
    ("74", "search-a-2d-matrix", "搜索二维矩阵", "中等"),
    ("34", "find-first-and-last-position-of-element-in-sorted-array", "在排序数组中查找元素的第一个和最后一个位置", "中等"),
    ("33", "search-in-rotated-sorted-array", "搜索旋转排序数组", "中等"),
    ("153", "find-minimum-in-rotated-sorted-array", "寻找旋转排序数组中的最小值", "中等"),
    ("4", "median-of-two-sorted-arrays", "寻找两个正序数组的中位数", "困难"),
    ("20", "valid-parentheses", "有效的括号", "简单"),
    ("155", "min-stack", "最小栈", "中等"),
    ("394", "decode-string", "字符串解码", "中等"),
    ("739", "daily-temperatures", "每日温度", "中等"),
    ("84", "largest-rectangle-in-histogram", "柱状图中最大的矩形", "困难"),
    ("215", "kth-largest-element-in-an-array", "数组中的第K个最大元素", "中等"),
    ("347", "top-k-frequent-elements", "前 K 个高频元素", "中等"),
    ("295", "find-median-from-data-stream", "数据流的中位数", "困难"),
    ("121", "best-time-to-buy-and-sell-stock", "买卖股票的最佳时机", "简单"),
    ("55", "jump-game", "跳跃游戏", "中等"),
    ("45", "jump-game-ii", "跳跃游戏 II", "中等"),
    ("763", "partition-labels", "划分字母区间", "中等"),
    ("70", "climbing-stairs", "爬楼梯", "简单"),
    ("118", "pascals-triangle", "杨辉三角", "简单"),
    ("198", "house-robber", "打家劫舍", "中等"),
    ("279", "perfect-squares", "完全平方数", "中等"),
    ("322", "coin-change", "零钱兑换", "中等"),
    ("139", "word-break", "单词拆分", "中等"),
    ("300", "longest-increasing-subsequence", "最长递增子序列", "中等"),
    ("152", "maximum-product-subarray", "乘积最大子数组", "中等"),
    ("416", "partition-equal-subset-sum", "分割等和子集", "中等"),
    ("32", "longest-valid-parentheses", "最长有效括号", "困难"),
    ("62", "unique-paths", "不同路径", "中等"),
    ("64", "minimum-path-sum", "最小路径和", "中等"),
    ("5", "longest-palindromic-substring", "最长回文子串", "中等"),
    ("1143", "longest-common-subsequence", "最长公共子序列", "中等"),
    ("72", "edit-distance", "编辑距离", "困难"),
    ("136", "single-number", "只出现一次的数字", "简单"),
    ("169", "majority-element", "多数元素", "简单"),
    ("75", "sort-colors", "颜色分类", "中等"),
    ("31", "next-permutation", "下一个排列", "中等"),
    ("287", "find-the-duplicate-number", "寻找重复数", "中等"),
]

UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36"


def fetch_page(slug: str) -> str | None:
    """Fetch the problem page HTML."""
    url = f"https://leetcode.cn/problems/{slug}/"
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    try:
        ctx = ssl.create_default_context()
        with urllib.request.urlopen(req, timeout=30, context=ctx) as resp:
            return resp.read().decode("utf-8", errors="replace")
    except Exception as e:
        print(f"  ERROR: {e}")
        return None


def extract_meta_description(html: str) -> str | None:
    """Extract problem description from <meta name='description'> tag."""
    m = re.search(
        r'<meta\s+name="description"\s+content="([^"]*(?:"[^"]*"[^"]*)*?)"\s*/?\s*>',
        html, re.DOTALL
    )
    if not m:
        # Try with single quotes
        m = re.search(
            r"<meta\s+name='description'\s+content='([^']*)'\s*/?\s*>",
            html, re.DOTALL
        )
    if not m:
        return None
    content = m.group(1)
    # Unescape HTML entities
    content = content.replace("&lt;", "<")
    content = content.replace("&gt;", ">")
    content = content.replace("&amp;", "&")
    content = content.replace("&quot;", '"')
    content = content.replace("&#x27;", "'")
    content = content.replace("&nbsp;", " ")
    return content


def extract_next_data(html: str) -> dict | None:
    """Extract __NEXT_DATA__ JSON from page."""
    m = re.search(r'<script id="__NEXT_DATA__"[^>]*>(.*?)</script>', html, re.DOTALL)
    if not m:
        return None
    try:
        return json.loads(m.group(1))
    except json.JSONDecodeError:
        return None


def extract_question_html(html: str) -> str | None:
    """Extract the full translated HTML content from __NEXT_DATA__."""
    data = extract_next_data(html)
    if not data:
        return None
    try:
        props = data["props"]["pageProps"]["dehydratedState"]["queries"]
        for query in props:
            qdata = query.get("state", {}).get("data", {})
            if not qdata:
                continue
            question = qdata.get("question", {})
            if question:
                # Prefer Chinese translation
                content = question.get("translatedContent") or question.get("content")
                if content:
                    return content
    except (KeyError, TypeError):
        pass
    return None


def html_to_markdown(html: str) -> str:
    """Convert LeetCode's HTML problem description to markdown."""
    if not html:
        return ""
    from bs4 import BeautifulSoup
    soup = BeautifulSoup(html, "html.parser")

    # Remove style/script tags
    for tag in soup(["style", "script"]):
        tag.decompose()

    # Convert <sup> to ^ notation, <sub> to _ notation
    for tag in soup.find_all("sup"):
        tag.replace_with(f"^{tag.get_text()}")
    for tag in soup.find_all("sub"):
        tag.replace_with(f"_{tag.get_text()}")

    # Remove spacer <img> tags (LeetCode uses these as layout spacers)
    for img in soup.find_all("img"):
        src = img.get("src", "")
        if not src or "spacer" in src.lower() or "blank" in src.lower():
            img.decompose()

    lines = []
    for el in soup.children:
        text = _element_to_text(el)
        if text is not None:
            t = text.strip()
            if t:
                lines.append(t)
    result = "\n\n".join(lines)

    # Collapse 3+ consecutive newlines
    import re
    result = re.sub(r'\n{3,}', '\n\n', result)
    # Remove trailing whitespace on each line
    result = '\n'.join(line.rstrip() for line in result.split('\n'))

    return result


def _element_to_text(el) -> str | None:
    """Recursively convert BeautifulSoup element to markdown text.
    Returns None for elements that should be skipped entirely."""
    from bs4 import NavigableString, Tag

    if isinstance(el, NavigableString):
        s = str(el)
        # Skip pure whitespace NavigableStrings
        if s.strip() == "":
            return "" if s else None
        return s

    if not isinstance(el, Tag):
        return ""

    tag_name = el.name.lower() if el.name else ""

    # Collect inner text from children
    inner_parts = []
    for child in el.children:
        t = _element_to_text(child)
        if t is not None and t != "":
            inner_parts.append(t)
        elif t == "":
            inner_parts.append("")
    inner = "".join(inner_parts)

    if tag_name == "p":
        s = inner.strip()
        if not s:
            return None  # skip empty paragraphs (&nbsp; etc.)
        return s + "\n"
    elif tag_name == "br":
        return "\n"
    elif tag_name == "pre":
        # LeetCode <pre> blocks are example I/O with inline formatting (strong, code).
        # Don't wrap in ``` ``` — render each line with preserved inline markup.
        content = inner.strip()
        if not content:
            return None
        # Add > blockquote prefix to each line for visual distinction
        formatted = "\n".join(f"> {line}" for line in content.split("\n"))
        return f"\n{formatted}\n"
    elif tag_name == "code":
        if el.parent and el.parent.name == "pre":
            return inner
        return f"`{inner}`"
    elif tag_name == "strong":
        # Check if this is an example label (class="example")
        cls = el.get("class", [])
        if "example" in cls:
            return f"\n**{inner}**"
        return f"**{inner}**"
    elif tag_name == "em":
        return f"*{inner}*"
    elif tag_name in ("ul", "ol"):
        result = ""
        for li in el.find_all("li", recursive=False):
            li_text = _element_to_text(li)
            if li_text is not None and li_text.strip():
                result += f"- {li_text.strip()}\n"
        return f"\n{result}" if result else None
    elif tag_name == "li":
        return inner.strip()
    elif tag_name == "img":
        src = el.get("src", "")
        if src:
            return f"![img]({src})"
        return None
    elif tag_name == "span":
        return inner
    elif tag_name == "div":
        return f"\n{inner}\n" if inner.strip() else None
    else:
        return inner


def clean_meta_text(text: str) -> str:
    """Clean and format the meta description text."""
    if not text:
        return ""

    # The meta description has the format:
    # "题号. 题目 - 题目描述..."
    # Remove the prefix "题号. 题目 - "
    first_dash = text.find(" - ")
    if first_dash > 0 and first_dash < 200:
        text = text[first_dash + 3:]

    # Split on sentence boundaries for readability
    # Replace multiple spaces
    text = re.sub(r'\s+', ' ', text)

    # Format examples and code blocks
    lines = text.split("。")
    result = []
    for line in lines:
        line = line.strip()
        if not line:
            continue
        # Detect input/output patterns
        if "输入：" in line or "输出：" in line or "解释：" in line:
            result.append(f"  {line}")
        elif "示例" in line:
            result.append(f"\n**{line}**")
        elif "提示" in line or "进阶" in line or "注意" in line:
            result.append(f"\n> **{line}**")
        else:
            result.append(line)
    return "\n".join(result)


def main():
    output_path = os.path.join(os.path.dirname(__file__), "leetcode-top-100.md")
    success = 0
    failed = []

    with open(output_path, "w", encoding="utf-8") as f:
        f.write("# LeetCode 热题 100 (Top 100 Liked) — 题目内容\n\n")
        f.write("> 来源：https://leetcode.cn/studyplan/top-100-liked/\n")
        f.write(f"> 爬取日期：{time.strftime('%Y-%m-%d')}\n")
        f.write("> 难度统计：简单 20 | 中等 64 | 困难 16\n\n")
        f.write("---\n\n")

        for idx, (num, slug, title_cn, difficulty) in enumerate(PROBLEMS, 1):
            print(f"[{idx}/100] Fetching #{num} {title_cn} ({slug})...", end=" ", flush=True)

            html = fetch_page(slug)
            if not html:
                print("FAIL (fetch)")
                failed.append((num, slug, title_cn, "fetch error"))
                time.sleep(1)
                continue

            # Try __NEXT_DATA__ first (rich HTML), fall back to meta description (plain text)
            content_html = extract_question_html(html)

            if content_html:
                md = html_to_markdown(content_html)
            else:
                # Fallback: meta description
                desc = extract_meta_description(html)
                if desc:
                    md = clean_meta_text(desc)
                else:
                    print("FAIL (no content)")
                    failed.append((num, slug, title_cn, "no content"))
                    time.sleep(1)
                    continue

            # Write to file
            emoji = {"简单": "\U0001F7E2", "中等": "\U0001F7E1", "困难": "\U0001F534"}.get(difficulty, "⚪")
            f.write(f"## {idx}. {emoji} {title_cn}\n\n")
            f.write(f"- **题号**：#{num}\n")
            f.write(f"- **难度**：{difficulty}\n")
            f.write(f"- **Slug**：`{slug}`\n")
            f.write(f"- **链接**：https://leetcode.cn/problems/{slug}/?envType=study-plan-v2&envId=top-100-liked\n\n")
            f.write(md)
            f.write("\n\n---\n\n")
            f.flush()

            print("OK")
            success += 1
            time.sleep(0.8)  # Rate limit

    print(f"\n{'='*60}")
    print(f"Done! {success}/100 problems fetched successfully.")
    if failed:
        print(f"Failed ({len(failed)}):")
        for num, slug, title, reason in failed:
            print(f"  #{num} {title} ({slug}) — {reason}")
    print(f"Output: {output_path}")


if __name__ == "__main__":
    main()
