# PROJECT:     ReactOS
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the synchronized Raspberry Pi GPIO controller driver
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

add_library(bcmgpio MODULE BcmGpio.cpp bcmgpio.rc)
set_module_type(bcmgpio kmdfdriver)

get_filename_component(_rpi3winsync_root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
target_include_directories(bcmgpio BEFORE PRIVATE
    ${_rpi3winsync_root}/reactos-compat
    ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(bcmgpio PRIVATE -Wno-unknown-pragmas -Wno-missing-braces)
target_link_libraries(bcmgpio msgpioclxstub ntoskrnl_vista libcntpr ${PSEH_LIB})
add_importlibs(bcmgpio ntoskrnl hal)
add_cd_file(TARGET bcmgpio DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(bcmgpio ${_rpi3winsync_root}/reactos-compat/gpio/bcmgpio.inf)
set_target_properties(bcmgpio PROPERTIES FOLDER "Drivers/Platform/Raspberry Pi 3")
