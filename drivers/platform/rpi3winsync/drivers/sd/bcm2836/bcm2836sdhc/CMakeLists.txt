# PROJECT:     ReactOS
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the synchronized Raspberry Pi Arasan SD miniport
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

add_library(bcm2836sdhc MODULE bcm2836sdhc.c bcm2836sdhc.rc)
set_module_type(bcm2836sdhc kernelmodedriver)

get_filename_component(_rpi3winsync_root "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
target_include_directories(bcm2836sdhc BEFORE PRIVATE
    ${_rpi3winsync_root}/reactos-compat
    ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(bcm2836sdhc PRIVATE -Wno-unknown-pragmas -Wno-missing-braces)
target_link_libraries(bcm2836sdhc libcntpr ${PSEH_LIB})
add_importlibs(bcm2836sdhc sdport ntoskrnl hal)
add_cd_file(TARGET bcm2836sdhc DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(bcm2836sdhc ${_rpi3winsync_root}/reactos-compat/sd/bcm2836sdhc.inf)
set_target_properties(bcm2836sdhc PROPERTIES FOLDER "Drivers/Platform/Raspberry Pi 3")
