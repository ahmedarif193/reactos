# PROJECT:     ReactOS NT Virtual Memory Subsystem
# FILE:        ntoskrnl/nvs/check-standalone.cmake
# PURPOSE:     Check virtual memory manager source isolation
#
# SPDX-FileCopyrightText: 2026 Ahmed ARIF
# SPDX-License-Identifier: GPL-3.0-only

function(nvs_assert_standalone)
    foreach(SOURCE IN LISTS ARGN)
        get_filename_component(ABSOLUTE_SOURCE "${SOURCE}" ABSOLUTE)
        file(TO_CMAKE_PATH "${ABSOLUTE_SOURCE}" NORMALIZED_SOURCE)
        if(NORMALIZED_SOURCE MATCHES "/(vmm_backup|cc_backup|ARM3|arm3)/" OR
           NORMALIZED_SOURCE MATCHES "/ntoskrnl/(mm|vmm)/" OR
           NORMALIZED_SOURCE MATCHES "/nvs/pool/backing/pfnbacking\\.c$")
            message(FATAL_ERROR "NVS: legacy implementation in source list: ${SOURCE}")
        endif()
        if(EXISTS "${ABSOLUTE_SOURCE}" AND
           NORMALIZED_SOURCE MATCHES "/ntoskrnl/(nvs|cc)/.*\\.[ch]$")
            file(STRINGS "${ABSOLUTE_SOURCE}" LEGACY_INCLUDES
                REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"]([^>\"]*/)?(vmm_backup|cc_backup|ARM3|arm3|mm|vmm)/")
            if(LEGACY_INCLUDES)
                message(FATAL_ERROR "NVS: legacy include in ${SOURCE}: ${LEGACY_INCLUDES}")
            endif()
        endif()
    endforeach()
endfunction()

if(DEFINED NVS_CHECK_SOURCES)
    nvs_assert_standalone(${NVS_CHECK_SOURCES})
endif()

if(DEFINED NVS_KERNEL_ROOT)
    file(GLOB_RECURSE NVS_CHECK_TREE_SOURCES
        "${NVS_KERNEL_ROOT}/nvs/*.c" "${NVS_KERNEL_ROOT}/nvs/*.h"
        "${NVS_KERNEL_ROOT}/cc/*.c" "${NVS_KERNEL_ROOT}/cc/*.h")
    nvs_assert_standalone(${NVS_CHECK_TREE_SOURCES})
    message(STATUS "NVS: no retired implementation or includes")
endif()
