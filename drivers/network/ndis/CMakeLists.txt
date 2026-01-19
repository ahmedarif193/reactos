
include_directories(BEFORE include)
spec2def(ndis.sys ndis.spec ADD_IMPORTLIB)

add_definitions(
    -DNDIS_WRAPPER
    -DNDIS51
    -DNDIS51_MINIPORT
    -DNDIS60
    -DNDIS60_MINIPORT
    -DNDIS_LEGACY_DRIVER
    -DNDIS_LEGACY_MINIPORT
    -DNDIS_LEGACY_PROTOCOL
    -DNDIS_MINIPORT_DRIVER)

list(APPEND SOURCE
    ndis/30stubs.c
    ndis/40stubs.c
    ndis/50stubs.c
    ndis/buffer.c
    ndis/cl.c
    ndis/cm.c
    ndis/co.c
    ndis/config.c
    ndis/control.c
    ndis/efilter.c
    ndis/hardware.c
    ndis/io.c
    ndis/main.c
    ndis/memory.c
    ndis/miniport.c
    # NDIS 6.x miniport files - enabled for NDIS 6.x support
    ndis/miniport6.c
    ndis/miniport6_int.c
    ndis/miniport6_nbl.c
    ndis/miniport6_oid.c
    ndis/miniport6_state.c
    ndis/misc.c
    ndis/protocol.c
    ndis/string.c
    ndis/time.c
    include/ndissys.h)

add_library(ndis MODULE
    ${SOURCE}
    ndis.rc
    ${CMAKE_CURRENT_BINARY_DIR}/ndis.def)

set_module_type(ndis kernelmodedriver)
add_importlibs(ndis ntoskrnl hal)
add_pch(ndis include/ndissys.h SOURCE)
add_cd_file(TARGET ndis DESTINATION reactos/system32/drivers FOR all)
