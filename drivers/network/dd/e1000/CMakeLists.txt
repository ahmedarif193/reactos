
#
# E1000 Driver Build Configuration
#
# Using NDIS 6.20 with NET_BUFFER_LIST send/receive, MSI-X support,
# and multi-queue adapter structure.
#

add_definitions(
    -DNDIS620_MINIPORT
    -DNDIS_MINIPORT_DRIVER)

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
    debug.c
    e1000.h
    e1000hw.h
    ndis6_compat.h
    debug.h)

add_library(e1000 MODULE ${SOURCE} e1000.rc)
add_pch(e1000 e1000.h SOURCE)
set_module_type(e1000 kernelmodedriver)

# Suppress warnings in e1000 driver (work in progress)
target_compile_options(e1000 PRIVATE
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-address
    -Wno-enum-compare)
add_importlibs(e1000 ndis ntoskrnl hal)
add_cd_file(TARGET e1000 DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(e1000 nete1000.inf)
