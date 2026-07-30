# PROJECT:  ReactOS WDDM Null/Software GPU Miniport
# LICENSE:  GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
# PURPOSE:  Build file for softgpu.sys — configurable WDDM software miniport
#           with a physical scanout provider and software command engine.
# COPYRIGHT: Copyright 2024 ReactOS WDDM Team
#
# Build order in drivers/directx:
#   1. dxgmms1   — no dxgkrnl dependency
#   2. dxgkrnl   — imports ntoskrnl/hal; owns the internal resolver device
#   3. softgpu   — resolves DxgkInitialize through the static displib wrapper

spec2def(softgpu.sys softgpu.spec ADD_IMPORTLIB)

# softgpu MUST compile with the same current header version as dxgkrnl so
# DRIVER_INITIALIZATION_DATA, DXGK_INTERFACE and every DXGKARG_* structure are
# byte-identical. InitData.Version is selected and capped in softgpu.c. Mirror
# dxgkrnl/CMakeLists.txt exactly.
remove_definitions(-D_WIN32_WINNT=${REACTOS_TARGET_NT})
remove_definitions(-DWINVER=${REACTOS_TARGET_NT})
add_definitions(-D_WIN32_WINNT=0x0601 -DWINVER=0x0601)
add_definitions(-DNTDDI_VERSION=0x06010000)    # NTDDI_WIN7

# The global default selector gates out newer ABI fields. Compile the audited
# declaration surface; runtime capability claims remain separately gated.
remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x11007)

list(APPEND SOURCE
    softgpu.c
    platform_common.c
    platform_root.c
    vidpn.c
    dma.c
    wddm2.c)

add_library(softgpu MODULE
    ${SOURCE}
    softgpu.rc
    ${CMAKE_CURRENT_BINARY_DIR}/softgpu.def)

target_compile_definitions(softgpu PRIVATE
    REACTOS_WDDM_TARGET_LEVEL=${REACTOS_WDDM_TARGET_LEVEL}
    REACTOS_WDDM_TARGET_INTERFACE_VERSION=${REACTOS_WDDM_TARGET_INTERFACE_VERSION})

set_module_type(softgpu kernelmodedriver)
target_link_libraries(softgpu displib)
add_importlibs(softgpu ntoskrnl hal)
add_pch(softgpu softgpu.h "${SOURCE}")
add_cd_file(TARGET softgpu DESTINATION reactos/system32/drivers FOR all)
