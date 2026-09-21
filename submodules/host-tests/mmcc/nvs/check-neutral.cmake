# PROJECT:     ReactOS host-native tests
# FILE:        submodules/host-tests/mmcc/nvs/check-neutral.cmake
# PURPOSE:     Check architecture neutrality of shared memory manager code
#
# SPDX-FileCopyrightText: 2026 Ahmed ARIF
# SPDX-License-Identifier: GPL-3.0-only

if(NOT DEFINED NVS_SOURCE_DIR)
    message(FATAL_ERROR "NVS_SOURCE_DIR must name the NVS implementation directory")
endif()

file(GLOB_RECURSE NVS_NEUTRAL_SOURCES
    ${NVS_SOURCE_DIR}/core/*.c ${NVS_SOURCE_DIR}/pool/*.c ${NVS_SOURCE_DIR}/vad/*.c
    ${NVS_SOURCE_DIR}/fault/*.c ${NVS_SOURCE_DIR}/section/*.c ${NVS_SOURCE_DIR}/ws/*.c
    ${NVS_SOURCE_DIR}/pagefile/*.c ${NVS_SOURCE_DIR}/vm/*.c
    ${NVS_SOURCE_DIR}/sys/*.c ${NVS_SOURCE_DIR}/proc/*.c
    ${NVS_SOURCE_DIR}/include/*.h)

set(NVS_BANNED "_M_IX86|_M_AMD64|_M_ARM64|_M_ARM|_M_PPC|_M_MIPS|_MI_PAGING_LEVELS|__aarch64__|__x86_64__|__riscv")
set(NVS_VIOLATIONS "")

foreach(SOURCE ${NVS_NEUTRAL_SOURCES})
    file(STRINGS ${SOURCE} HITS REGEX "${NVS_BANNED}")
    if(HITS)
        file(RELATIVE_PATH REL ${NVS_SOURCE_DIR} ${SOURCE})
        foreach(HIT ${HITS})
            string(APPEND NVS_VIOLATIONS "\n    ${REL}: ${HIT}")
        endforeach()
    endif()
endforeach()

if(NOT NVS_VIOLATIONS STREQUAL "")
    message(FATAL_ERROR
        "nvs: the neutral core names an architecture.${NVS_VIOLATIONS}\n"
        "Architecture shape belongs in arch/<ARCH>/archdef.h and architecture "
        "behaviour behind a hook in include/miarch.h.")
endif()

message(STATUS "nvs: neutral core verified, no architecture named outside arch/")
