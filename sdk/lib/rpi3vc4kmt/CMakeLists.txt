# PROJECT:     ReactOS Raspberry Pi 3 VC4 user-mode support
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x11007)

add_library(rpi3vc4kmt STATIC rpi3vc4kmt.c)
add_dependencies(rpi3vc4kmt psdk)
