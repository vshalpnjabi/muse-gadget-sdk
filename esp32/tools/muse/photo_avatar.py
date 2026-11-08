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
square (centre crop), resized to SIDE (the face's size on a 466 px screen is
320) and stored as RGB565, SIDE * SIDE * 2 bytes of flash. Needs Pillow.
Remove the file to build without the picture.
"""
import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "components", "muse", "avatar", "muse_photo_img.c")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("picture")
    ap.add_argument("--side", type=int, default=320, help="pixels per side (default 320)")
    ap.add_argument("--share", type=float, default=0.72,
                    help="share of the picture's width the round face shows (default 0.72: head and shoulders)")
    ap.add_argument("--center-y", type=float, default=0.41,
                    help="how far down the picture that view is centred (default 0.41: the app puts the face high)")
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
