#!/usr/bin/env python3
"""Convert a splash JPG to a flash-resident RGB565 C source for LVGL.

Reads the asset read-only. Writes derived files only.
Usage: python tools/jpg_to_rgb565.py <input.jpg> <symbol> [width height]
"""
import sys
from PIL import Image

def main():
    src = sys.argv[1]
    sym = sys.argv[2] if len(sys.argv) > 2 else "splash_img"
    w = int(sys.argv[3]) if len(sys.argv) > 3 else 320
    h = int(sys.argv[4]) if len(sys.argv) > 4 else 240
    img = Image.open(src).convert("RGB")
    # Center-crop to target aspect, then resize.
    target = w / h
    iw, ih = img.size
    if iw / ih > target:
        nw = int(ih * target)
        x0 = (iw - nw) // 2
        img = img.crop((x0, 0, x0 + nw, ih))
    else:
        nh = int(iw / target)
        y0 = (ih - nh) // 2
        img = img.crop((0, y0, iw, y0 + nh))
    img = img.resize((w, h), Image.LANCZOS)
    px = list(img.getdata())
    with open(f"src/{sym}.c", "w") as f:
        f.write('#include "lvgl.h"\n\n')
        f.write(f"const uint8_t {sym}_map[] = {{\n")
        for i, (r, g, b) in enumerate(px):
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            f.write(f"0x{v & 0xFF:02X}, 0x{(v >> 8) & 0xFF:02X}, ")
            if i % 8 == 7:
                f.write("\n")
        f.write("};\n\n")
        f.write(f"const lv_img_dsc_t {sym} = {{\n")
        f.write("  .header.cf = LV_IMG_CF_TRUE_COLOR,\n")
        f.write("  .header.always_zero = 0,\n")
        f.write("  .header.reserved = 0,\n")
        f.write(f"  .header.w = {w},\n")
        f.write(f"  .header.h = {h},\n")
        f.write(f"  .data_size = {w * h * 2},\n")
        f.write(f"  .data = {sym}_map,\n")
        f.write("};\n")
    with open(f"src/{sym}.h", "w") as f:
        f.write("#pragma once\n#include \"lvgl.h\"\n\n")
        f.write(f"extern const lv_img_dsc_t {sym};\n")
    print(f"wrote src/{sym}.c/.h {w}x{h} RGB565")

main()
