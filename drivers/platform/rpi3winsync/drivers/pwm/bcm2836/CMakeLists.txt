# PROJECT:     ReactOS
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the unchanged Raspberry Pi Windows PWM driver sources
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

set(BCM2836PWM_SOURCE
    device.cpp
    dma.cpp
    dmaInterrupt.cpp
    driver.cpp
    pwm.cpp
    resource.rc)

add_library(bcm2836pwm MODULE ${BCM2836PWM_SOURCE})
set_module_type(bcm2836pwm kmdfdriver)

get_filename_component(_rpi3winsync_root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
target_include_directories(bcm2836pwm BEFORE PRIVATE
    ${_rpi3winsync_root}/reactos-compat/pwm
    ${_rpi3winsync_root}/reactos-compat
    ${CMAKE_CURRENT_SOURCE_DIR})

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(bcm2836pwm PRIVATE
        -Wno-unknown-pragmas
        -Wno-microsoft-anon-tag
        -Wno-missing-braces)
endif()

target_link_libraries(bcm2836pwm ntoskrnl_vista libcntpr ${PSEH_LIB})
add_importlibs(bcm2836pwm ntoskrnl hal)

add_cd_file(TARGET bcm2836pwm DESTINATION reactos/system32/drivers FOR all)

add_driver_inf(bcm2836pwm "${_rpi3winsync_root}/reactos-compat/pwm/bcm2836pwm.inf")

set_target_properties(bcm2836pwm PROPERTIES FOLDER "Drivers/Platform/Raspberry Pi 3")
