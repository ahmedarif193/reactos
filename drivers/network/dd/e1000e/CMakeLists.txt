#
# E1000E — Intel PRO/1000 PCIe NDIS 6.20 driver
#
# This is a separate driver from the legacy ../e1000/ NDIS 5.0 variant.
# It targets NT 6.0+ and uses the NDIS 6 NET_BUFFER_LIST datapath, MSI-X,
# multi-queue, RSS, offload, and the NDIS 6 power management framework.
#
# The legacy ../e1000/ driver is left untouched and continues to ship for
# NT 5.x and as a fallback when MSI-X / NET_BUFFER_LIST aren't usable.
#
# Both drivers can coexist in the build; PnP will pick one at install time
# based on the .inf precedence.
#
# Imported from the dev branch on the dev-nt6-1 branch (NT 5.2 -> NT 6.1
# upgrade) by Ahmed ARIF as part of the NDIS 6 driver tree expansion.
#

add_definitions(
    -DNDIS620_MINIPORT
    -DNDIS_MINIPORT_DRIVER
    -DNDIS_LEGACY_DRIVER=1
    -DNDIS_LEGACY_MINIPORT=1
    -DNTDDI_VERSION=0x06010000
    -D_WIN32_WINNT=0x0601)

list(APPEND SOURCE
    init.c
    hardware.c
    interrupt_ndis6.c
    send_ndis6.c
    receive_ndis6.c
    oid_ndis6.c
    control_ndis6.c
    offload_ndis6.c
    rss_ndis6.c
    power_ndis6.c
    e1000.h
    e1000hw.h
    ndis6_compat.h)

add_library(e1000e MODULE ${SOURCE} e1000e.rc)
add_pch(e1000e e1000.h SOURCE)
set_module_type(e1000e kernelmodedriver)

target_compile_options(e1000e PRIVATE
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-address
    -Wno-enum-compare)

add_importlibs(e1000e ndis ntoskrnl hal)
add_cd_file(TARGET e1000e DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(e1000e nete1000e.inf)
