#
# ReactOS Native WiFi (802.11) intermediate driver - nwifi.sys
# NDIS 6.20 IM driver: upper virtual 802.3 miniport + lower dot11 protocol.
#

add_definitions(
    -DNDIS620_MINIPORT
    -DNDIS_MINIPORT_DRIVER
    -DNDIS_LEGACY_DRIVER=1
    -DNTDDI_VERSION=0x06010000)

# WPA2-PSK (RSNA) supplicant static library (rsna_supplicant.h)
include_directories(${REACTOS_SOURCE_DIR}/sdk/lib/drivers/rsna_supplicant)

list(APPEND SOURCE
    nwifi.c
    protocol.c
    miniport.c
    datapath.c
    oid.c
    msm.c
    control.c
    supplicant.c
    nwifi.h)

add_library(nwifi MODULE
    ${SOURCE}
    nwifi.rc)

set_module_type(nwifi kernelmodedriver)
target_link_libraries(nwifi ${PSEH_LIB} rsna_supplicant)

add_importlibs(nwifi ndis ntoskrnl hal)
add_pch(nwifi nwifi.h "")
add_cd_file(TARGET nwifi DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(nwifi nwifi.inf)
