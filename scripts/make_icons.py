#!/usr/bin/env python3
"""Draws the app icon (headphones + down arrow) and writes
assets/app.ico and extension/icons/icon{16,32,48,128}.png. Needs Pillow."""
import os
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
N = 1024
GREEN = (52, 160, 70, 255)
WHITE = (255, 255, 255, 255)


def draw():
    im = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([24, 24, N - 24, N - 24], radius=220, fill=GREEN)
    # headband
    w = 90
    d.arc([200, 170, N - 200, N - 170 + 120], start=180, end=360, fill=WHITE, width=w)
    # side bars down to ear cups
    d.rectangle([200, 470, 200 + w, 640], fill=WHITE)
    d.rectangle([N - 200 - w, 470, N - 200, 640], fill=WHITE)
    # ear cups
    d.rounded_rectangle([160, 560, 380, 840], radius=70, fill=WHITE)
    d.rounded_rectangle([N - 380, 560, N - 160, 840], radius=70, fill=WHITE)
    # download arrow in the middle
    cx = N // 2
    d.rectangle([cx - 45, 430, cx + 45, 650], fill=WHITE)
    d.polygon([(cx - 130, 630), (cx + 130, 630), (cx, 800)], fill=WHITE)
    return im


def main():
    im = draw()
    os.makedirs(os.path.join(ROOT, "assets"), exist_ok=True)
    os.makedirs(os.path.join(ROOT, "extension", "icons"), exist_ok=True)
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    im.resize((256, 256), Image.LANCZOS).save(os.path.join(ROOT, "assets", "app.ico"),
                                              sizes=[(s, s) for s in sizes])
    for s in (16, 32, 48, 128):
        im.resize((s, s), Image.LANCZOS).save(os.path.join(ROOT, "extension", "icons", f"icon{s}.png"))
    im.resize((512, 512), Image.LANCZOS).save(os.path.join(ROOT, "assets", "icon512.png"))


if __name__ == "__main__":
    main()
