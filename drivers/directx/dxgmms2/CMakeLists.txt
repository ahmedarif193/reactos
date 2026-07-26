# dxgmms2.sys — typed WDDM 2.x memory-manager/scheduler module boundary.
#
# The module owns administrative registration, adapter lifecycle, and the live
# fence-identity timeline plus the isolated logical context-stream core.
# Hardware dispatch, public scheduler policy, sync-object binding, and all
# VidMm ownership remain in dxgkrnl until later capability-gated integration.

spec2def(dxgmms2.sys dxgmms2.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    dxgmms2.c
    interface.c
    adapter.c
    timeline.c
    timeline_core.c
    context_stream.c
    context_stream_core.c
    scheduler.c
    scheduler_core.c)

add_library(dxgmms2 MODULE
    ${SOURCE}
    dxgmms2.rc
    ${CMAKE_CURRENT_BINARY_DIR}/dxgmms2.def)

set_module_type(dxgmms2 kernelmodedriver)
add_importlibs(dxgmms2 ntoskrnl)
add_pch(dxgmms2 dxgmms2_private.h "${SOURCE}")
add_cd_file(TARGET dxgmms2 DESTINATION reactos/system32/drivers FOR all)
