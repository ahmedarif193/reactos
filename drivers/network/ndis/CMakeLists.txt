
include_directories(BEFORE include)
spec2def(ndis.sys ndis.spec ADD_IMPORTLIB)

add_definitions(
    -DNDIS_WRAPPER
    -DNDIS51
    -DNDIS51_MINIPORT
    -DNDIS_MINIPORT_DRIVER
    -DNDIS_LEGACY_DRIVER=1
    -DNDIS_LEGACY_MINIPORT=1
    -DNDIS_LEGACY_PROTOCOL=1)

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
    ndis/misc.c
    ndis/protocol.c
    ndis/string.c
    ndis/time.c
    include/ndissys.h)

# NDIS 6 stubs are compiled with NDIS620_MINIPORT / NDIS620 so they see the
# NDIS 6.20 view of ndis.h. They live outside the precompiled header because
# the PCH is built at NDIS 5.1 level.
set(NDIS6_SOURCE ndis/60stubs.c)
set_source_files_properties(${NDIS6_SOURCE} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

add_library(ndis MODULE
    ${SOURCE}
    ${NDIS6_SOURCE}
    ndis.rc
    ${CMAKE_CURRENT_BINARY_DIR}/ndis.def)

set_module_type(ndis kernelmodedriver)
add_importlibs(ndis ntoskrnl hal)
add_pch(ndis include/ndissys.h SOURCE)
add_cd_file(TARGET ndis DESTINATION reactos/system32/drivers FOR all)
