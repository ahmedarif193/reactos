# PROJECT:     ReactOS Raspberry Pi 3 video support
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     VideoCore MMAL H.264 codec transport
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

spec2def(rpi3mmal.dll rpi3mmal.spec ADD_IMPORTLIB)

add_library(rpi3mmal MODULE
    rpi3mmal.c
    ${CMAKE_CURRENT_BINARY_DIR}/rpi3mmal.def)

set_module_type(rpi3mmal win32dll ENTRYPOINT 0)
target_include_directories(rpi3mmal PRIVATE
    ${REACTOS_SOURCE_DIR}/drivers/platform/rpi3winsync/drivers/misc/userland
    ${REACTOS_SOURCE_DIR}/drivers/platform/rpi3winsync/drivers/misc/userland/interface/vcos/win32)
target_compile_definitions(rpi3mmal PRIVATE WIN32 __REACTOS__)
add_importlibs(rpi3mmal kernel32 msvcrt ntdll)
add_cd_file(TARGET rpi3mmal DESTINATION reactos/system32 FOR all)
set_target_properties(rpi3mmal PROPERTIES FOLDER "Drivers/Platform/Raspberry Pi 3")
