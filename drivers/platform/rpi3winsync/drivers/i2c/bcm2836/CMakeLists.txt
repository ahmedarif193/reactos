# PROJECT:     ReactOS
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the synchronized Raspberry Pi I2C controller driver
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

add_library(bcmi2c MODULE device.cpp driver.cpp resource.rc)
set_module_type(bcmi2c kmdfdriver)

get_filename_component(_rpi3winsync_root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
target_include_directories(bcmi2c BEFORE PRIVATE
    ${_rpi3winsync_root}/reactos-compat
    ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(bcmi2c PRIVATE -Wno-unknown-pragmas -Wno-missing-braces)
target_link_libraries(bcmi2c spbcxstubs ntoskrnl_vista libcntpr ${PSEH_LIB})
add_importlibs(bcmi2c ntoskrnl hal)
add_cd_file(TARGET bcmi2c DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(bcmi2c ${_rpi3winsync_root}/reactos-compat/i2c/bcmi2c.inf)
set_target_properties(bcmi2c PROPERTIES FOLDER "Drivers/Platform/Raspberry Pi 3")
