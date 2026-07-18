
# dxgmms1 - DirectX GPU Memory Manager and Scheduler
#
# dxgmms1.sys is loaded by dxgkrnl.sys (not by PnP).  It provides GPU
# scheduling and video memory management services.  dxgkrnl calls the
# exported DxgkMmsRegister() to obtain the DXGMMS_INTERFACE dispatch
# table at initialisation time.
#
# Build order inside drivers/directx:
#   1. dxgmms1  (no upstream ReactOS deps beyond ntoskrnl/hal)
#   2. dxgkrnl  (links against dxgmms1 import library)

# dxgmms1 and dxgkrnl exchange WDDM structures by value. Keep both targets on
# the same WDDM 2.0 ABI instead of inheriting the tree-wide Vista default.
remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x5023)

spec2def(dxgmms1.sys dxgmms1.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    dxgmms1.c
    interface.c
    vidmm_interface.c
    scheduler.c
    paging.c
    preemption.c
    vidmm.c
    debug.c)

add_library(dxgmms1 MODULE
    ${SOURCE}
    dxgmms1.rc
    ${CMAKE_CURRENT_BINARY_DIR}/dxgmms1.def)

set_module_type(dxgmms1 kernelmodedriver)
add_importlibs(dxgmms1 ntoskrnl hal)
add_pch(dxgmms1 dxgmms1_private.h "${SOURCE}")
add_cd_file(TARGET dxgmms1 DESTINATION reactos/system32/drivers FOR all)
