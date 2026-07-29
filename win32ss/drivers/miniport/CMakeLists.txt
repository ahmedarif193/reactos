
add_subdirectory(bochs)
add_subdirectory(vbe)
add_subdirectory(vga)
#add_subdirectory(vga_new)
add_subdirectory(uefifb)

if(REACTOS_USE_XPDM)
    add_subdirectory(vmx_svga)
    add_subdirectory(virtgpu)

    if((ARCH STREQUAL "arm64") AND RPI_SUPPORT)
        add_subdirectory(rpi5vc4)
    endif()

    if(ARCH STREQUAL "i386")
        add_subdirectory(pc98vid)
        add_subdirectory(xboxvmp)
    endif()
endif()
