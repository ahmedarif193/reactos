# PROJECT:     ReactOS Raspberry Pi 3 video support
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the Windows-compatible VC4 D3D9 user-mode driver
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x11007)

spec2def(rpi3vc4um.dll rpi3vc4um.spec)

add_library(rpi3vc4um MODULE
    ../softgpuum/softgpuum.c
    rpi3vc4um_video.c
    ${CMAKE_CURRENT_BINARY_DIR}/rpi3vc4um.def)

target_include_directories(rpi3vc4um PRIVATE
    ../softgpuum
    ../rpi3mmal)
set_module_type(rpi3vc4um win32dll ENTRYPOINT DllMain 12)
add_importlibs(rpi3vc4um rpi3mmal msvcrt kernel32 ntdll)
add_dependencies(rpi3vc4um psdk)
add_cd_file(TARGET rpi3vc4um DESTINATION reactos/system32 FOR all)
set_target_properties(rpi3vc4um PROPERTIES
    FOLDER "Drivers/Platform/Raspberry Pi 3")
