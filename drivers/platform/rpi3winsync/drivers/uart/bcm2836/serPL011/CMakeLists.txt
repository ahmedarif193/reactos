# PROJECT:     ReactOS
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the synchronized Raspberry Pi PL011 serial driver
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

get_filename_component(_rpi3winsync_root "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)

add_library(SerPL011 MODULE
    ${_rpi3winsync_root}/drivers/shared/acpi/acpiutil.cpp
    PL011common.cpp
    PL011device.cpp
    PL011driver.cpp
    PL011hw.cpp
    PL011interrupt.cpp
    PL011ioctl.cpp
    pl011rx.cpp
    PL011tx.cpp
    PL011uart.cpp
    PL011.rc)
set_module_type(SerPL011 kmdfdriver)
target_include_directories(SerPL011 BEFORE PRIVATE
    ${_rpi3winsync_root}/reactos-compat
    ${_rpi3winsync_root}/drivers/include
    ${_rpi3winsync_root}/drivers/shared/acpi
    ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(SerPL011 PRIVATE -Wno-unknown-pragmas -Wno-missing-braces)
target_link_libraries(SerPL011 sercx2stubs ntoskrnl_vista libcntpr ${PSEH_LIB})
add_importlibs(SerPL011 ntoskrnl hal)
add_cd_file(TARGET SerPL011 DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(SerPL011 ${_rpi3winsync_root}/reactos-compat/serpl011/SerPL011.inf)
set_target_properties(SerPL011 PROPERTIES FOLDER "Drivers/Platform/Raspberry Pi 3")
