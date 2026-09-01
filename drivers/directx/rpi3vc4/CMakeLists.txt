# PROJECT:     ReactOS Raspberry Pi 3 VC4 WDDM miniport
# LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
# PURPOSE:     Build the BCM2837 platform provider over the common KMDOD core

set(SOFTGPU_SOURCE_DIR ${REACTOS_SOURCE_DIR}/drivers/directx/softgpu)

spec2def(rpi3vc4.sys ../softgpu/softgpu.spec ADD_IMPORTLIB)

remove_definitions(-D_WIN32_WINNT=${REACTOS_TARGET_NT})
remove_definitions(-DWINVER=${REACTOS_TARGET_NT})
add_definitions(-D_WIN32_WINNT=0x0601 -DWINVER=0x0601)
add_definitions(-DNTDDI_VERSION=0x06010000)

remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x11007)

include_directories(${SOFTGPU_SOURCE_DIR})

list(APPEND SOURCE
    ${SOFTGPU_SOURCE_DIR}/softgpu.c
    ${SOFTGPU_SOURCE_DIR}/platform_common.c
    ${SOFTGPU_SOURCE_DIR}/vidpn.c
    ${SOFTGPU_SOURCE_DIR}/pointer.c
    ${SOFTGPU_SOURCE_DIR}/dma.c
    ${SOFTGPU_SOURCE_DIR}/wddm2.c
    rpi3vc4_hvs.c
    rpi3vc4_v3d.c
    rpi3vc4_validate.c
    vc4_validate.c
    vc4_validate_shaders.c
    vc4_render_cl.c
    platform_rpi3.c)

add_library(rpi3vc4 MODULE
    ${SOURCE}
    rpi3vc4.rc
    ${CMAKE_CURRENT_BINARY_DIR}/rpi3vc4.def)

target_compile_definitions(rpi3vc4 PRIVATE
    SOFTGPU_PLATFORM_HARDWARE_3D=1
    SOFTGPU_PLATFORM_PRIVATE_ESCAPE=1
    REACTOS_WDDM_TARGET_LEVEL=${REACTOS_WDDM_TARGET_LEVEL}
    REACTOS_WDDM_TARGET_INTERFACE_VERSION=${REACTOS_WDDM_TARGET_INTERFACE_VERSION})

set_module_type(rpi3vc4 kernelmodedriver)
target_link_libraries(rpi3vc4 displib)
add_importlibs(rpi3vc4 ntoskrnl hal)
add_pch(rpi3vc4 ${SOFTGPU_SOURCE_DIR}/softgpu.h "${SOURCE}")
add_cd_file(TARGET rpi3vc4 DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(rpi3vc4 rpi3vc4.inf)
add_registry_inf(rpi3vc4_reg.inf)
