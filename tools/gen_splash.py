#!/usr/bin/env python3
"""Draws the splash mascot (a platypus with a radio) and writes it as an LVGL
RGB565A8 image, src/ui/splash_img.c. Shapes only, no fonts, supersampled 4x.

    python3 tools/gen_splash.py [--png preview.png]

Also writes src/ui/splash_img_mono.c, 1-bit line art for e-paper.
"""
import argparse
import os
from PIL import Image, ImageDraw, ImageFilter

W, H, S = 240, 132, 4          # output size, supersampling factor
OX = 40                         # the platypus sits right of the bubble
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

FUR = (139, 90, 60)
FUR_DARK = (104, 64, 40)
BELLY = (214, 167, 118)
BILL = (66, 78, 96)
BILL_HI = (98, 112, 134)
TAIL = (92, 56, 36)
TAIL_LINE = (70, 42, 26)
BUBBLE = (250, 246, 236)


def draw(scale=1):
    """The colour mascot at `scale` times the splash size (whole numbers)."""
    S = globals()["S"] * scale
    im = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    # Platypus coordinates are drawn shifted right by OX; the bubble's are not.
    def sh(v):
        return [(c + OX if i % 2 == 0 else c) * S for i, c in enumerate(v)]
    def e(box, fill, outline=None, width=0):
        d.ellipse(sh(box), fill=fill, outline=outline, width=width * S)
    def rr(box, r, fill):
        d.rounded_rectangle(sh(box), r * S, fill=fill)
    def line(pts, fill, width):
        d.line(sh(pts), fill=fill, width=width * S, joint="curve")

    # Speech bubble, top left, its tail running down into the tip of the bill
    # (drawn over it, so the words come out of the mouth). The splash puts
    # "CQ CQ" in it with an LVGL label, so no font is baked into the image.
    d.polygon([v * S for v in (20, 30, 36, 30, 41, 78)], fill=BUBBLE)
    d.rounded_rectangle([v * S for v in (0, 2, 88, 34)], 14 * S, fill=BUBBLE)

    # Tail: a flat paddle behind the body, with a little crosshatch.
    e((118, 70, 196, 112), TAIL)
    for x in range(132, 190, 10):
        line((x, 76, x - 8, 106), TAIL_LINE + (150,), 2)
    # Body and belly.
    e((44, 52, 156, 122), FUR)
    e((62, 80, 140, 120), BELLY)
    # Feet.
    for fx in (66, 112):
        e((fx, 112, fx + 22, 126), FUR_DARK)
        for t in range(3):
            e((fx + 2 + t * 7, 120, fx + 8 + t * 7, 128), FUR_DARK)
    # Head.
    e((38, 30, 112, 96), FUR)
    # Duck bill, pointing left.
    # A fur cheek under the bill's root so it grows out of the face.
    e((44, 60, 72, 86), FUR)
    rr((2, 60, 58, 86), 13, BILL)
    rr((6, 62, 50, 71), 7, BILL_HI)
    line((10, 79, 48, 79), (48, 58, 74), 2)
    e((14, 66, 19, 70), (40, 46, 58))
    e((26, 66, 31, 70), (40, 46, 58))
    # Eyes: big, with highlights, just above and behind the bill's root.
    for ex in (46, 66):
        e((ex, 47, ex + 13, 62), (24, 20, 22))
        e((ex + 3, 49, ex + 8, 54), (255, 255, 255))
        e((ex + 8, 56, ex + 10, 58), (255, 255, 255))

    im = im.resize((W * scale, H * scale), Image.LANCZOS)
    return im


def draw_mono(scale=1.0):
    """Line-art version for 1-bit e-paper: white fills with black outlines, the
    bill, feet and eyes solid black. Returns an "L" image, 0 black, 255 white,
    at `scale` times the splash size."""
    im = Image.new("L", (W * S, H * S), 255)
    d = ImageDraw.Draw(im)
    K, Wh = 0, 255
    T = S / scale   # strokes keep their pixel width when shrunk
    def sh(v):
        return [(c + OX if i % 2 == 0 else c) * S for i, c in enumerate(v)]
    def e(box, fill=Wh, width=2):
        d.ellipse(sh(box), fill=fill, outline=K, width=int(width * T))
    def line(pts, fill=K, width=2):
        d.line(sh(pts), fill=fill, width=int(width * T), joint="curve")

    # Speech bubble: outline both parts, then refill inside so they join.
    tail = [v * S for v in (20, 30, 36, 30, 41, 78)]
    box = [v * S for v in (0, 2, 88, 34)]
    d.polygon(tail, fill=K)
    d.rounded_rectangle(box, 14 * S, fill=K)
    d.polygon([v * S for v in (23, 30, 34, 30, 38, 70)], fill=Wh)
    d.rounded_rectangle([v * S for v in (2, 4, 86, 32)], 12 * S, fill=Wh)

    # Tail paddle with hatching.
    e((118, 70, 196, 112))
    for x in range(140, 190, 12):
        line((x, 78, x - 8, 104), width=1.5)
    # Body and belly.
    e((44, 52, 156, 122))
    d.arc(sh((64, 84, 138, 132)), 200, 340, fill=K, width=int(1.5 * T))
    # Feet: solid, with toes.
    for fx in (66, 112):
        e((fx, 112, fx + 22, 126), K)
        for t in range(3):
            e((fx + 2 + t * 7, 120, fx + 8 + t * 7, 128), K, 0)
    # Head.
    e((38, 30, 112, 96))
    # Bill: solid black with white nostrils and mouth line.
    d.rounded_rectangle(sh((2, 60, 58, 86)), 13 * S, fill=K)
    line((10, 79, 46, 79), Wh, 1.5)
    d.ellipse(sh((14, 66, 19, 70)), fill=Wh)
    d.ellipse(sh((26, 66, 31, 70)), fill=Wh)
    # Eyes with highlights.
    for ex in (46, 66):
        d.ellipse(sh((ex, 47, ex + 13, 62)), fill=K)
        d.ellipse(sh((ex + 3, 49, ex + 8, 54)), fill=Wh)
    # Average down, then cut at mid-grey: clean 1-bit edges.
    im = im.resize((round(W * scale), round(H * scale)), Image.BOX)
    return im.point(lambda v: 255 if v >= 128 else 0)


