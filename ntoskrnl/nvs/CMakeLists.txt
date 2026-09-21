# PROJECT:     ReactOS NT Virtual Memory Subsystem
# FILE:        ntoskrnl/nvs/CMakeLists.txt
# PURPOSE:     Virtual memory manager build configuration
#
# SPDX-FileCopyrightText: 2026 Ahmed ARIF
# SPDX-License-Identifier: GPL-3.0-only

set(NVS_ARCH_DIR ${CMAKE_CURRENT_SOURCE_DIR}/arch/${ARCH})

if(NOT EXISTS ${NVS_ARCH_DIR}/descriptor.c OR NOT EXISTS ${NVS_ARCH_DIR}/archdef.h)
    message(FATAL_ERROR
        "nvs: ${ARCH} has no backend. Create ntoskrnl/nvs/arch/${ARCH}/ with "
        "descriptor.c, archdef.h, and implementations of the architecture interface.")
endif()

file(GLOB NVS_CORE_SOURCES
    core/*.c vad/*.c fault/*.c section/*.c ws/*.c pagefile/*.c vm/*.c sys/*.c proc/*.c
    pool/va/*.c pool/heap/*.c pool/track/*.c)

file(GLOB NVS_NT_SOURCES nt/*.c pool/backing/mmbacking.c)
file(GLOB NVS_ARCH_SOURCES ${NVS_ARCH_DIR}/*.c)

add_library(nvs_neutral STATIC ${NVS_CORE_SOURCES} ${NVS_NT_SOURCES} ${NVS_ARCH_SOURCES})

target_include_directories(nvs_neutral PRIVATE
    ${REACTOS_SOURCE_DIR}/sdk/lib/apisets
    ${REACTOS_SOURCE_DIR}/ntoskrnl
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_dependencies(nvs_neutral bugcodes xdk)
