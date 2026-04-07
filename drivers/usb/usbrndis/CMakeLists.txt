#
# ReactOS USB RNDIS Network Driver
#
# Uses NDIS 6.20 with NET_BUFFER_LIST send/receive. On the dev-nt6-1 branch
# the NDIS 6 stubs live in drivers/network/ndis/ndis/60stubs.c — this
# driver links against those symbols and the usbrndis/ndis6_compat.h
# shim fills in anything that the mainline ndis.h is still missing.
#
# NDIS_LEGACY_DRIVER=1 is needed because a few function declarations in
# ndis.h still use the legacy PNDIS_PACKET / PNDIS_REQUEST types without
# proper version gating. The driver itself does not call any legacy API.
#

add_definitions(
    -DNDIS620_MINIPORT
    -DNDIS_MINIPORT_DRIVER
    -DNDIS_LEGACY_DRIVER=1
    -DNTDDI_VERSION=0x06010000
    -D_WIN32_WINNT=0x0601)

list(APPEND SOURCE
    usbrndis.c
    rndisusb.c
    rndisctl.c
    rndisdata.c
    rndisoid.c)

list(APPEND PCH_SKIP_SOURCE
    guid.c)

add_library(usbrndis MODULE
    ${SOURCE}
    ${PCH_SKIP_SOURCE}
    usbrndis.rc)

set_module_type(usbrndis kernelmodedriver)
target_link_libraries(usbrndis ${PSEH_LIB})

# Suppress warnings during development
target_compile_options(usbrndis PRIVATE
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-unused-but-set-variable)

add_importlibs(usbrndis ndis ntoskrnl hal usbd)
add_pch(usbrndis usbrndis.h "${PCH_SKIP_SOURCE}")
add_cd_file(TARGET usbrndis DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(usbrndis usbrndis.inf)
