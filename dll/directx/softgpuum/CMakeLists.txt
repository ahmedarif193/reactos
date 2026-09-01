
# A user-mode display driver is a WDDM component and must be compiled at the
# WDDM interface version, exactly as drivers/directx does for the kernel half.
# D3D_UMD_INTERFACE_VERSION is selected tree-wide from the configured WDDM
# level, capped at this driver's WDDM 2.1 implementation.  The KMD header
# selector remains at the audited current layout so shared KMT types are
# available without making the UMD advertise a newer table.
remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x11007)

spec2def(softgpuum.dll softgpuum.spec)

add_library(softgpuum MODULE
    softgpuum.c
    ${CMAKE_CURRENT_BINARY_DIR}/softgpuum.def)

set_module_type(softgpuum win32dll ENTRYPOINT DllMain 12)
add_importlibs(softgpuum msvcrt kernel32 ntdll)
add_dependencies(softgpuum psdk)
add_cd_file(TARGET softgpuum DESTINATION reactos/system32 FOR all)
