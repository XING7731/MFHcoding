#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成 OLED 6x8 ASCII 字库 (F6x8[][6]) —— 5x7 点阵放在 6x8 单元中。

为什么用脚本生成:
    手抄 95 个字符的点阵极易出错, 用系统等宽字体栅格化后再做位打包,
    结果可复现、可审计(生成过程完全透明), 也便于替换字体重新生成。

输出: user/Src/oled_font.c
用法: python tools/gen_font.py
"""
import os
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("需要 Pillow: pip install pillow")
    sys.exit(1)

CELL_W, CELL_H = 5, 7          # 点阵区域(字符本体)
FONT_CANDIDATES = [
    ("C:/Windows/Fonts/consola.ttf", "Consolas"),
    ("C:/Windows/Fonts/lucon.ttf", "Lucida Console"),
    ("C:/Windows/Fonts/cour.ttf", "Courier New"),
    ("C:/Windows/Fonts/arial.ttf", "Arial"),
]
SIZES = [11, 10, 9, 8, 7]      # 从大到小挑第一个能装进 5x7 的字号


def pick_font():
    for path, name in FONT_CANDIDATES:
        if os.path.exists(path):
            return path, name
    raise SystemExit("没有找到可用字体, 请修改 FONT_CANDIDATES")


def glyph_bitmap(font, ch):
    """把单字符栅格化成 CELL_W x CELL_H 的 0/1 矩阵(居中裁剪/缩放)"""
    img = Image.new("L", (24, 24), 0)
    ImageDraw.Draw(img).text((4, 4), ch, fill=255, font=font)

    bbox = img.getbbox()
    if bbox is None:
        return [[0] * CELL_W for _ in range(CELL_H)]

    img = img.crop(bbox)                     # 去掉空白边
    w, h = img.size

    # 等比缩放到能放进 5x7, 不放大
    scale = min(CELL_W / w, CELL_H / h, 1.0)
    nw, nh = max(1, int(round(w * scale))), max(1, int(round(h * scale)))
    if (nw, nh) != (w, h):
        img = img.resize((nw, nh), Image.LANCZOS)

    # 阈值化
    img = img.point(lambda p: 255 if p >= 110 else 0)
    px = img.load()

    # 居中放入 5x7 单元
    mat = [[0] * CELL_W for _ in range(CELL_H)]
    ox = (CELL_W - nw) // 2
    oy = (CELL_H - nh) // 2
    for y in range(nh):
        for x in range(nw):
            if px[x, y]:
                mat[oy + y][ox + x] = 1
    return mat


def pack_columns(mat):
    """列优先打包: 每字节 = 一列, bit0 为最上一行(SSD1306 页格式)"""
    out = []
    for x in range(CELL_W):
        b = 0
        for y in range(CELL_H):
            if mat[y][x]:
                b |= (1 << y)
        out.append(b)
    return out


def main():
    path, name = pick_font()
    print("使用字体: %s (%s)" % (path, name))

    chosen = None
    for size in SIZES:
        f = ImageFont.truetype(path, size)
        ok = True
        for c in range(32, 127):
            img = Image.new("L", (24, 24), 0)
            ImageDraw.Draw(img).text((4, 4), chr(c), fill=255, font=f)
            bb = img.getbbox()
            if bb and (bb[2] - bb[0]) > 12 and (bb[3] - bb[1]) > 18:
                ok = False
                break
        chosen = (size, f)
        if ok:
            break

    size, font = chosen
    print("选用字号: %d px (栅格化后缩放进 5x7 单元)" % size)

    lines = []
    lines.append("/* ==========================================================================")
    lines.append(" * oled_font.c —— 6x8 ASCII 字库 (5x7 点阵 / 6x8 单元)")
    lines.append(" *")
    lines.append(" * 本文件由 tools/gen_font.py 自动生成, 请勿手工编辑。")
    lines.append(" * 生成字体: %s, 字号 %d px" % (name, size))
    lines.append(" * 覆盖 ASCII 32~126, 共 95 个字符; 每字符 6 字节(前 5 字节为点阵列,")
    lines.append(" * 第 6 字节为空列作为字符间距); 每字节 bit0 = 单元最上一行。")
    lines.append(" *")
    lines.append(" * 观感说明: 系统字体在小字号下栅格化的效果不如手工点阵字体精细。")
    lines.append(" * 若追求更好的显示效果, 可直接替换为 OLED 厂商例程自带的 6x8 字库")
    lines.append(" * (保持同名同格式即可)。")
    lines.append(" * ========================================================================== */")
    lines.append("")
    lines.append("const unsigned char F6x8[95][6] = {")

    for c in range(32, 127):
        cols = pack_columns(glyph_bitmap(font, chr(c)))
        ch = chr(c)
        label = "space" if ch == " " else ch
        lines.append("    {0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x00},  /* %02X '%s' */"
                     % (cols[0], cols[1], cols[2], cols[3], cols[4], c, label))

    lines.append("};")
    lines.append("")

    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "user", "Src")
    out_path = os.path.normpath(os.path.join(out_dir, "oled_font.c"))
    with open(out_path, "w", encoding="utf-8") as fp:
        fp.write("\n".join(lines))
    print("已生成: %s (%d 字符)" % (out_path, 95))


if __name__ == "__main__":
    main()
