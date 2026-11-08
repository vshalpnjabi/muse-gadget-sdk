#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Turns a picture (the Muse app's portrait of your Muse) into the photo avatar.

    python3 tools/muse/photo_avatar.py PICTURE [--side 320]

writes components/muse/avatar/muse_photo_img.c (gitignored, like your drawn
avatar), which the next build picks up: Settings > Appearance then offers the
picture beside the pixel art, and shows it by default. The picture is made
square (centre crop), cut to head and shoulders, resized to SIDE (the face's
size on a 466 px screen is 320), its plain light backdrop turned black, and
stored as RGB565, SIDE * SIDE * 2 bytes of flash. Needs Pillow.
Remove the file to build without the picture.
"""
import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "components", "muse", "avatar", "muse_photo_img.c")


def black_background(im, tol):
    """Turns the picture's light, plain backdrop black, as an OLED shows nothing.

    The app's portraits are outlined cartoons on near-white: everything near
    white and grey that connects to the picture's edge is backdrop. Pale parts
    of the character inside its outline (a cream T-shirt) aren't reached. The
    outline's anti-aliased rim was blended with white; it's darkened by how
    light it is, so no white halo is left around the character on black.
    """
    w, h = im.size
    px = im.load()

    def light(c):
        r, g, b = c
        return min(r, g, b) >= 255 - tol and max(r, g, b) - min(r, g, b) <= 24

    seen = bytearray(w * h)
    stack = [(x, y) for x in range(w) for y in (0, h - 1)] + [(x, y) for y in range(h) for x in (0, w - 1)]
    while stack:
        x, y = stack.pop()
        i = y * w + x
        if seen[i] or not light(px[x, y]):
            continue
        seen[i] = 1
        if x > 0: stack.append((x - 1, y))
        if x < w - 1: stack.append((x + 1, y))
        if y > 0: stack.append((x, y - 1))
        if y < h - 1: stack.append((x, y + 1))
    for y in range(h):
        for x in range(w):
            i = y * w + x
            if seen[i]:
                px[x, y] = (0, 0, 0)
                continue
            near = any(0 <= x + dx < w and 0 <= y + dy < h and seen[(y + dy) * w + x + dx]
                       for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)))
            if near:
                r, g, b = px[x, y]
                keep = max(0.0, min(1.0, (255 - min(r, g, b)) / 155.0))   # white: 0, dark outline: 1
                px[x, y] = (int(r * keep), int(g * keep), int(b * keep))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("picture")
    ap.add_argument("--side", type=int, default=320, help="pixels per side (default 320)")
    ap.add_argument("--share", type=float, default=0.72,
                    help="share of the picture's width the round face shows (default 0.72: head and shoulders)")
    ap.add_argument("--center-y", type=float, default=0.41,
                    help="how far down the picture that view is centred (default 0.41: the app puts the face high)")
    ap.add_argument("--keep-background", action="store_true",
                    help="keep the picture's own background (by default its light, plain backdrop turns black)")
    ap.add_argument("--bg-tolerance", type=int, default=28,
                    help="how far from white the backdrop may be, 0-255 (default 28)")
    ap.add_argument("--preview", help="also save the result as an image here (PNG), to check it")
    args = ap.parse_args()
    try:
        from PIL import Image
    except ImportError:
        sys.exit("Pillow is missing: python3 -m pip install pillow")
    if not 32 <= args.side <= 512:
        sys.exit("--side must be 32 to 512")

    im = Image.open(args.picture)
    if im.mode in ("RGBA", "LA", "P"):
        im = im.convert("RGBA")
        bg = Image.new("RGBA", im.size, (0, 0, 0, 255))
        im = Image.alpha_composite(bg, im)
    im = im.convert("RGB")
    w, h = im.size
    s = min(w, h)
    im = im.crop(((w - s) // 2, (h - s) // 2, (w - s) // 2 + s, (h - s) // 2 + s))
    # The view the round face shows, cropped and resized here (Lanczos), so the
    # board draws it 1:1 rather than enlarging it with nearest-neighbour steps.
    if not 0.2 <= args.share <= 1.0:
        sys.exit("--share must be 0.2 to 1")
    v = args.share * s
    top = min(max(args.center_y * s - v / 2, 0), s - v)
    left = (s - v) / 2
    im = im.resize((args.side, args.side), Image.LANCZOS, box=(left, top, left + v, top + v))

    if not args.keep_background:
        black_background(im, args.bg_tolerance)
    if args.preview:
        im.save(args.preview)   # what the board gets, before the round mask
    raw = im.tobytes()   # RGB, row by row
    px = [(raw[i] >> 3) << 11 | (raw[i + 1] >> 2) << 5 | (raw[i + 2] >> 3) for i in range(0, len(raw), 3)]
    lines = []
    for i in range(0, len(px), 16):
        lines.append("    " + ", ".join(f"0x{v:04x}" for v in px[i:i + 16]) + ",")
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("/* Made by tools/muse/photo_avatar.py from %s. Yours: gitignored. */\n"
                % os.path.basename(args.picture))
        f.write('#include "muse_photo.h"\n\n')
        f.write(f"const int muse_photo_side = {args.side};\n")
        f.write(f"const uint16_t muse_photo_px[{args.side} * {args.side}] = {{\n")
        f.write("\n".join(lines))
        f.write("\n};\n")
    print(f"Wrote {os.path.relpath(OUT, ROOT)}: {args.side} x {args.side}, {len(px) * 2} bytes")


if __name__ == "__main__":
    main()
