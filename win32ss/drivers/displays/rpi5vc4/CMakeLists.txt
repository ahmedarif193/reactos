# PROJECT:     ReactOS Raspberry Pi 5 XPDM graphics stack
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Raspberry Pi 5 XPDM display driver build rules
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

list(APPEND RPI5VC4_DISPLAY_SOURCE
    ../framebuf/enable.c
    ../framebuf/palette.c
    ../framebuf/pointer.c
    ../framebuf/screen.c
    ../framebuf/shadow.c
    ../framebuf/shadow_hooks.c
    ../framebuf/surface.c
    ../framebuf/framebuf.h
    rpi5_driver.c
    rpi5_gdi.c
    rpi5_gdi.h
    rpi5_escape.c)

add_library(rpi5vc4disp MODULE
    ${RPI5VC4_DISPLAY_SOURCE}
    rpi5vc4disp.rc)

target_include_directories(rpi5vc4disp PRIVATE ../framebuf)
set_module_type(rpi5vc4disp kerneldll ENTRYPOINT DrvEnableDriver 12)
target_link_libraries(rpi5vc4disp libcntpr)
add_importlibs(rpi5vc4disp win32k)
add_dependencies(rpi5vc4disp psdk)
add_cd_file(TARGET rpi5vc4disp DESTINATION reactos/system32 FOR all)
