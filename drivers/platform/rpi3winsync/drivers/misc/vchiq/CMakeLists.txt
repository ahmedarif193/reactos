# PROJECT:     ReactOS
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the Raspberry Pi VCHIQ kernel driver
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

set(VCHIQ_SOURCE
    device.c
    driver.c
    file.c
    init.c
    interrupt.c
    ioctl.c
    memory.c
    slots.c
    trace.c
    transfer.c)

add_library(vchiq MODULE ${VCHIQ_SOURCE})
set_module_type(vchiq kmdfdriver)

target_include_directories(vchiq BEFORE PRIVATE
    ${_rpi3winsync_root}/reactos-compat/vchiq
    ${_rpi3winsync_root}/reactos-compat
    ${_rpi3winsync_root}/drivers/mailbox/bcm2836
    ${CMAKE_CURRENT_SOURCE_DIR})

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(vchiq PRIVATE
        -Wno-microsoft-anon-tag
        -Wno-missing-braces)
endif()

target_link_libraries(vchiq ntoskrnl_vista libcntpr ${PSEH_LIB})
add_importlibs(vchiq ntoskrnl hal)

add_cd_file(TARGET vchiq DESTINATION reactos/system32/drivers FOR all)

add_driver_inf(vchiq "${_rpi3winsync_root}/reactos-compat/vchiq/vchiq.inf")

set_target_properties(vchiq PROPERTIES FOLDER "Drivers/Platform/Raspberry Pi 3")
