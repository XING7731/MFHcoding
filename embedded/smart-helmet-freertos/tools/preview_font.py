import re
import os
from PIL import Image

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.normpath(os.path.join(here, ".."))
src = open(os.path.join(root, "user", "Src", "oled_font.c"), encoding="utf-8").read()
rows = re.findall(r"\{0x([0-9A-F]{2}),0x([0-9A-F]{2}),0x([0-9A-F]{2}),0x([0-9A-F]{2}),0x([0-9A-F]{2}),0x00\}", src)
font = [[int(v, 16) for v in r] for r in rows]
print("glyphs:", len(font))

lines = ["2026-09-11T10:00:00Z", "HR:078 SpO2:--", "T:32.5C H:60.0%",
         "GAS:12 LIGHT:150", "A:0.1 0.2 -1.8", "FALL ALARM", "UP:00:12:34"]
W, H = 22 * 6, len(lines) * 8 + 6
img = Image.new("L", (W, H), 0)
px = img.load()
for li, line in enumerate(lines):
    for ci, ch in enumerate(line):
        if 32 <= ord(ch) <= 126:
            g = font[ord(ch) - 32]
            for col in range(5):
                for bit in range(7):
                    if g[col] & (1 << bit):
                        px[ci * 6 + col, li * 8 + bit] = 255
img = img.resize((W * 3, H * 3), Image.NEAREST)
out = os.path.join(here, "oled_preview.png")
img.save(out)
print("saved", out, img.size)
