
#
# PROJECT:     ReactOS Display Driver Model - Win32k/dxgkrnl Bridge
# LICENSE:     MIT (https://spdx.org/licenses/MIT)
# PURPOSE:     Build rules for the static WDDM bridge library
# COPYRIGHT:   Copyright 2025 ReactOS Contributors
#
# This library is linked statically into win32k.sys. WDDM builds own the full
# bridge and use the selected WDDM DDI layouts. XPDM builds retain only the
# link surface needed by the always-built win32k thunk source; those entry
# points truthfully report that WDDM is unavailable.
#

if(REACTOS_USE_WDDM)
    remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
    add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x11007)

    list(APPEND SOURCE
        wddm_bridge.c
        d3dkmt_stubs.c
        ntgdi_dxg.c)
else()
    list(APPEND SOURCE
        xpdm_stubs.c)
endif()

add_library(wddm_bridge STATIC ${SOURCE})
add_dependencies(wddm_bridge bugcodes xdk)

# The bridge participates in the win32k build environment: it needs the same
# include paths and definitions as the rest of win32k.
target_include_directories(wddm_bridge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..           # win32ss root  -> pch.h / win32k.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include   # win32ss/include -> ntwin32.h, ntusrtyp.h
    ${REACTOS_SOURCE_DIR}/ntoskrnl/include
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/drivers)

# win32k already imports ntoskrnl; the static lib is linked into win32k so it
# picks up all ntoskrnl symbols through win32k's import table.
# Do NOT add target_link_libraries(wddm_bridge ntoskrnl) here — ntoskrnl is a
# kernel module and adding it as a cmake dep would pull in the entire kernel
# build chain (including ntdll) as a build prerequisite of win32k.
