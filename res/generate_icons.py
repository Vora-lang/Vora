#!/usr/bin/env python3
"""
Generate ICO and PNG icons from vora_logo.svg — pure Python (Pillow only).

No Cairo/rsvg needed. Manually renders the SVG paths since the design
is simple and known (V-shape + orbital ellipse + planet dot).

Requires: pip install Pillow

Output (in res/):
  vora.ico          — Windows icon (16, 32, 48, 256 px)
  vora.png          — 512px master PNG (also used for Linux packages)
  vora.iconset/     — macOS iconset directory
"""

import math
import struct
import xml.etree.ElementTree as ET
from io import BytesIO
from pathlib import Path

from PIL import Image, ImageDraw

RES_DIR = Path(__file__).resolve().parent
PROJECT_DIR = RES_DIR.parent
SVG_PATH = PROJECT_DIR / "vora_logo.svg"

# ── SVG parsing ────────────────────────────────────────────────────────


def parse_svg(svg_bytes: bytes):
    """Parse the vora_logo.svg into drawable elements."""
    root = ET.fromstring(svg_bytes)
    ns = "http://www.w3.org/2000/svg"
    viewbox = [float(x) for x in root.get("viewBox", "0 0 260 260").split()]
    vb_x, vb_y, vb_w, vb_h = viewbox

    elements = []
    for g in root.findall(f"{{{ns}}}g"):
        g_fill = g.get("fill", "none")
        g_stroke = g.get("stroke", "#111")
        g_stroke_width = float(g.get("stroke-width", "16"))
        g_linecap = g.get("stroke-linecap", "round")
        g_linejoin = g.get("stroke-linejoin", "round")

        for el in g.findall(f"{{{ns}}}path"):
            d = el.get("d", "")
            elements.append({
                "type": "path",
                "d": d,
                "fill": el.get("fill", g_fill),
                "stroke": el.get("stroke", g_stroke),
                "stroke_width": float(el.get("stroke-width", g_stroke_width)),
                "linecap": el.get("stroke-linecap", g_linecap),
                "linejoin": el.get("stroke-linejoin", g_linejoin),
            })

        for el in g.findall(f"{{{ns}}}ellipse"):
            cx = float(el.get("cx", 0))
            cy = float(el.get("cy", 0))
            rx = float(el.get("rx", 0))
            ry = float(el.get("ry", 0))
            transform = el.get("transform", "")
            elements.append({
                "type": "ellipse",
                "cx": cx, "cy": cy, "rx": rx, "ry": ry,
                "transform": transform,
                "fill": el.get("fill", g_fill),
                "stroke": el.get("stroke", g_stroke),
                "stroke_width": float(el.get("stroke-width", g_stroke_width)),
            })

        for el in g.findall(f"{{{ns}}}circle"):
            cx = float(el.get("cx", 0))
            cy = float(el.get("cy", 0))
            r = float(el.get("r", 0))
            elements.append({
                "type": "circle",
                "cx": cx, "cy": cy, "r": r,
                "fill": el.get("fill", g_fill),
                "stroke": el.get("stroke", g_stroke),
                "stroke_width": float(el.get("stroke-width", g_stroke_width)),
            })

    return viewbox, elements


def parse_path_d(d: str):
    """Parse SVG path data string into a list of commands."""
    commands = []
    current_cmd = ""
    current_nums = []

    d = d.replace(",", " ").strip()
    parts = d.split()

    i = 0
    while i < len(parts):
        token = parts[i]
        if token.isalpha() or (len(token) == 1 and not token.replace(".", "").replace("-", "").isdigit()):
            # It's a command letter (possibly with no space before it, like "M35")
            if len(token) == 1:
                if current_cmd:
                    commands.append((current_cmd, current_nums))
                current_cmd = token
                current_nums = []
                i += 1
            else:
                # Command letter glued to number, e.g. "M35"
                cmd = token[0]
                num_str = token[1:]
                if current_cmd:
                    commands.append((current_cmd, current_nums))
                current_cmd = cmd
                current_nums = [float(num_str)]
                i += 1
        else:
            current_nums.append(float(token))
            i += 1

    if current_cmd:
        commands.append((current_cmd, current_nums))

    return commands


# ── Rendering ──────────────────────────────────────────────────────────


def rotate_point(x, y, cx, cy, angle_rad):
    """Rotate point (x,y) around center (cx,cy) by angle_rad."""
    dx = x - cx
    dy = y - cy
    cos_a = math.cos(angle_rad)
    sin_a = math.sin(angle_rad)
    return cx + dx * cos_a - dy * sin_a, cy + dx * sin_a + dy * cos_a


def ellipse_points(cx, cy, rx, ry, angle_rad, num_points=64):
    """Generate polygon points for a rotated ellipse (stroke outline)."""
    pts = []
    for i in range(num_points):
        theta = 2.0 * math.pi * i / num_points
        ex = cx + rx * math.cos(theta)
        ey = cy + ry * math.sin(theta)
        rx_ex, rx_ey = rotate_point(ex, ey, cx, cy, angle_rad)
        pts.append((rx_ex, rx_ey))
    return pts


