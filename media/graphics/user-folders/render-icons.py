#!/usr/bin/env python3
"""Encode the supplied colored folder artwork as Windows icon resources.

Requires Pillow and NumPy. The checked-in RGBA PNGs are the source artwork.
``--originals`` remains available for the older five-file magenta-source set.
"""

import argparse
from pathlib import Path
import struct

import numpy as np
from PIL import Image, ImageFilter


ASSETS = (
    ("home", None, None, "35.ico"),
    ("music", "35", 1, "237.ico"),
    ("documents", "35", 2, "235.ico"),
    ("downloads", "35", 3, "downloads.ico"),
    ("videos", "36", 4, "238.ico"),
    ("pictures", "36", 5, "236.ico"),
)
SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)


def remove_magenta(image):
    """Unmatte the narrow outer edge while preserving the opaque interior."""
    rgb = np.asarray(image.convert("RGB"), dtype=np.float32)
    magenta = np.minimum(rgb[:, :, 0], rgb[:, :, 2]) - rgb[:, :, 1]
    foreground = magenta <= 25
    mask = Image.fromarray(foreground.astype(np.uint8) * 255)
    core = np.asarray(mask.filter(ImageFilter.MinFilter(7))) != 0
    band = np.asarray(mask.filter(ImageFilter.MaxFilter(9))) != 0
    background = np.median(rgb[magenta > 180], axis=0)

    # Extend nearby opaque artwork colors into the antialiased boundary. This
    # gives a local foreground estimate for undoing the magenta composition.
    reference = rgb.copy()
    known = core.copy()
    height, width = core.shape
    for _ in range(8):
        padded_rgb = np.pad(reference * known[:, :, None], ((1, 1), (1, 1), (0, 0)))
        padded_known = np.pad(known.astype(np.float32), 1)
        total = np.zeros_like(rgb)
        count = np.zeros_like(known, dtype=np.float32)
        for dy, dx in ((0, 1), (1, 0), (1, 2), (2, 1)):
            total += padded_rgb[dy:dy + height, dx:dx + width]
            count += padded_known[dy:dy + height, dx:dx + width]
        extend = ~known & (count > 0) & band
        reference[extend] = total[extend] / count[extend, None]
        known |= extend

    direction = reference - background
    denominator = np.maximum(np.sum(direction * direction, axis=2), 1)
    alpha = np.clip(np.sum((rgb - background) * direction, axis=2) / denominator, 0, 1)
    alpha[core] = 1
    alpha[~band | ~known | (alpha < 0.025)] = 0
    colors = np.clip(
        (rgb - (1 - alpha[:, :, None]) * background) /
        np.maximum(alpha[:, :, None], 1 / 255), 0, 255)
    colors[core] = rgb[core]
    colors[alpha == 0] = 0
    rgba = np.dstack((np.rint(colors), np.rint(alpha * 255))).astype(np.uint8)
    assert np.array_equal(rgba[core, :3], rgb[core].astype(np.uint8))
    return Image.fromarray(rgba)


def write_ico(image, path, sizes=SIZES):
    """Write 32-bit DIB frames with alpha and legacy AND transparency masks."""
    frames = []
    for size in sizes:
        frame = image.resize((size, size), Image.Resampling.LANCZOS)
        rgba = np.array(frame)
        rgba[rgba[:, :, 3] == 0, :3] = 0
        pixels = rgba[::-1, :, [2, 1, 0, 3]].tobytes()
        mask_stride = ((size + 31) // 32) * 4
        mask = bytearray(mask_stride * size)
        for y, row in enumerate(rgba[::-1, :, 3]):
            for x, alpha in enumerate(row):
                if alpha < 128:
                    mask[y * mask_stride + x // 8] |= 0x80 >> (x % 8)
        header = struct.pack("<IiiHHIIiiII", 40, size, size * 2,
                             1, 32, 0, len(pixels), 0, 0, 0, 0)
        frames.append(header + pixels + mask)

    offset = 6 + 16 * len(frames)
    directory = bytearray(struct.pack("<HHH", 0, 1, len(frames)))
    for size, frame in zip(sizes, frames):
        directory.extend(struct.pack("<BBBBHHII", size % 256, size % 256,
                                     0, 0, 1, 32, len(frame), offset))
        offset += len(frame)
    path.write_bytes(directory + b"".join(frames))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--originals", type=Path, help="directory containing the five original PNGs")
    args = parser.parse_args()
    assets_dir = Path(__file__).resolve().parent
    icon_dir = assets_dir.parents[2] / "dll/win32/shell32/res/icons"
    for name, second, number, icon in ASSETS:
        png = assets_dir / (name + ".png")
        if args.originals and second is not None:
            original = args.originals / (
                f"ChatGPT Image Sep 13, 2026, 10_51_{second} PM ({number}).png")
            with Image.open(original) as image:
                remove_magenta(image).save(png, optimize=True)
        with Image.open(png) as image:
            image = image.convert("RGBA")
            assert image.width == image.height
            write_ico(image, icon_dir / icon)
        print(f"{name}: {png.name} -> {icon}")


if __name__ == "__main__":
    main()
