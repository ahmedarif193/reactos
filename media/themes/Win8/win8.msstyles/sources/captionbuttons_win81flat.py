import os
import struct
import sys
from PIL import Image

ATLAS = os.environ.get("WIN81_DWM_ATLAS", "/Users/mac/working_dir/win81-fs/aero_res/stream_1060.png")
ACCENT = (43, 43, 43)
H = 20

CLOSE_BG = ((111, 0, 162, 88), 4, (3, 0, 49, 20), (2, 1, 2, 4))
MINMAX_BG = ((62, 440, 93, 528), 4, (3, 0, 31, 20), (3, 1, 2, 4))
GLYPHS = {
    "close": ((0, 290, 13, 346), 4),
    "min": ((0, 346, 14, 402), 4),
    "max": ((0, 638, 16, 694), 4),
    "restore": ((0, 458, 15, 518), 4),
}


def cell(atlas, rect, count, state):
    x0, y0, x1, y1 = rect
    h = (y1 - y0) // count
    return atlas.crop((x0, y0 + (state - 1) * h, x1, y0 + state * h))


def lift(color, amount):
    return tuple(min(255, c + (255 - c) * amount // 100) for c in color)


def colorize(im):
    out = im.copy()
    px = out.load()
    w, h = out.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a and (b > r + 40):
                lum = (r * 299 + g * 587 + b * 114) // 1000
                base = lift(ACCENT, 45) if lum > 90 else lift(ACCENT, 25)
                px[x, y] = (base[0], base[1], base[2], a)
    return out


def background(atlas, spec, state, inactive):
    rect, count, crop, margins = spec
    src = state
    if inactive and state in (1, 4):
        src = 4
    im = cell(atlas, rect, count, src).crop(crop)
    return colorize(im), margins


def glyph(atlas, kind, state, inactive):
    rect, count = GLYPHS[kind]
    disabled = state == 4
    src = 4 if disabled or (inactive and state == 1) else 2
    return cell(atlas, rect, count, src)


def render(kind):
    spec = CLOSE_BG if kind == "close" else MINMAX_BG
    w = spec[2][2] - spec[2][0]
    strip = Image.new("RGBA", (w, H * 8), (0, 0, 0, 0))
    for s in range(1, 9):
        state = ((s - 1) % 4) + 1
        inactive = s > 4
        bg, (ml, mt, mr, mb) = background(atlas_img, spec, state, inactive)
        g = glyph(atlas_img, kind, state, inactive)
        cw, ch = w - ml - mr, H - mt - mb
        gx = ml + (cw - g.size[0]) // 2
        gy = mt + (ch - g.size[1]) // 2
        frame = Image.new("RGBA", (w, H), (0, 0, 0, 0))
        frame.paste(bg, (0, 0), bg)
        frame.alpha_composite(g, (gx, gy))
        strip.paste(frame, (0, (s - 1) * H))
    return strip


def save_bmp(im, path):
    w, h = im.size
    px = im.load()
    rows = bytearray()
    for y in range(h - 1, -1, -1):
        for x in range(w):
            r, g, b, a = px[x, y]
            rows += bytes((b, g, r, a))
    hdr = struct.pack("<IiiHHIIiiII", 108, w, h, 1, 32, 3, len(rows), 2835, 2835, 0, 0)
    hdr += struct.pack("<IIII", 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    hdr += b"BGRs" + bytes(36) + struct.pack("<III", 0, 0, 0)
    off = 14 + len(hdr)
    with open(path, "wb") as f:
        f.write(b"BM" + struct.pack("<IHHI", off + len(rows), 0, 0, off) + hdr + rows)


atlas_img = Image.open(ATLAS).convert("RGBA")


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "bitmaps")
    for kind, name in (("close", "NORMAL_CLOSEBUTTON.bmp"), ("min", "NORMAL_MINBUTTON.bmp"),
                       ("max", "NORMAL_MAXBUTTON.bmp"), ("restore", "NORMAL_RESTOREBUTTON.bmp")):
        save_bmp(render(kind), os.path.join(out, name))


if __name__ == "__main__":
    main()
