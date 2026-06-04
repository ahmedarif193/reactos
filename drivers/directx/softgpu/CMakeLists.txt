# PROJECT:  ReactOS WDDM Null/Software GPU Miniport
# LICENSE:  GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
# PURPOSE:  Build file for softgpu.sys — WDDM 1.0 test miniport targeting
#           QEMU STD VGA (PCI VEN_1234&DEV_1111).
# COPYRIGHT: Copyright 2024 ReactOS WDDM Team
#
# Build order in drivers/directx:
#   1. dxgmms1   — no dxgkrnl dependency
#   2. dxgkrnl   — imports ntoskrnl/hal; exports DxgkInitialize
#   3. softgpu   — links DxgkInitialize from dxgkrnl import lib

spec2def(softgpu.sys softgpu.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    softgpu.c
    vidpn.c
    dma.c)

add_library(softgpu MODULE
    ${SOURCE}
    softgpu.rc
    ${CMAKE_CURRENT_BINARY_DIR}/softgpu.def)

set_module_type(softgpu kernelmodedriver)
add_importlibs(softgpu dxgkrnl ntoskrnl hal)
add_pch(softgpu softgpu.h "${SOURCE}")
add_cd_file(TARGET softgpu DESTINATION reactos/system32/drivers FOR all)
add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/softgpu.inf DESTINATION reactos/inf FOR all)
