# dxgmms2.sys — typed WDDM 2.x memory-manager/scheduler module boundary.
#
# The v1 module owns administrative registration and adapter lifecycle only.
# Functional scheduler and VidMm ownership remains in dxgkrnl until a later,
# capability-gated contract version is implemented and tested.

spec2def(dxgmms2.sys dxgmms2.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    dxgmms2.c
    interface.c
    adapter.c
    timeline.c
    timeline_core.c)

add_library(dxgmms2 MODULE
    ${SOURCE}
    dxgmms2.rc
    ${CMAKE_CURRENT_BINARY_DIR}/dxgmms2.def)

set_module_type(dxgmms2 kernelmodedriver)
add_importlibs(dxgmms2 ntoskrnl)
add_pch(dxgmms2 dxgmms2_private.h "${SOURCE}")
add_cd_file(TARGET dxgmms2 DESTINATION reactos/system32/drivers FOR all)
