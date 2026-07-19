
#
# PROJECT:     ReactOS Display Driver Model - Win32k/dxgkrnl Bridge
# LICENSE:     MIT (https://spdx.org/licenses/MIT)
# PURPOSE:     Build rules for the static WDDM bridge library
# COPYRIGHT:   Copyright 2025 ReactOS Contributors
#
# This library is linked statically into win32k.sys. It owns:
#   wddm_bridge.c   - kernel-to-kernel channel to \Device\DxgKrnl
#   d3dkmt_stubs.c  - D3DKMT* wrapper functions that will route through
#                     WddmBridgeSendIoctl once the IOCTL interface is defined
#   ntgdi_dxg.c     - NtGdiDdDDI* -> D3DKMT* thin adapters, documenting the
#                     full call path from user-mode syscall to bridge
#

remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x10004)

list(APPEND SOURCE
    wddm_bridge.c
    d3dkmt_stubs.c
    ntgdi_dxg.c)

add_library(wddm_bridge STATIC ${SOURCE})
add_dependencies(wddm_bridge xdk)

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
