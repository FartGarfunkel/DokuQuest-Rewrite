#!/usr/bin/env python3
"""tools/android/make_icons.py — the Android launcher icons, made from the game's own icon (assets/icon.png).

    python3 tools/android/make_icons.py        (needs Pillow; writes android/res/mipmap-*/)

The icon is pixel art (32x32 drawn 8x larger). Android 8+ shows an adaptive icon: a 108dp foreground over a background
(android/res/drawable/ic_launcher_background.xml), cut to the launcher's shape (a circle on a Pixel). The art is placed
60dp wide in the middle, so even the cube's outer corners stay inside the 66dp circle every shape keeps.
The plain icons (mipmap-*/ic_launcher.png) are for anything that asks for a flat picture.
Run it again after changing assets/icon.png, and commit the PNGs it writes.
"""
import os
import sys
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "assets", "icon.png")
RES = os.path.join(ROOT, "android", "res")
DENSITIES = {"mdpi": 1.0, "hdpi": 1.5, "xhdpi": 2.0, "xxhdpi": 3.0, "xxxhdpi": 4.0}
ART_DP, CANVAS_DP, LEGACY_DP, LEGACY_ART_DP = 60, 108, 48, 46


def pixel_art(size):
    """The icon at `size` px: blown up with hard pixels first, then brought down smoothly, so the blocks stay crisp."""
    art = Image.open(SRC).convert("RGBA")
    big = art.resize((art.width * 8, art.height * 8), Image.NEAREST)
    return big.resize((size, size), Image.LANCZOS)


def main():
    if not os.path.exists(SRC):
        sys.exit("missing " + SRC)
    for name, scale in DENSITIES.items():
        out = os.path.join(RES, "mipmap-" + name)
        os.makedirs(out, exist_ok=True)
        canvas = round(CANVAS_DP * scale)
        art = round(ART_DP * scale)
        fg = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
        fg.alpha_composite(pixel_art(art), ((canvas - art) // 2, (canvas - art) // 2))
        fg.save(os.path.join(out, "ic_launcher_foreground.png"), optimize=True)
        legacy = round(LEGACY_DP * scale)
        la = round(LEGACY_ART_DP * scale)
        flat = Image.new("RGBA", (legacy, legacy), (0, 0, 0, 0))
        flat.alpha_composite(pixel_art(la), ((legacy - la) // 2, (legacy - la) // 2))
        flat.save(os.path.join(out, "ic_launcher.png"), optimize=True)
        flat.save(os.path.join(out, "ic_launcher_round.png"), optimize=True)
        print("mipmap-%s: foreground %dpx, icon %dpx" % (name, canvas, legacy))


if __name__ == "__main__":
    main()
