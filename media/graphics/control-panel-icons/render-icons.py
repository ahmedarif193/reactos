#!/usr/bin/env python3
"""Encode the supplied Control Panel artwork as ReactOS icon resources."""

from pathlib import Path
import runpy

from PIL import Image


TARGETS = (
    ("add-hardware", ("dll/cpl/hdwwiz/resources/applet.ico",)),
    ("administrative-tools", ("dll/cpl/main/resources/admintools_folder.ico",)),
    ("console", ("dll/cpl/console/res/terminal.ico",)),
    ("date-and-time", ("dll/cpl/timedate/resources/applet.ico",)),
    ("device-manager", ("base/applications/mscutils/devmgmt/devmgmt.ico",)),
    ("display", ("dll/cpl/desk/resources/applet.ico",)),
    ("ease-of-access", ("dll/cpl/access/resources/applet.ico",)),
    ("folder-options", ("dll/win32/shell32/res/icons/210.ico",)),
    ("fonts", ("dll/win32/shell32/res/icons/39.ico",)),
    ("game-controllers", ("dll/cpl/joy/joy.ico",)),
    ("internet-options", ("dll/cpl/inetcpl/inetcpl.ico",)),
    ("keyboard", ("dll/cpl/main/resources/keyboard.ico",)),
    ("mouse", ("dll/cpl/main/resources/mouse.ico",)),
    ("network-connections", ("dll/shellext/netshell/res/netshell.ico",)),
    ("opengl-configuration", ("dll/cpl/openglcfg/resources/openglcfg.ico",)),
    ("personalization", ("dll/cpl/desk/resources/personalization.ico",)),
    ("phone-and-modem", ("dll/cpl/telephon/resources/applet.ico",)),
    ("power-options", ("dll/cpl/powercfg/resources/ac.ico",)),
    ("printers", ("dll/win32/shell32/res/icons/138.ico",)),
    ("programs-and-features", ("dll/cpl/appwiz/resources/applet.ico",)),
    ("region-and-language", ("dll/cpl/intl/resources/applet.ico",)),
    ("safely-remove-hardware", ("dll/cpl/hotplug/resources/1.ico",)),
    ("sound", ("dll/cpl/mmsys/resources/3004.ico",)),
    ("system", ("dll/cpl/sysdm/resources/applet.ico",)),
    ("taskbar-and-start-menu", ("dll/win32/shell32/res/icons/40.ico",)),
    ("text-services", ("dll/cpl/input/resources/keyboard-shortcuts.ico",)),
    ("user-accounts", ("dll/cpl/usrmgr/resources/applet.ico",)),
    ("wined3d-options", ("dll/cpl/wined3dcfg/resources/wined3dcfg.ico",)),
)


def main():
    assets = Path(__file__).resolve().parent
    root = assets.parents[2]
    write_ico = runpy.run_path(
        str(assets.parent / "user-folders/render-icons.py"))["write_ico"]

    for name, target_paths in TARGETS:
        source_path = assets / (name + ".png")
        with Image.open(source_path) as source:
            image = source.convert("RGBA")
            assert image.size == (512, 512)
            assert image.getchannel("A").getextrema() == (0, 255)
            for target in target_paths:
                write_ico(image, root / target)
                print(f"{name} -> {target}")


if __name__ == "__main__":
    main()
