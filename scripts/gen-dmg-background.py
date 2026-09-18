#!/usr/bin/env python3
"""DMG「拖拽安装」布局的背景图生成器（纯标准库，macOS CI 运行器零依赖）。

用法：gen-dmg-background.py <输出.png> <宽> <高>

输出：浅色垂直渐变 + 中部向右箭头（提示把左侧 app 图标拖到右侧 Applications）。
图标坐标由 package-macos.sh 的 AppleScript 布置（app 左 ≈175px、Applications
右 ≈485px，垂直居中），箭头画在两者连线之间。
"""
import struct
import sys
import zlib


def lerp(a, b, t):
    return int(a + (b - a) * t)


def point_in_triangle(px, py, ax, ay, bx, by, cx, cy):
    # 重心法同号判定
    d1 = (px - bx) * (ay - by) - (ax - bx) * (py - by)
    d2 = (px - cx) * (by - cy) - (bx - cx) * (py - cy)
    d3 = (px - ax) * (cy - ay) - (cx - ax) * (py - ay)
    has_neg = d1 < 0 or d2 < 0 or d3 < 0
    has_pos = d1 > 0 or d2 > 0 or d3 > 0
    return not (has_neg and has_pos)


def arrow_at(x, y, w, h):
    """向右箭头：杆（矩形）+ 头（三角），位于两图标连线的垂直中带。"""
    cy = h // 2
    shaft_h = 20
    x0, x1 = int(w * 0.38), int(w * 0.58)     # 杆
    if x0 <= x <= x1 and abs(y - cy) <= shaft_h // 2:
        return True
    hx0, hx1 = x1, int(w * 0.66)              # 头（从杆端向右收拢）
    if hx0 <= x <= hx1:
        t = (x - hx0) / max(1, hx1 - hx0)
        half = int((shaft_h // 2 + 16) * (1 - t))
        if abs(y - cy) <= max(half, 1):
            return True
    return False


def write_png(path, w, h):
    top, bottom = (245, 246, 248), (223, 227, 232)   # 浅色渐变，不干扰图标文字
    arrow_color = (156, 160, 166)

    raw = bytearray()
    for y in range(h):
        t = y / max(1, h - 1)
        row = bytearray()
        for x in range(w):
            c = arrow_color if arrow_at(x, y, w, h) else (
                lerp(top[0], bottom[0], t),
                lerp(top[1], bottom[1], t),
                lerp(top[2], bottom[2], t))
            row += bytes(c)
        raw += b"\x00"                      # 每扫描行的过滤器类型 0（None）
        raw += row

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    if len(sys.argv) != 4:
        sys.exit(f"用法: {sys.argv[0]} <输出.png> <宽> <高>")
    out, w, h = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    write_png(out, w, h)
    print(f"已生成 {out} ({w}x{h})")


if __name__ == "__main__":
    main()
