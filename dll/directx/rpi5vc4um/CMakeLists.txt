# The user-mode half of the Raspberry Pi 5 VideoCore VII driver.
#
# A WDDM driver is two halves and rpi5vc4.sys is only one of them.  Without a
# user-mode driver the D3D runtime has nothing to load after it finds the
# adapter, so the kernel half can start correctly and still render nothing.
#
# Compiled at the same effective UMD interface version as d3dumdrt.  The
# in-tree VC4 UMD remains a WDDM 2.0 driver even when the configured OS header
# ceiling is newer; table length follows the UMD contract, not the KMD one.
remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0xF003)

spec2def(rpi5vc4um.dll rpi5vc4um.spec)

add_library(rpi5vc4um MODULE
    rpi5vc4um.c
    ${CMAKE_CURRENT_BINARY_DIR}/rpi5vc4um.def)

set_module_type(rpi5vc4um win32dll ENTRYPOINT DllMain 12)
add_importlibs(rpi5vc4um msvcrt kernel32 ntdll)
add_dependencies(rpi5vc4um psdk)
add_cd_file(TARGET rpi5vc4um DESTINATION reactos/system32 FOR all)
