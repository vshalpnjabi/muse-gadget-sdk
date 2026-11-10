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

    python3 tools/muse/photo_avatar.py PICTURE [--side 320] [--out FOLDER]

writes muse_photo_img.c into FOLDER (by default components/muse/avatar/,
gitignored like your drawn avatar; point CONFIG_MUSE_AVATAR_DIR at any other
folder you use), which the next build picks up: Settings > Appearance then offers the
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
    # Light pockets the edge fill can't reach (gaps between curls of hair) are
    # backdrop too, unless they touch skin: eye whites and the glasses' glints
    # sit against skin, hair gaps against hair and outline.
    def pale(c):
        r, g, b = c
        return min(r, g, b) >= 200 and max(r, g, b) - min(r, g, b) <= 30

    def skin(c):
        r, g, b = c
        return r >= 190 and g >= 140 and b >= 100 and r - b >= 35

    done = bytearray(seen)
    for y0 in range(h):
        for x0 in range(w):
            if done[y0 * w + x0] or not pale(px[x0, y0]):
                continue
            comp, touches_skin, stack = [], False, [(x0, y0)]
            done[y0 * w + x0] = 1
            while stack:
                x, y = stack.pop()
                comp.append((x, y))
                for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                    if not (0 <= nx < w and 0 <= ny < h):
                        continue
                    j = ny * w + nx
                    if done[j]:
                        continue
                    if pale(px[nx, ny]):
                        done[j] = 1
                        stack.append((nx, ny))
                    elif skin(px[nx, ny]):
                        touches_skin = True
            if not touches_skin and len(comp) < w * h // 50:
                for x, y in comp:
                    seen[y * w + x] = 1

    # The outline's anti-aliased rim was blended with white: within 2 px of the
    # backdrop, greyish light pixels are darkened by how light they are.
    dist = bytearray([255]) * (w * h)
    for i in range(w * h):
        if seen[i]:
            dist[i] = 0
    for _ in range(2):
        nxt = bytearray(dist)
        for y in range(h):
            for x in range(w):
                i = y * w + x
                if dist[i] == 255 and any(0 <= x + dx < w and 0 <= y + dy < h and dist[(y + dy) * w + x + dx] != 255
                                          for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1))):
                    nxt[i] = min(dist[(y + dy) * w + x + dx] for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1))
                                 if 0 <= x + dx < w and 0 <= y + dy < h) + 1
        dist = nxt
    for y in range(h):
        for x in range(w):
            i = y * w + x
            if dist[i] == 0:
                px[x, y] = (0, 0, 0)
            elif dist[i] <= 2:
                r, g, b = px[x, y]
                if max(r, g, b) - min(r, g, b) <= 40:   # grey or white glow, not a coloured part
                    keep = max(0.0, min(1.0, (255 - min(r, g, b)) / 155.0))   # white: 0, dark outline: 1
                    px[x, y] = (int(r * keep), int(g * keep), int(b * keep))


