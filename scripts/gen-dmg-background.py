#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""DMG 拖拽安装背景图生成器（本地工具——产物已提交仓库，CI 不运行本脚本）。

用法: python3 gen-dmg-background.py <输出.png>

设计（2026-09-19 真机反馈迭代版，参考 IINA/主流拖拽布局）:
  - @2x 1320×800 + 144dpi（pHYs）——Finder 按逻辑 660×400 显示，Retina 清晰
  - 图标中心 (185,190)/(475,190)（逻辑坐标，与 package-macos.sh 的 AppleScript
    position 对齐；Finder position = 图标视觉中心，真机截图校准）
  - 箭头与图标中心线对齐（旧版箭头画在 h/2 而图标在 y=140，错位 60px），
    总宽 96px（旧版 188px 过大）
  - 箭头下方一行中文提示「拖动图标到应用以完成安装」（PingFang SC）

依赖: Pillow + 中文字体（按序探测 PingFang SC / Noto Sans CJK SC）。
改设计后本地重跑生成 scripts/dmg-background.png 并提交，同步更新
tests/test_dmgbg.cpp 的几何断言（该测试断言的是已提交资产，即 CI 实际打包的文件）。
"""
import sys

from PIL import Image, ImageDraw, ImageFont

# —— 几何常量（全部 @2x 资产坐标 = 逻辑坐标 × 2）——
W, H = 1320, 800          # 资素尺寸（Finder 显示为 660×400）
ARROW_Y = 380             # 箭头中心线 = 图标中心 y（190 逻辑 × 2）
SHAFT_X0, SHAFT_X1 = 564, 704   # 箭杆（70 逻辑长）
SHAFT_HALF = 6            # 箭杆半厚（杆高 6 逻辑）
HEAD_X0, HEAD_X1 = 704, 756     # 箭头三角形（26 逻辑长）
HEAD_HALF = 26            # 三角形半高（26 逻辑）
TEXT = "拖动图标到应用以完成安装"
TEXT_CENTER = (660, 636)  # 提示文字中心（318 逻辑；图标标签下缘 ~280 逻辑）
TEXT_SIZE = 30            # 15 逻辑 pt
SS = 4                    # 超采样倍数（PIL 多边形无原生抗锯齿）

GRAD_TOP = (246, 247, 249)      # 近白微渐变（中性灰，不与图标抢视觉）
GRAD_BOTTOM = (237, 239, 242)
ARROW_COLOR = (150, 154, 161)
TEXT_COLOR = (99, 103, 110)

FONT_CANDIDATES = [
    "/usr/local/share/fonts/macos/PingFangSC-Regular.otf",   # 本地拷贝的 macOS 苹方
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf",
]


def load_font(px_size: int) -> ImageFont.FreeTypeFont:
    for path in FONT_CANDIDATES:
        try:
            return ImageFont.truetype(path, px_size)
        except OSError:
            continue
    sys.exit("错误: 找不到中文字体（试过 " + ", ".join(FONT_CANDIDATES) + "）")


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit(f"用法: {sys.argv[0]} <输出.png>")
    out = sys.argv[1]

    # 4x 超采样画布：形状（箭杆/箭头）经 LANCZOS 缩小获得抗锯齿
    sw, sh = W * SS, H * SS
    img = Image.new("RGB", (sw, sh))
    draw = ImageDraw.Draw(img)

    # 垂直渐变
    for y in range(sh):
        t = y / (sh - 1)
        color = tuple(round(a + (b - a) * t) for a, b in zip(GRAD_TOP, GRAD_BOTTOM))
        draw.line([(0, y), (sw, y)], fill=color)

    # 箭头（右指：md-editor → Applications）
    sy = ARROW_Y * SS
    draw.rectangle(
        [(SHAFT_X0 * SS, (ARROW_Y - SHAFT_HALF) * SS),
         (SHAFT_X1 * SS, (ARROW_Y + SHAFT_HALF) * SS)],
        fill=ARROW_COLOR)
    draw.polygon(
        [(HEAD_X0 * SS, (ARROW_Y - HEAD_HALF) * SS),
         (HEAD_X1 * SS, sy),
         (HEAD_X0 * SS, (ARROW_Y + HEAD_HALF) * SS)],
        fill=ARROW_COLOR)

    # 中文提示（字体渲染自带抗锯齿，超采样尺寸下直接画）
    font = load_font(TEXT_SIZE * SS)
    draw.text((TEXT_CENTER[0] * SS, TEXT_CENTER[1] * SS), TEXT,
              font=font, fill=TEXT_COLOR, anchor="mm")

    img = img.resize((W, H), Image.LANCZOS)
    # dpi=144 → pHYs 5669 px/m：Finder 据此把 1320×800 按 660×400 逻辑尺寸显示
    img.save(out, "PNG", dpi=(144, 144), optimize=True)
    print(f"已生成: {out} ({W}x{H} @144dpi)")


if __name__ == "__main__":
    main()
