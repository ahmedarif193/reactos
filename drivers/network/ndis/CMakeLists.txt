
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

# NDIS 6 sources are compiled with NDIS620_MINIPORT / NDIS620 so they see
# the NDIS 6.20 view of ndis.h. They live outside the precompiled header
# because the PCH is built at NDIS 5.1 level. The dev-nt6-1 branch splits
# the NDIS 6 work across several files for clarity:
#   60stubs.c   - filter/protocol registration stubs and Phase 4 datapath
#   60nbl.c     - NBL pool allocation
#   60io.c      - MMIO/IO port/DMA/shared memory/interrupts
#   60driver.c  - NdisMRegisterMiniportDriver + AddDevice + attributes
#   60adapter.c - LOGICAL_ADAPTER lifecycle + MiniportInitializeEx
#   60oid.c     - Legacy NDIS 5 OID dispatcher for NDIS 6 adapters
set(NDIS6_SOURCE
    ndis/60stubs.c
    ndis/60nbl.c
    ndis/60io.c
    ndis/60driver.c
    ndis/60adapter.c
    ndis/60oid.c
    ndis/60thunk_tx.c
    ndis/60thunk_rx.c
    ndis/60filter.c
    ndis/60util.c
    include/ndis6_internal.h)
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
