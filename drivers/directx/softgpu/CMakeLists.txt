# PROJECT:  ReactOS WDDM Null/Software GPU Miniport
# LICENSE:  GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
# PURPOSE:  Build file for softgpu.sys — WDDM 2.0 ABI test miniport with a
#           physical/software engine and no GPU MMU, targeting QEMU STD VGA.
# COPYRIGHT: Copyright 2024 ReactOS WDDM Team
#
# Build order in drivers/directx:
#   1. dxgmms1   — no dxgkrnl dependency
#   2. dxgkrnl   — imports ntoskrnl/hal; exports DxgkInitialize
#   3. softgpu   — links DxgkInitialize from dxgkrnl import lib

spec2def(softgpu.sys softgpu.spec ADD_IMPORTLIB)

# softgpu is a WDDM 2.0 miniport.  It MUST speak the exact same DDI contract as
# dxgkrnl so that DRIVER_INITIALIZATION_DATA, DXGK_INTERFACE and every DXGKARG_*
# structure have byte-identical layouts between the port driver and this
# miniport.  dxgkrnl's DxgkInitialize copies sizeof(DRIVER_INITIALIZATION_DATA)
# (its own, WDDM2-sized) bytes out of the table softgpu passes, so a smaller /
# differently-versioned table on this side reads stack garbage into the WDDM2
# DDI slots.  Mirror dxgkrnl/CMakeLists.txt exactly (lines 10-19).
remove_definitions(-D_WIN32_WINNT=${REACTOS_TARGET_NT})
remove_definitions(-DWINVER=${REACTOS_TARGET_NT})
add_definitions(-D_WIN32_WINNT=0x0601 -DWINVER=0x0601)
add_definitions(-DNTDDI_VERSION=0x06010000)    # NTDDI_WIN7

# The global default DXGKDDI version (0x1052) gates out the WDDM 2.0 ABI fields.
# Raise it to WDDM 2.0 to match dxgkrnl; this does not claim a GPU MMU.
remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x5023)

list(APPEND SOURCE
    softgpu.c
    vidpn.c
    dma.c
    wddm2.c)

add_library(softgpu MODULE
    ${SOURCE}
    softgpu.rc
    ${CMAKE_CURRENT_BINARY_DIR}/softgpu.def)

set_module_type(softgpu kernelmodedriver)
add_importlibs(softgpu dxgkrnl ntoskrnl hal)
add_pch(softgpu softgpu.h "${SOURCE}")
add_cd_file(TARGET softgpu DESTINATION reactos/system32/drivers FOR all)
