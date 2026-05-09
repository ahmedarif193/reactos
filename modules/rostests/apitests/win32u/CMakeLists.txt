
# TODO(ARM64): Port win32u syscall stubs (sys-stubs.S uses x86/amd64 MASM syntax)
if(NOT ARCH STREQUAL "arm64")
add_subdirectory(win32u_ros)
add_subdirectory(win32u_xpsp2)
add_subdirectory(win32u_2k3sp2)
add_subdirectory(win32u_vista)
endif()
