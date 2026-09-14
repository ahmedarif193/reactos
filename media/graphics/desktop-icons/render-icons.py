#!/usr/bin/env python3
"""Encode the desktop icon PNGs as ReactOS ICO resources."""

from pathlib import Path
import runpy

from PIL import Image


def main():
    assets = Path(__file__).resolve().parent
    root = assets.parents[2]
    write_ico = runpy.run_path(
        str(assets.parent / "user-folders/render-icons.py"))["write_ico"]
    targets = (
        ("my-documents", ("dll/win32/shell32/res/icons/235.ico",)),
        ("my-pc", ("dll/win32/shell32/res/icons/16.ico",)),
        ("network-places", ("dll/win32/shell32/res/icons/18.ico",)),
        ("internet-browser", (
            "dll/win32/shell32/res/icons/512.ico",
            "base/applications/iexplore/iexplore.ico",
        )),
        ("recycle-bin", (
            "dll/win32/shell32/res/icons/32.ico",
            "dll/win32/shell32/res/icons/33.ico",
        )),
        ("applications-manager", ("base/applications/rapps/res/main.ico",)),
        ("command-prompt", ("base/shell/cmd/res/terminal.ico",)),
        ("device-manager", ("base/applications/mscutils/devmgmt/devmgmt.ico",)),
        ("dwm-settings", ("base/applications/dwmsettings/res/dwm-settings.ico",)),
        ("read-me", ("dll/win32/shell32/res/icons/152.ico",)),
        ("rosget", ("base/applications/cmdutils/rosget/res/rosget.ico",)),
        ("task-manager", (
            "base/applications/taskmgr/res/taskmgr.ico",
            "base/applications/taskmgr11/res/taskmgr11.ico",
        )),
    )

    for name, target_paths in targets:
        with Image.open(assets / (name + ".png")) as source:
            image = source.convert("RGBA")
            assert image.width == image.height
            assert image.getchannel("A").getextrema() == (0, 255)
            for target in target_paths:
                write_ico(image, root / target)
                print(f"{name} -> {target}")


if __name__ == "__main__":
    main()
