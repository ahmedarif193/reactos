# PROJECT:     ReactOS Raspberry Pi 3 video support
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     VideoCore H.264 Media Foundation encoder
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

spec2def(rpi3h264enc.dll rpi3h264enc.spec ADD_IMPORTLIB)

add_library(rpi3h264enc MODULE
    rpi3h264enc.c
    ${CMAKE_CURRENT_BINARY_DIR}/rpi3h264enc.def)

add_idl_reg_scripts(rpi3h264enc registry rpi3h264enc.idl)
set_module_type(rpi3h264enc win32dll)
target_link_libraries(rpi3h264enc mfuuid wmcodecdspuuid uuid wine_dll_register)
add_importlibs(rpi3h264enc rpi3mmal combase mfplat oleaut32 msvcrt kernel32 ntdll)
add_cd_file(TARGET rpi3h264enc DESTINATION reactos/system32 FOR all)
set_target_properties(rpi3h264enc PROPERTIES FOLDER "Drivers/Platform/Raspberry Pi 3")
