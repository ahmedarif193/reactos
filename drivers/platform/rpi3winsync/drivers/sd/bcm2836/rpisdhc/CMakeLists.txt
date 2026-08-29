# PROJECT:     ReactOS
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the synchronized Raspberry Pi SDHOST miniport
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

add_library(rpisdhc MODULE rpisdhc.cpp sdhclogging.cpp rpisdhc.rc)
set_module_type(rpisdhc kernelmodedriver)

get_filename_component(_rpi3winsync_root "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
target_include_directories(rpisdhc BEFORE PRIVATE
    ${_rpi3winsync_root}/reactos-compat
    ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(rpisdhc PRIVATE -Wno-unknown-pragmas -Wno-missing-braces)
target_link_libraries(rpisdhc libcntpr ${PSEH_LIB})
add_importlibs(rpisdhc sdport ntoskrnl hal)
add_cd_file(TARGET rpisdhc DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(rpisdhc ${_rpi3winsync_root}/reactos-compat/sd/rpisdhc.inf)
set_target_properties(rpisdhc PROPERTIES FOLDER "Drivers/Platform/Raspberry Pi 3")