def _avg(im, box):
    px = [im.getpixel((x, y)) for x in range(box[0], box[2]) for y in range(box[1], box[3])]
    return tuple(sum(c[i] for c in px) // len(px) for i in range(3))


def _lid(im, cx, cy, rx, ry):
    """An eyelid's colour: the median of the skin in a ring just round the eye
    (between it and the glasses), a shade darker, as a lid is."""
    ring = []
    for y in range(cy - ry - 5, cy + ry + 6):
        for x in range(cx - rx - 5, cx + rx + 6):
            e = ((x - cx) / (rx + 1.5)) ** 2 + ((y - cy) / (ry + 1.5)) ** 2
            if 1.0 < e <= 1.9:
                r, g, b = im.getpixel((x, y))
                if r >= 190 and g >= 140 and b >= 100 and r - b >= 35:   # skin, not lash or frame
                    ring.append((r, g, b))
    if not ring:
        return _avg(im, (cx - 4, cy + ry + 2, cx + 4, cy + ry + 5))
    med = [sorted(c[i] for c in ring)[len(ring) // 2] for i in range(3)]
    return tuple(int(v * 0.95) for v in med)


def _darkest(im, box):
    return min((im.getpixel((x, y)) for x in range(box[0], box[2]) for y in range(box[1], box[3])), key=sum)


def face_patches(im, eyes, eye_size, mouth):
    """The face's moving parts, drawn from the picture's own colours, as
    (x, y, w, h, image) patches the board swaps in: eyes closed, eyes happy
    (closed in an upward curve), glancing left and right, the mouth open small,
    medium and wide, a smile, and a wink with either eye.
    Order matters: muse_photo.c names them by index."""
    from PIL import ImageDraw
    out = []
    rx, ry = eye_size
    if eyes:
        lx, ly, rx_, ry_ = eyes
        x0, y0 = min(lx, rx_) - rx - 4, min(ly, ry_) - ry - 4
        x1, y1 = max(lx, rx_) + rx + 5, max(ly, ry_) + ry + 5
        for curve in (1, -1):   # 1: closed (bowed down), -1: happy (bowed up)
            p = im.crop((x0, y0, x1, y1)).copy()
            d = ImageDraw.Draw(p)
            for cx, cy in ((lx, ly), (rx_, ry_)):
                skin = _lid(im, cx, cy, rx, ry)
                lash = _darkest(im, (cx - rx // 2, cy - ry, cx + rx // 2, cy - ry // 2))   # the upper lid's line
                ex, ey = cx - x0, cy - y0
                d.ellipse((ex - rx, ey - ry, ex + rx, ey + ry), fill=skin)
                pts = []
                for k in range(-rx + 1, rx):
                    t = k / rx
                    pts.append((ex + k, ey + 1 + curve * 3.5 * (1 - t * t) - (2 if curve < 0 else 0)))
                d.line(pts, fill=lash, width=2)
            out.append((x0, y0, p))
        # Glances: the irises shifted inside the eye, the white filling in behind them.
        for dirn in (-1, 1):   # left, right (as you look at the screen)
            p = im.crop((x0, y0, x1, y1)).copy()
            src = im.crop((x0, y0, x1, y1))
            for cx, cy in ((lx, ly), (rx_, ry_)):
                ex, ey = cx - x0, cy - y0
                irx, iry = max(rx - 4, 3), max(ry - 2, 3)
                white = max((src.getpixel((ex + k, ey)) for k in range(-rx + 2, rx - 1)), key=sum)
                for yy in range(ey - iry, ey + iry + 1):
                    for xx in range(ex - irx, ex + irx + 1):
                        if ((xx - ex) / irx) ** 2 + ((yy - ey) / iry) ** 2 > 1:
                            continue
                        sx = xx - dirn * 3
                        inside = ((sx - ex) / irx) ** 2 + ((yy - ey) / iry) ** 2 <= 1
                        p.putpixel((xx, yy), src.getpixel((sx, yy)) if inside else white)
            out.append((x0, y0, p))
    if mouth:
        mx, my = mouth
        x0, y0, x1, y1 = mx - 18, my - 8, mx + 19, my + 12
        lip = _darkest(im, (mx - 6, my - 2, mx + 6, my + 3))   # the mouth's own line
        dark = (max(lip[0] - 10, 40), max(lip[1] - 25, 12), max(lip[2] - 25, 12))
        for w2, h2 in ((7, 2), (9, 4), (11, 6)):
            p = im.crop((x0, y0, x1, y1)).copy()
            d = ImageDraw.Draw(p)
            ex, ey = mx - x0, my - y0 + h2 // 2
            d.ellipse((ex - w2, ey - h2, ex + w2, ey + h2), fill=(110, 40, 40), outline=dark)
            if h2 >= 4:   # a tongue in the lower half
                d.ellipse((ex - w2 + 3, ey, ex + w2 - 3, ey + h2 - 1), fill=(205, 105, 100))
            out.append((x0, y0, p))
    if mouth:
        # A smile: a wider curve over the picture's own, in its lip colour.
        p = im.crop((x0, y0, x1, y1)).copy()
        d = ImageDraw.Draw(p)
        ex, ey = mx - x0, my - y0
        pts = [(ex + k, ey - 2 + 5.0 * (1 - (k / 17.0) ** 2)) for k in range(-17, 18)]
        d.line(pts, fill=lip, width=2)
        out.append((x0, y0, p))
    if eyes:
        # Winks: one eye closed, the other open, the screen's right eye then its left.
        lx, ly, rx_, ry_ = eyes
        x0, y0 = min(lx, rx_) - rx - 4, min(ly, ry_) - ry - 4
        x1, y1 = max(lx, rx_) + rx + 5, max(ly, ry_) + ry + 5
        for cx, cy in ((rx_, ry_), (lx, ly)):
            p = im.crop((x0, y0, x1, y1)).copy()
            d = ImageDraw.Draw(p)
            skin = _lid(im, cx, cy, rx, ry)
            lash = _darkest(im, (cx - rx // 2, cy - ry, cx + rx // 2, cy - ry // 2))
            ex, ey = cx - x0, cy - y0
            d.ellipse((ex - rx, ey - ry, ex + rx, ey + ry), fill=skin)
            pts = [(ex + k, ey + 1 + 3.5 * (1 - (k / rx) ** 2)) for k in range(-rx + 1, rx)]
            d.line(pts, fill=lash, width=2)
            out.append((x0, y0, p))
    if eyes:
        # Looking up (thinking) and down (working): the irises shifted inside the eye.
        lx, ly, rx_, ry_ = eyes
        x0, y0 = min(lx, rx_) - rx - 4, min(ly, ry_) - ry - 4
        x1, y1 = max(lx, rx_) + rx + 5, max(ly, ry_) + ry + 5
        for dirn in (-1, 1):   # up, down
            p = im.crop((x0, y0, x1, y1)).copy()
            src = im.crop((x0, y0, x1, y1))
            for cx, cy in ((lx, ly), (rx_, ry_)):
                ex, ey = cx - x0, cy - y0
                irx, iry = max(rx - 4, 3), max(ry - 2, 3)
                white = max((src.getpixel((ex + k, ey)) for k in range(-rx + 2, rx - 1)), key=sum)
                for yy in range(ey - iry, ey + iry + 1):
                    for xx in range(ex - irx, ex + irx + 1):
                        if ((xx - ex) / irx) ** 2 + ((yy - ey) / iry) ** 2 > 1:
                            continue
                        sy = yy - dirn * 2
                        inside = ((xx - ex) / irx) ** 2 + ((sy - ey) / iry) ** 2 <= 1
                        p.putpixel((xx, yy), src.getpixel((xx, sy)) if inside else white)
            out.append((x0, y0, p))
    if mouth:
        # Lip shapes for speech: a round "oo" and a wide "ee", beside the open sizes.
        mx, my = mouth
        x0, y0, x1, y1 = mx - 18, my - 8, mx + 19, my + 12
        lip = _darkest(im, (mx - 6, my - 2, mx + 6, my + 3))
        dark = (max(lip[0] - 10, 40), max(lip[1] - 25, 12), max(lip[2] - 25, 12))
        for w2, h2 in ((4, 4), (12, 2)):   # oo, ee
            p = im.crop((x0, y0, x1, y1)).copy()
            d = ImageDraw.Draw(p)
            ex, ey = mx - x0, my - y0 + h2 // 2
            d.ellipse((ex - w2, ey - h2, ex + w2, ey + h2), fill=(110, 40, 40), outline=dark)
            out.append((x0, y0, p))
    return [(x, y, p.size[0], p.size[1], p) for x, y, p in out]


def face_layout(side, eyes, eye_size, mouth, chin):
    """The touch zones and the head's extent, 0..1, from the eyes and mouth found
    in the result. None without both: the board's defaults stand."""
    if not (eyes and mouth):
        return None
    clamp = lambda x: max(0.0, min(1.0, x))
    lx, ly, rx, ry = (c / side for c in eyes)
    mx, my = (c / side for c in mouth)
    ey = (ly + ry) / 2
    eh = eye_size[1] / side
    gap = abs(rx - lx)
    head_chin = clamp(chin / side if chin is not None else my + 0.6 * (my - ey))
    return {
        "hair_end": clamp(ey - eh - 0.6 * gap),
        "eyes_end": clamp(ey + eh + 0.08),
        "eyes_u0": clamp(min(lx, rx) - 0.5 * gap),
        "eyes_u1": clamp(max(lx, rx) + 0.5 * gap),
        "face_end": clamp((my + head_chin) / 2),
        "chin_end": clamp(head_chin + 0.08),
        "chin_u0": clamp(mx - 0.6 * gap),
        "chin_u1": clamp(mx + 0.6 * gap),
        "head_chin": head_chin,
        "neck_end": clamp(head_chin + 0.12),
    }


def _rgb565(im):
    raw = im.tobytes()   # RGB, row by row
    return [(raw[i] >> 3) << 11 | (raw[i + 1] >> 2) << 5 | (raw[i + 2] >> 3) for i in range(0, len(raw), 3)]


def _c_array(name, px):
    lines = ["    " + ", ".join(f"0x{v:04x}" for v in px[i:i + 16]) + "," for i in range(0, len(px), 16)]
    return f"static const uint16_t {name}[] = {{\n" + "\n".join(lines) + "\n};\n"


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
    ap.add_argument("--eyes", help="the eyes' centres in the result, LX,LY,RX,RY (see --preview): "
                                   "the board then blinks them, and squints them happily when you pet it")
    ap.add_argument("--eye-size", default="14,8", help="half width and half height of an eye's opening (default 14,8)")
    ap.add_argument("--mouth", help="the mouth's centre in the result, X,Y: the board then opens it as Muse talks")
    ap.add_argument("--chin", type=float,
                    help="the chin's bottom in the result, Y pixels: a head shake moves what's above it "
                         "(default: a little below the mouth, or the default crop's)")
    ap.add_argument("--out", help="the folder to write muse_photo_img.c into (default components/muse/avatar; "
                                  "set CONFIG_MUSE_AVATAR_DIR to it if it's another)")
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
    def nums(text, n, what):
        try:
            v = [int(round(float(t))) for t in text.split(",")]
        except ValueError:
            v = []
        if len(v) != n:
            sys.exit(f"{what} takes {n} numbers, comma-separated")
        return v

    eyes = nums(args.eyes, 4, "--eyes") if args.eyes else None
    mouth = nums(args.mouth, 2, "--mouth") if args.mouth else None
    patches = face_patches(im, eyes, nums(args.eye_size, 2, "--eye-size"), mouth)
    if args.preview:
        # The picture, then each patch in place: what the board can show.
        sheet = Image.new("RGB", (im.size[0] * (1 + len(patches)), im.size[1]))
        sheet.paste(im, (0, 0))
        for k, (x, y, w, h, p) in enumerate(patches):
            frame = im.copy()
            frame.paste(p, (x, y))
            sheet.paste(frame, (im.size[0] * (k + 1), 0))
        sheet.save(args.preview)
    px = _rgb565(im)
    out = os.path.join(os.path.abspath(args.out), "muse_photo_img.c") if args.out else OUT
    layout = face_layout(args.side, eyes, nums(args.eye_size, 2, "--eye-size"), mouth, args.chin)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        f.write("/* Made by tools/muse/photo_avatar.py from %s. Yours: gitignored. */\n"
                % os.path.basename(args.picture))
        f.write('#include "muse_photo.h"\n\n')
        f.write(f"const int muse_photo_side = {args.side};\n")
        f.write(f"const uint16_t muse_photo_px[{args.side} * {args.side}] = {{\n")
        f.write("\n".join("    " + ", ".join(f"0x{v:04x}" for v in px[i:i + 16]) + ","
                          for i in range(0, len(px), 16)))
        f.write("\n};\n\n")
        for k, (x, y, w, h, p) in enumerate(patches):
            f.write(_c_array(f"patch{k}", _rgb565(p)))
        f.write("\n/* the face's moving parts (muse_photo.h) */\n")
        kinds = (["MUSE_PHOTO_EYES_CLOSED", "MUSE_PHOTO_EYES_HAPPY", "MUSE_PHOTO_EYES_LEFT",
                  "MUSE_PHOTO_EYES_RIGHT"] if eyes else []) + \
                (["MUSE_PHOTO_MOUTH_1", "MUSE_PHOTO_MOUTH_2", "MUSE_PHOTO_MOUTH_3",
                  "MUSE_PHOTO_MOUTH_SMILE"] if mouth else []) + \
                (["MUSE_PHOTO_EYES_WINK", "MUSE_PHOTO_EYES_WINK_L", "MUSE_PHOTO_EYES_UP", "MUSE_PHOTO_EYES_DOWN"] if eyes else []) + \
                (["MUSE_PHOTO_MOUTH_OO", "MUSE_PHOTO_MOUTH_EE"] if mouth else [])
        f.write("const muse_photo_patch_t muse_photo_patches[] = {\n")
        for k, ((x, y, w, h, p), kind) in enumerate(zip(patches, kinds)):
            f.write(f"    {{ {kind}, {x}, {y}, {w}, {h}, patch{k} }},\n")
        if not patches:
            f.write("    { MUSE_PHOTO_PATCH_NONE, 0, 0, 0, 0, 0 },\n")
        f.write("};\n")
        f.write(f"const int muse_photo_patch_count = {len(patches)};\n")
        if layout:
            f.write("\n/* where the face's parts are, 0..1 (muse_photo.h) */\n")
            f.write("const muse_photo_layout_t muse_photo_layout = {\n")
            f.write("".join(f"    .{k} = {v:.3f}f,\n" for k, v in layout.items()))
            f.write("};\n")
    print(f"Wrote {out}: {args.side} x {args.side}, {len(px) * 2} bytes"
          + ("" if layout else " (default face layout: give --eyes and --mouth to fit touches to this face)"))


if __name__ == "__main__":
    main()
