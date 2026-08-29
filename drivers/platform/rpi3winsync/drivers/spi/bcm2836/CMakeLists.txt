# PROJECT:     ReactOS
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the synchronized Raspberry Pi SPI controller driver
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

add_library(bcmspi MODULE controller.cpp device.cpp driver.cpp resource.rc)
set_module_type(bcmspi kmdfdriver)

get_filename_component(_rpi3winsync_root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
target_include_directories(bcmspi BEFORE PRIVATE
    ${_rpi3winsync_root}/reactos-compat
    ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(bcmspi PRIVATE -Wno-unknown-pragmas -Wno-missing-braces)
target_link_libraries(bcmspi spbcxstubs ntoskrnl_vista libcntpr ${PSEH_LIB})
add_importlibs(bcmspi ntoskrnl hal)
add_cd_file(TARGET bcmspi DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(bcmspi ${_rpi3winsync_root}/reactos-compat/spi/bcmspi.inf)
set_target_properties(bcmspi PROPERTIES FOLDER "Drivers/Platform/Raspberry Pi 3")