def render_svg(viewbox, elements, size, scale):
    """Render SVG elements onto a Pillow Image."""
    vb_x, vb_y, vb_w, vb_h = viewbox
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    for el in elements:
        sw = el["stroke_width"] * scale
        stroke = el["stroke"] if el["stroke"] != "none" else None
        fill = el["fill"] if el["fill"] != "none" else None

        if el["type"] == "path":
            commands = parse_path_d(el["d"])
            points = []
            for cmd, nums in commands:
                if cmd == "M":
                    pts = [(nums[j] * scale, nums[j + 1] * scale) for j in range(0, len(nums), 2)]
                    points.extend(pts)
                elif cmd == "L":
                    pts = [(nums[j] * scale, nums[j + 1] * scale) for j in range(0, len(nums), 2)]
                    points.extend(pts)

            if len(points) >= 2:
                draw.line(points, fill=stroke, width=max(1, int(sw + 0.5)), joint="curve")

        elif el["type"] == "ellipse":
            cx = el["cx"] * scale
            cy = el["cy"] * scale
            rx = el["rx"] * scale
            ry = el["ry"] * scale

            angle_rad = 0.0
            if el["transform"]:
                parts = el["transform"].replace("rotate(", "").replace(")", "").split()
                angle_rad = math.radians(float(parts[0]))
                # Rotation origin: stored in transform but ellipse is centered at (cx,cy)
                # SVG rotate(angle cx_rot cy_rot) rotates around (cx_rot, cy_rot)

            num_pts = max(24, int(2 * math.pi * max(rx, ry) / 2))
            num_pts = min(num_pts, 128)
            pts = ellipse_points(cx, cy, rx, ry, angle_rad, num_pts)
            draw.line(pts + [pts[0]], fill=stroke, width=max(1, int(sw + 0.5)), joint="curve")

        elif el["type"] == "circle":
            r = el["r"] * scale
            cx = el["cx"] * scale
            cy = el["cy"] * scale
            bbox = [cx - r, cy - r, cx + r, cy + r]
            if fill != "none":
                draw.ellipse(bbox, fill=fill, outline=stroke, width=max(1, int(sw + 0.5)))
            else:
                draw.ellipse(bbox, outline=stroke, width=max(1, int(sw + 0.5)))

    return img


def generate_png(size: int) -> bytes:
    """Render the SVG to a PNG at the given size."""
    svg_bytes = SVG_PATH.read_bytes()
    viewbox, elements = parse_svg(svg_bytes)
    scale = size / viewbox[2]  # fit to viewBox width

    # Add padding (10% on each side so strokes don't clip)
    pad_factor = 0.85
    padded_size = int(size / pad_factor)
    scale = size / viewbox[2] * pad_factor

    img = render_svg(viewbox, elements, padded_size, scale)

    # Crop back to target size (center crop)
    margin = (padded_size - size) // 2
    img = img.crop((margin, margin, margin + size, margin + size))

    buf = BytesIO()
    img.save(buf, format="PNG")
    return buf.getvalue()


# ── ICO generation ─────────────────────────────────────────────────────


def create_ico(png_images: dict) -> bytes:
    """Create .ico file from {size: png_bytes} dict."""
    sizes = sorted(png_images.keys())
    num_images = len(sizes)

    header = struct.pack("<HHH", 0, 1, num_images)

    dir_entries = []
    image_data = b""
    data_offset = 6 + num_images * 16

    for size in sizes:
        png_data = png_images[size]
        image_data += png_data

        display_size = 0 if size >= 256 else size
        entry = struct.pack(
            "<BBBBHHII",
            display_size, display_size,
            0, 0, 1, 32,
            len(png_data), data_offset,
        )
        dir_entries.append(entry)
        data_offset += len(png_data)

    return header + b"".join(dir_entries) + image_data


# ── Main ───────────────────────────────────────────────────────────────


def main():
    print(f"Rendering icons from: {SVG_PATH}")

    # ── Generate PNGs at all needed sizes ──────────────────────────────
    png_images = {}
    for size in (16, 32, 48, 64, 128, 256, 512):
        print(f"  {size}x{size}...", end=" ", flush=True)
        png_images[size] = generate_png(size)
        print("OK")

    # ── Master PNG (512px) ────────────────────────────────────────────
    (RES_DIR / "vora.png").write_bytes(png_images[512])
    print(f"  Saved: vora.png (512px)")

    # ── Windows ICO ───────────────────────────────────────────────────
    ico_sizes = {s: png_images[s] for s in (16, 32, 48, 256)}
    ico_data = create_ico(ico_sizes)
    ico_path = RES_DIR / "vora.ico"
    ico_path.write_bytes(ico_data)
    print(f"  Saved: vora.ico ({len(ico_data)} bytes, sizes: {sorted(ico_sizes.keys())})")

    # ── macOS iconset ─────────────────────────────────────────────────
    iconset = RES_DIR / "vora.iconset"
    iconset.mkdir(exist_ok=True)

    iconset_specs = {
        "icon_16x16.png": 16,
        "icon_16x16@2x.png": 32,
        "icon_32x32.png": 32,
        "icon_32x32@2x.png": 64,
        "icon_128x128.png": 128,
        "icon_128x128@2x.png": 256,
        "icon_256x256.png": 256,
        "icon_256x256@2x.png": 512,
        "icon_512x512.png": 512,
    }
    for filename, size in iconset_specs.items():
        (iconset / filename).write_bytes(png_images[size])
    print(f"  Created: vora.iconset/ ({len(iconset_specs)} files)")

    print("\nDone! Generated files in", RES_DIR)


if __name__ == "__main__":
    main()
