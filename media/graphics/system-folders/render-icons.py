#!/usr/bin/env python3
"""Encode transparent shell PNGs using the user-folder ICO format."""

from pathlib import Path
import runpy

from PIL import Image


def main():
    assets = Path(__file__).resolve().parent
    root = assets.parents[2]
    write_ico = runpy.run_path(str(assets.parent / "user-folders/render-icons.py"))["write_ico"]
    targets = (
        ("my-pc", "dll/win32/shell32/res/icons/16.ico"),
        ("fixed-drive", "dll/win32/shell32/res/icons/9.ico"),
        ("folder", "dll/win32/shell32/res/icons/4.ico"),
        ("folder", "dll/win32/shell32/res/icons/5.ico"),
        ("control-panel", "dll/win32/shell32/res/icons/137.ico"),
        ("nt-object-namespace", "dll/shellext/ntobjshex/resources/1.ico"),
        ("system-registry", "dll/shellext/ntobjshex/resources/7.ico"),
    )
    for name, target in targets:
        with Image.open(assets / (name + ".png")) as source:
            image = source.convert("RGBA")
            assert image.width == image.height
            assert image.getchannel("A").getextrema() == (0, 255)
            write_ico(image, root / target)
        print(name + " -> " + target)


if __name__ == "__main__":
    main()
