# PROJECT:     ReactOS
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the synchronized Raspberry Pi Windows audio driver
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

set(RPIWAV_SOURCE
    adapter.cpp
    basetopo.cpp
    common.cpp
    kshelper.cpp
    EndpointsCommon/mintopo.cpp
    EndpointsCommon/minwavert.cpp
    EndpointsCommon/minwavertstream.cpp
    EndpointsCommon/speakerhptopo.cpp
    rpiwav.rc)

add_library(rpiwav MODULE ${RPIWAV_SOURCE})
set_module_type(rpiwav kmdfdriver)

get_filename_component(_rpi3winsync_root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
target_include_directories(rpiwav BEFORE PRIVATE
    ${_rpi3winsync_root}/reactos-compat/audio
    ${_rpi3winsync_root}/reactos-compat
    ${_rpi3winsync_root}/drivers/pwm/bcm2836
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/EndpointsCommon
    ${CMAKE_CURRENT_SOURCE_DIR}/rpiwav)

target_compile_definitions(rpiwav PRIVATE _USE_WAVERT_)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(rpiwav PRIVATE
        -Wno-unknown-pragmas
        -Wno-microsoft-anon-tag
        -Wno-missing-braces
        -Wno-write-strings)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(rpiwav PRIVATE -fms-compatibility -D__GNUC__=4)
endif()

target_link_libraries(rpiwav ntoskrnl_vista stdunk libcntpr ${PSEH_LIB})
add_importlibs(rpiwav ntoskrnl hal portcls)

add_cd_file(TARGET rpiwav DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(rpiwav "${_rpi3winsync_root}/reactos-compat/audio/rpiwav.inf")

set_target_properties(rpiwav PROPERTIES FOLDER "Drivers/Platform/Raspberry Pi 3")