def write_c_mono(im, path, name="splash_img_mono"):
    """LVGL I1 image: index 0 transparent, 1 black. White shows the screen."""
    W, H = im.size
    px = im.load()
    stride = (W + 7) // 8
    bits = bytearray(stride * H)
    for y in range(H):
        for x in range(W):
            if px[x, y] == 0:
                bits[y * stride + x // 8] |= 0x80 >> (x & 7)
    # Palette: ARGB8888 as B, G, R, A.
    data = bytes((0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF)) + bits
    with open(path, "w") as f:
        f.write("// Generated by tools/gen_splash.py --mono. Do not edit by hand.\n")
        f.write("// The mascot as 1-bit line art for e-paper.\n")
        f.write('#include <lvgl.h>\n\n')
        f.write("static const uint8_t kData[] = {\n")
        for i in range(0, len(data), 24):
            f.write("    " + ", ".join("0x%02x" % b for b in data[i:i + 24]) + ",\n")
        f.write("};\n\n")
        f.write("const lv_image_dsc_t %s = {\n" % name)
        f.write("    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_I1,\n")
        f.write("               .flags = 0, .w = %d, .h = %d, .stride = %d},\n" % (W, H, stride))
        f.write("    .data_size = sizeof(kData),\n")
        f.write("    .data = kData,\n")
        f.write("};\n")


def write_c(im, path, name="splash_img", guard=None):
    W, H = im.size
    px = im.load()
    rgb, alpha = bytearray(), bytearray()
    for y in range(H):
        for x in range(W):
            r, g, b, a = px[x, y]
            v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            rgb += bytes((v & 0xFF, v >> 8))
            alpha.append(a)
    data = rgb + alpha
    with open(path, "w") as f:
        f.write("// Generated by tools/gen_splash.py. Do not edit by hand.\n")
        f.write('#include <lvgl.h>\n\n')
        if guard:
            f.write("#ifdef %s\n\n" % guard)
        f.write("static const uint8_t kSplashData[] = {\n")
        for i in range(0, len(data), 24):
            f.write("    " + ", ".join("0x%02x" % b for b in data[i:i + 24]) + ",\n")
        f.write("};\n\n")
        f.write("const lv_image_dsc_t %s = {\n" % name)
        f.write("    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565A8,\n")
        f.write("               .flags = 0, .w = %d, .h = %d, .stride = %d},\n" % (W, H, W * 2))
        f.write("    .data_size = sizeof(kSplashData),\n")
        f.write("    .data = kSplashData,\n")
        f.write("};\n")
        if guard:
            f.write("\n#endif  // %s\n" % guard)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--png", help="also write a preview PNG")
    args = ap.parse_args()
    img = draw()
    write_c(img, os.path.join(ROOT, "src", "ui", "splash_img.c"))
    # Double size for the 800x480 CrowPanel 5.0, compiled only into that build.
    write_c(draw(2), os.path.join(ROOT, "src", "ui", "splash_img_large.c"),
            "splash_img_large", "DEVICE_CROWPANEL_50")
    mono = draw_mono()
    write_c_mono(mono, os.path.join(ROOT, "src", "ui", "splash_img_mono.c"))
    # The e-paper screensaver's smaller copy.
    write_c_mono(draw_mono(0.75), os.path.join(ROOT, "src", "ui", "splash_img_mono_small.c"),
                 "splash_img_mono_small")
    if args.png:
        mono.resize((W * 2, H * 2), Image.NEAREST).save(args.png.replace(".png", "_mono.png"))
    if args.png:
        bg = Image.new("RGBA", img.size, (18, 70, 92, 255))
        bg.alpha_composite(img)
        bg.resize((W * 2, H * 2), Image.NEAREST).save(args.png)
