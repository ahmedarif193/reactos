#!/usr/bin/env python3
"""Encode the supplied Explorer toolbar artwork, optionally removing magenta.

Requires Pillow and NumPy. Pass --originals with the directory containing the
eleven original PNGs to repeat background removal. Otherwise use the stored
transparent PNGs.
"""

import argparse
import hashlib
import json
from pathlib import Path
import runpy

from PIL import Image


ASSETS = (
    ("01-back", "24", 1, "Back"),
    ("02-forward", "24", 2, "Forward"),
    ("03-up", "24", 3, "Up"),
    ("04-search", "25", 4, "Search"),
    ("05-folders", "25", 5, "Folders"),
    ("06-move", "25", 6, "Move To"),
    ("07-copy", "25", 7, "Copy To"),
    ("08-delete", "26", 8, "Delete"),
    ("09-undo", "26", 9, "Undo"),
    ("10-views", "26", 10, "Views"),
    ("11-dropdown", "26", 11, "Views dropdown"),
)
SIZES = (11, 16, 20, 24, 32, 40, 48, 64)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--originals", type=Path, help="directory containing the original PNGs")
    args = parser.parse_args()
    assets_dir = Path(__file__).resolve().parent
    icon_dir = assets_dir.parents[2] / "dll/win32/browseui/res/toolbar"
    icon_dir.mkdir(parents=True, exist_ok=True)
    renderer = runpy.run_path(str(assets_dir.parent / "user-folders/render-icons.py"))
    sources = []
    for name, second, number, position in ASSETS:
        png = assets_dir / (name + ".png")
        original_name = f"ChatGPT Image Sep 13, 2026, 11_05_{second} PM ({number}).png"
        if args.originals:
            original = args.originals / original_name
            with Image.open(original) as image:
                renderer["remove_magenta"](image).save(png, optimize=True)
            sources.append({
                "image": number,
                "original": original_name,
                "original_sha256": hashlib.sha256(original.read_bytes()).hexdigest(),
                "png_sha256": hashlib.sha256(png.read_bytes()).hexdigest(),
                "icon": name + ".ico",
                "toolbar_position": position,
                "sizes": SIZES,
            })
        with Image.open(png) as image:
            renderer["write_ico"](image.convert("RGBA"), icon_dir / (name + ".ico"), SIZES)
        print(f"{name}: image {number} -> {position}")
    if args.originals:
        (assets_dir / "sources.json").write_text(json.dumps(sources, indent=2) + "\n")


if __name__ == "__main__":
    main()
