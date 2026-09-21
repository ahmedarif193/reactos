# PROJECT:     ReactOS host-native tests
# FILE:        submodules/host-tests/mmcc/nvs/amd64/check-backend.cmake
# PURPOSE:     Check AMD64 memory manager backend structure
#
# SPDX-FileCopyrightText: 2026 Ahmed ARIF
# SPDX-License-Identifier: GPL-3.0-only

if(NOT DEFINED NVS_SOURCE_DIR)
    message(FATAL_ERROR "NVS_SOURCE_DIR must name the NVS implementation directory")
endif()
get_filename_component(NVS_ROOT "${NVS_SOURCE_DIR}" ABSOLUTE)
get_filename_component(KERNEL_ROOT "${NVS_ROOT}/.." ABSOLUTE)
file(GLOB BACKEND "${NVS_ROOT}/arch/amd64/*.c" "${NVS_ROOT}/arch/amd64/*.h")
foreach(SOURCE IN LISTS BACKEND)
    file(STRINGS "${SOURCE}" FORBIDDEN
        REGEX "(^|[^A-Za-z0-9_])(MMPTE|PMMPTE|PTE_BASE|PDE_BASE|PPE_BASE|PXE_BASE|KiIpiSendTbFlush|KiTbFlushPacket)([^A-Za-z0-9_]|$)")
    if(FORBIDDEN)
        message(FATAL_ERROR "AMD64 NVS backend depends on replaced MM glue: ${SOURCE}: ${FORBIDDEN}")
    endif()
endforeach()
foreach(SOURCE cpu.c ipi.c)
    file(STRINGS "${KERNEL_ROOT}/ke/amd64/${SOURCE}" FORBIDDEN
        REGEX "(^|[^A-Za-z0-9_])(KiIpiSendTbFlush|KiTbFlushPacket|KiFlushLocalTb|KI_TB_FLUSH_PACKET|KeFlushCurrentTb|KeFlushEntireTb)([^A-Za-z0-9_]|$)")
    if(FORBIDDEN)
        message(FATAL_ERROR "Replaced AMD64 TLB implementation returned to ke/${SOURCE}: ${FORBIDDEN}")
    endif()
endforeach()
message(STATUS "AMD64 NVS owns PTE encoding and TLB invalidation")
