# PROJECT:     ReactOS
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the unchanged Raspberry Pi Windows mini-UART driver sources
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

set(PI_MINIUART_SOURCE
    error.c
    flush.c
    immediat.c
    initunlo.c
    ioctl.c
    isr.c
    modmflow.c
    openclos.c
    pnp.c
    power.c
    purge.c
    qsfile.c
    read.c
    registry.c
    utils.c
    waitmask.c
    wmi.c
    write.c
    serial.rc)

add_library(pi_miniuart MODULE ${PI_MINIUART_SOURCE})
set_module_type(pi_miniuart kmdfdriver)

get_filename_component(_rpi3winsync_root "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
target_include_directories(pi_miniuart BEFORE PRIVATE
    ${_rpi3winsync_root}/reactos-compat/miniuart
    ${_rpi3winsync_root}/reactos-compat
    ${_rpi3winsync_root}/drivers/mailbox/bcm2836
    ${CMAKE_CURRENT_SOURCE_DIR})

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(pi_miniuart PRIVATE
        -Wno-unknown-pragmas
        -Wno-missing-braces)
endif()

target_link_libraries(pi_miniuart ntoskrnl_vista rtlver libcntpr ${PSEH_LIB})
add_importlibs(pi_miniuart hal ntoskrnl wmilib)

add_cd_file(TARGET pi_miniuart DESTINATION reactos/system32/drivers FOR all)

add_driver_inf(pi_miniuart "${_rpi3winsync_root}/reactos-compat/miniuart/pi_miniuart.inf")

set_target_properties(pi_miniuart PROPERTIES FOLDER "Drivers/Platform/Raspberry Pi 3")
