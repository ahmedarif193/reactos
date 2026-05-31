
#
# PROJECT:     ReactOS Network Drivers
# LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
# FILE:        drivers/network/dd/CMakeLists.txt
# PURPOSE:     Network driver build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_subdirectory(dc21x4)
add_subdirectory(e1000)
# e1000e is the NDIS 6.20 PCIe variant; only built at NT 6.0 (Vista) or
# later because it depends on NET_BUFFER_LIST datapath and the NDIS 6 stub
# exports in drivers/network/ndis/ndis/60stubs.c. Below Vista the legacy
# e1000 (NDIS 5.0) driver covers the same hardware family.
math(EXPR _E1000E_NT_NUM "${REACTOS_TARGET_NT} + 0")
if(_E1000E_NT_NUM GREATER_EQUAL 1536)
    add_subdirectory(e1000e)
endif()
if((ARCH STREQUAL "arm64") AND (_E1000E_NT_NUM GREATER_EQUAL 1536))
    add_subdirectory(rp1gem)
endif()
add_subdirectory(ne2000)
add_subdirectory(netkvm)
if(ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64")
    add_subdirectory(nvnet)
endif()
add_subdirectory(pcnet)
add_subdirectory(rtl8139)
