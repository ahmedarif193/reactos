import io
import os
import struct
import sys
from PIL import Image

RES = os.environ.get("WIN81_AERO_RES", "/Users/mac/working_dir/win81-fs/aero_res/IMAGE")
H = 20
MARGINS = (2, 2, 7, 2)
OUTLINE = 0.42
FACE_MAX = 0.35
RING = (255, 255, 255, 67)
RED = {1: (176, 39, 25), 2: (196, 43, 28), 3: (140, 30, 20), 6: (196, 43, 28), 7: (140, 30, 20)}
ACCENT = tuple(int(v) for v in os.environ.get("WIN8_ACCENT", "51,153,255").split(","))
BUTTON_MARGINS = (2, 2, 8, 2)
BUTTONS = {
    "close": (1015, 1016, 45, True, False, True),
    "min": (1041, 1042, 27, False, True, False),
    "max": (1030, 1031, 26, False, False, False),
    "restore": (1030, 1048, 26, False, False, False),
}


def load(rid):
    return Image.open(io.BytesIO(open(os.path.join(RES, "%d.bin" % rid), "rb").read())).convert("RGBA")


def state_cell(im, state, count=8):
    h = im.size[1] // count
    return im.crop((0, (state - 1) * h, im.size[0], state * h))


def nine_slice(im, margins, tw, th):
    l, r, t, b = margins
    w, h = im.size
    out = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
    xs = ((0, l, 0, l), (l, w - r, l, tw - r), (w - r, w, tw - r, tw))
    ys = ((0, t, 0, t), (t, h - b, t, th - b), (h - b, h, th - b, th))
    for sx0, sx1, dx0, dx1 in xs:
        for sy0, sy1, dy0, dy1 in ys:
            if sx1 <= sx0 or sy1 <= sy0 or dx1 <= dx0 or dy1 <= dy0:
                continue
            piece = im.crop((sx0, sy0, sx1, sy1)).resize((dx1 - dx0, dy1 - dy0), Image.NEAREST)
            out.paste(piece, (dx0, dy0))
    return out


def darken(im, inactive, lift=0.0):
    out = Image.new("RGBA", im.size, (0, 0, 0, 0))
    px = im.load()
    dst = out.load()
    for y in range(im.size[1]):
        for x in range(im.size[0]):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            lum = (r * 299 + g * 587 + b * 114) // 1000
            if lum < 150:
                dst[x, y] = (0, 0, 0, int(OUTLINE * a))
            else:
                white = (lum - 150) / 105.0 * FACE_MAX + lift
                if inactive:
                    white *= 0.7
                dst[x, y] = (255, 255, 255, int(min(1.0, white) * a))
    return out


def outline_ring(im):
    px = im.load()
    w, h = im.size
    edge = (0, 0, 0, int(OUTLINE * 255))
    for y in range(h):
        px[0, y] = edge
        px[w - 1, y] = edge
    for x in range(w):
        px[x, 0] = edge
        px[x, h - 1] = edge
    return im


def square_corners(im, right_outline):
    px = im.load()
    w, h = im.size
    edge = (0, 0, 0, int(OUTLINE * 255))
    for y in range(h):
        px[0, y] = edge
        if right_outline:
            px[w - 1, y] = edge
    for x in range(w):
        px[x, h - 1] = edge
    return im


def tint(cell, base, top_outline, lift=0.0):
    w, h = cell.size
    src = cell.load()
    lums = [(src[x, y][0] * 299 + src[x, y][1] * 587 + src[x, y][2] * 114) // 1000
            for y in range(1, h - 1) for x in range(1, w - 1) if src[x, y][3]]
    lo, hi = min(lums), max(lums)
    out = Image.new("RGBA", cell.size, (0, 0, 0, 0))
    dst = out.load()
    for y in range(h):
        for x in range(w):
            r, g, b, a = src[x, y]
            if a == 0:
                continue
            if x == 0 or x == w - 1 or y == h - 1 or (top_outline and y == 0):
                dst[x, y] = (0, 0, 0, int(OUTLINE * 255))
                continue
            lum = (r * 299 + g * 587 + b * 114) // 1000
            k = (lum - lo) / float(max(1, hi - lo)) * 0.35 + lift
            dst[x, y] = (int(base[0] + (255 - base[0]) * k), int(base[1] + (255 - base[1]) * k),
                         int(base[2] + (255 - base[2]) * k), 255)
    return out


def render(kind):
    bg_id, glyph_id, w, keep_color, ring_left, right_outline = BUTTONS[kind]
    bg = load(bg_id)
    glyph = load(glyph_id)
    sw = w + (1 if ring_left else 0) + (1 if right_outline else 0)
    sh = H + 1
    strip = Image.new("RGBA", (sw, sh * 8), (0, 0, 0, 0))
    for state in range(1, 9):
        src = state_cell(bg, state)
        src = src.crop((0, 1, src.size[0], src.size[1]))
        cell = nine_slice(src, MARGINS, w + (0 if right_outline else 1), H)
        cell = cell.crop((0, 0, w, H))
        mid = cell.getpixel((w // 2, H // 2))
        red = keep_color and mid[0] >= mid[1] + 40
        cell = tint(cell, RED[state], False) if red else darken(cell, state > 4)
        cell = square_corners(cell, right_outline)
        x0 = 1 if ring_left else 0
        frame = Image.new("RGBA", (sw, sh), (0, 0, 0, 0))
        frame.alpha_composite(cell, (x0, 0))
        g = state_cell(glyph, state)
        frame.alpha_composite(g, (x0 + (w - g.size[0] + 1) // 2, (H - g.size[1] + 1) // 2))
        px = frame.load()
        for y in range(sh):
            if ring_left:
                px[0, y] = RING
            if right_outline:
                px[sw - 1, y] = RING
        for x in range(sw):
            px[x, sh - 1] = RING
        strip.paste(frame, (0, (state - 1) * sh))
    return strip


def render_button():
    bg = load(1041)
    w = 24
    cells = {s: nine_slice(state_cell(bg, s), BUTTON_MARGINS, w, H) for s in (1, 3, 4)}
    dim = tuple(int(c * 0.55 + 43 * 0.45) for c in ACCENT)
    dark = tuple(int(c * 0.72) for c in ACCENT)
    variants = (
        darken(cells[1], False), darken(cells[1], False, 0.10), darken(cells[3], False), darken(cells[4], True),
        tint(cells[1], ACCENT, True), tint(cells[1], ACCENT, True, 0.15), tint(cells[3], dark, True), tint(cells[1], dim, True),
    )
    strip = Image.new("RGBA", (w, H * 8), (0, 0, 0, 0))
    for i, cell in enumerate(variants):
        strip.paste(outline_ring(cell), (0, i * H))
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


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "bitmaps")
    for kind, name in (("close", "NORMAL_CLOSEBUTTON.bmp"), ("min", "NORMAL_MINBUTTON.bmp"),
                       ("max", "NORMAL_MAXBUTTON.bmp"), ("restore", "NORMAL_RESTOREBUTTON.bmp")):
        save_bmp(render(kind), os.path.join(out, name))
    save_bmp(render_button(), os.path.join(out, "NORMAL_FLYOUTBUTTON.bmp"))


if __name__ == "__main__":
    main()
