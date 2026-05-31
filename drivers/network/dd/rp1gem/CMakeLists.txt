#
# PROJECT:     ReactOS RP1 Ethernet Driver
# LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
# FILE:        drivers/network/dd/rp1gem/CMakeLists.txt
# PURPOSE:     RP1 Ethernet miniport build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_definitions(
    -DNDIS620_MINIPORT
    -DNDIS_MINIPORT_DRIVER
    -DNDIS_LEGACY_DRIVER=1
    -DNDIS_LEGACY_MINIPORT=1
    -DNTDDI_VERSION=0x06010000)

list(APPEND SOURCE
    rp1gem.c
    rp1gem.h)

add_library(rp1gem MODULE ${SOURCE} rp1gem.rc)
add_pch(rp1gem rp1gem.h SOURCE)
set_module_type(rp1gem kernelmodedriver)
add_importlibs(rp1gem ndis ntoskrnl hal)
add_cd_file(TARGET rp1gem DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(rp1gem netrp1gem.inf)
