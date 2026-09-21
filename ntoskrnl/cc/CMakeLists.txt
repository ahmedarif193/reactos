# PROJECT:     ReactOS Cache Manager
# FILE:        ntoskrnl/cc/CMakeLists.txt
# PURPOSE:     Cache manager build configuration
#
# SPDX-FileCopyrightText: 2026 Ahmed ARIF
# SPDX-License-Identifier: GPL-3.0-only

file(GLOB CC_SOURCES engine/*.c api/*.c backing/*.c)

add_library(cc_neutral STATIC ${CC_SOURCES})

target_include_directories(cc_neutral PRIVATE
    ${REACTOS_SOURCE_DIR}/ntoskrnl
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_dependencies(cc_neutral bugcodes xdk)
