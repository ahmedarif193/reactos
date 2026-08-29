# PROJECT:     ReactOS
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the synchronized Raspberry Pi auxiliary SPI driver
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

add_library(bcmauxspi MODULE bcmauxspi.cpp resource.rc)
set_module_type(bcmauxspi kmdfdriver)

get_filename_component(_rpi3winsync_root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
target_include_directories(bcmauxspi BEFORE PRIVATE
    ${_rpi3winsync_root}/reactos-compat
    ${_rpi3winsync_root}/drivers/mailbox/bcm2836
    ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(bcmauxspi PRIVATE -Wno-unknown-pragmas -Wno-missing-braces)
target_link_libraries(bcmauxspi spbcxstubs ntoskrnl_vista libcntpr ${PSEH_LIB})
add_importlibs(bcmauxspi ntoskrnl hal)
add_cd_file(TARGET bcmauxspi DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(bcmauxspi ${_rpi3winsync_root}/reactos-compat/spi/bcmauxspi.inf)
set_target_properties(bcmauxspi PROPERTIES FOLDER "Drivers/Platform/Raspberry Pi 3")
