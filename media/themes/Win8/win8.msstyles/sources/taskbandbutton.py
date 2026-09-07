import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from showdesktop import HORIZONTAL_IMAGE, VERTICAL_IMAGE, STYLES, png, resource, save_bmp, straighten

SOURCE_STATES = 3
TOOLBAR_STATES = (0, 1, 2, 0, 1, 2)


def restate(w, h, rows):
    band = h // SOURCE_STATES
    out = []
    for src in TOOLBAR_STATES:
        out += rows[src * band:(src + 1) * band]
    return w, band * len(TOOLBAR_STATES), out


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "bitmaps")
    for ident, name in ((HORIZONTAL_IMAGE, "NORMAL_TASKBANDBUTTON.bmp"),
                        (VERTICAL_IMAGE, "NORMAL_TASKBANDBUTTONVERTICAL.bmp")):
        w, h, rows = png(resource(STYLES, "IMAGE", ident))
        w, h, rows = restate(w, h, straighten(rows))
        save_bmp(w, h, rows, os.path.join(out, name))


if __name__ == "__main__":
    main()
