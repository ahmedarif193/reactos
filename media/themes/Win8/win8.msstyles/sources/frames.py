import os
import sys
from PIL import Image

KEY = {0: (32, 32, 32), 1: (43, 43, 43)}
LIGHT = {0: (87, 87, 87), 1: (95, 95, 95)}
DARK = (0, 0, 0)


def frame(w, h, states, left, right, top, bottom):
    im = Image.new("RGB", (w, h * states))
    px = im.load()
    for s in range(states):
        y0 = s * h
        for y in range(h):
            for x in range(w):
                dark = (left and x == 0) or (right and x == w - 1) or (top and y == 0) or (bottom and y == h - 1)
                light = (left and x == 1) or (right and x == w - 2) or (top and y == 1) or (bottom and y == h - 2)
                px[x, y0 + y] = DARK if dark else LIGHT[s] if light else KEY[s]
    return im


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "bitmaps")
    frame(8, 30, 2, True, True, True, False).save(os.path.join(out, "NORMAL_FRAMECAPTION.bmp"))
    frame(8, 22, 2, True, True, True, False).save(os.path.join(out, "NORMAL_SMALLFRAMECAPTION.bmp"))
    frame(8, 30, 2, True, True, True, True).save(os.path.join(out, "NORMAL_FRAMECAPTIONMIN.bmp"))
    frame(6, 8, 2, True, False, False, False).save(os.path.join(out, "NORMAL_FRAMELEFT.bmp"))
    frame(6, 8, 2, False, True, False, False).save(os.path.join(out, "NORMAL_FRAMERIGHT.bmp"))
    frame(8, 6, 2, True, True, False, True).save(os.path.join(out, "NORMAL_FRAMEBOTTOM.bmp"))


if __name__ == "__main__":
    main()
