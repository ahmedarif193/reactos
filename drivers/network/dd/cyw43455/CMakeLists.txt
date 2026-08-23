# PROJECT:     ReactOS Broadcom/Cypress CYW43xx Native 802.11 Miniport
# LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
# PURPOSE:     CYW43xx Native Wi-Fi miniport build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

add_definitions(
    -DNDIS620_MINIPORT
    -DNDIS_MINIPORT_DRIVER
    -DNDIS_LEGACY_DRIVER=1
    -DNTDDI_VERSION=0x06010000)

list(APPEND SOURCE
    cyw43455.c
    sdio.c
    chip.c
    fwil.c
    oid.c
    cyw43455.h)

add_library(cyw43455 MODULE ${SOURCE} cyw43455.rc)
target_link_libraries(cyw43455 sdbuslib)
set_module_type(cyw43455 kernelmodedriver)
add_importlibs(cyw43455 ndis ntoskrnl hal)
add_cd_file(TARGET cyw43455 DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(cyw43455 cyw43455.inf)

add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/fw/brcmfmac43430-sdio.bin DESTINATION reactos/system32/drivers/brcm NO_CAB FOR all)
add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/fw/brcmfmac43430-sdio.clm_blob DESTINATION reactos/system32/drivers/brcm NO_CAB FOR all)
add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/fw/brcmfmac43430-sdio.txt DESTINATION reactos/system32/drivers/brcm NO_CAB FOR all)
add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/fw/brcmfmac43455-sdio.bin DESTINATION reactos/system32/drivers/brcm NO_CAB FOR all)
add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/fw/brcmfmac43455-sdio.clm_blob DESTINATION reactos/system32/drivers/brcm NO_CAB FOR all)
add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/fw/brcmfmac43455-sdio.txt DESTINATION reactos/system32/drivers/brcm NO_CAB FOR all)
