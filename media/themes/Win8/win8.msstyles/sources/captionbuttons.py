import os
import struct
import sys
from PIL import Image

H = 21
OUTLINE = 0.42
GLYPH_OUTLINE = 0.30
FACE = [0.61, 0.66, 0.54, 0.51, 0.49, 0.45, 0.40, 0.37, 0.34, 0.31,
        0.29, 0.26, 0.22, 0.17, 0.13, 0.10, 0.07, 0.06, 0.28]
RED_HOT = (196, 43, 28)
RED_PRESSED = (176, 39, 25)
FACE_SCALE = 0.3


def over(dst, src):
    sr, sg, sb, sa = src
    dr, dg, db, da = dst
    oa = sa + da * (1 - sa)
    if oa <= 0:
        return (0, 0, 0, 0.0)
    return ((sr * sa + dr * da * (1 - sa)) / oa,
            (sg * sa + dg * da * (1 - sa)) / oa,
            (sb * sa + db * da * (1 - sa)) / oa, oa)


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [[(0, 0, 0, 0.0) for _ in range(w)] for _ in range(h)]

    def put(self, x, y, color, alpha):
        if 0 <= x < self.w and 0 <= y < self.h and alpha > 0:
            self.px[y][x] = over(self.px[y][x], (color[0], color[1], color[2], alpha))

    def white(self, x, y, a):
        self.put(x, y, (255, 255, 255), a)

    def black(self, x, y, a):
        self.put(x, y, (0, 0, 0), a)


def face_alpha(row, hot, inactive):
    a = FACE[row - 1] * FACE_SCALE
    if hot:
        a = min(0.92, a + 0.11)
    if inactive:
        a *= 0.7
    return a


def draw_body(c, w, hot=False, pressed=False, inactive=False, red=None):
    for y in range(H):
        for x in range(w):
            if y == 0 or y == H - 1 or x == 0:
                c.black(x, y, OUTLINE)
                continue
            if red:
                c.put(x, y, red, 1.0)
            if x == w - 1:
                c.white(x, y, (0.24 if not inactive else 0.17) * FACE_SCALE)
                continue
            a = face_alpha(y, hot, inactive)
            if x == 1:
                a = min(0.95, a + 0.12 * FACE_SCALE)
            elif x == w - 2:
                a = min(0.95, a + 0.05 * FACE_SCALE)
            if red:
                a *= 0.6
            c.white(x, y, a)
            if pressed:
                c.black(x, y, 0.12)


def outline_around(c, cells, alpha):
    cs = set(cells)
    ring = set()
    for (x, y) in cs:
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (x + dx, y + dy)
            if n not in cs:
                ring.add(n)
    for (x, y) in ring:
        c.black(x, y, alpha)


def glyph(c, cells, white_alpha, outline_alpha, inner=()):
    outline_around(c, cells, outline_alpha)
    for (x, y) in inner:
        c.black(x, y, outline_alpha)
    for (x, y) in cells:
        c.white(x, y, white_alpha)


def rect_ring(x0, y0, x1, y1, thick):
    return [(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)
            if x < x0 + thick or x > x1 - thick or y < y0 + thick or y > y1 - thick]


def min_cells():
    return [(x, y) for y in range(12, 15) for x in range(9, 19)]


def max_cells():
    return rect_ring(9, 7, 17, 14, 2)


def max_inner():
    return [(x, y) for y in range(9, 13) for x in range(11, 16) if x in (11, 15) or y in (9, 12)]


def restore_cells():
    back = rect_ring(12, 5, 18, 10, 2)
    front = rect_ring(8, 9, 14, 15, 2)
    back = [(x, y) for (x, y) in back if not (7 <= x <= 15 and 8 <= y <= 16)]
    return back + front


def close_cells():
    cells = []
    for r in range(8):
        d = r if r < 4 else 7 - r
        for x in (18 + d, 19 + d, 24 - d, 25 - d):
            cells.append((x, 7 + r))
    return sorted(set(cells))


def render(kind, w):
    strip = Image.new("RGBA", (w, H * 8))
    for state in range(1, 9):
        hot = state in (2, 6)
        pressed = state in (3, 7)
        disabled = state in (4, 8)
        inactive = state >= 5
        c = Canvas(w, H)
        red = RED_HOT if (kind == "close" and hot) else RED_PRESSED if (kind == "close" and pressed) else None
        draw_body(c, w, hot, pressed, inactive, red)
        ga = 0.55 if disabled else (0.8 if inactive and not hot and not pressed else 1.0)
        oa = GLYPH_OUTLINE * (0.5 if disabled else 1.0)
        if kind == "min":
            glyph(c, min_cells(), ga, oa)
        elif kind == "max":
            glyph(c, max_cells(), ga, oa, max_inner())
        elif kind == "restore":
            glyph(c, restore_cells(), ga, oa)
        else:
            glyph(c, close_cells(), ga, oa)
        for y in range(H):
            for x in range(w):
                r, g, b, a = c.px[y][x]
                strip.putpixel((x, (state - 1) * H + y),
                               (int(round(r)), int(round(g)), int(round(b)), int(round(a * 255))))
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
    for kind, name, w in (("close", "NORMAL_CLOSEBUTTON.bmp", 45), ("min", "NORMAL_MINBUTTON.bmp", 27),
                          ("max", "NORMAL_MAXBUTTON.bmp", 26), ("restore", "NORMAL_RESTOREBUTTON.bmp", 26)):
        save_bmp(render(kind, w), os.path.join(out, name))


if __name__ == "__main__":
    main()
