
if(NOT ARCH STREQUAL "riscv64")
    add_subdirectory(bochs)
    add_subdirectory(vbe)
    add_subdirectory(vga)
endif()
#add_subdirectory(vga_new)
add_subdirectory(uefifb)

if(REACTOS_USE_XPDM)
    add_driver_inf(uefifb uefifb/uefifb.inf)
    add_registry_inf(uefifb/uefifb_reg.inf)
endif()

if(REACTOS_USE_XPDM)
    if(NOT ARCH STREQUAL "riscv64")
        add_subdirectory(vmx_svga)
    endif()
    add_subdirectory(virtgpu)

    if((ARCH STREQUAL "arm64") AND RPI5_SUPPORT)
        add_subdirectory(rpi5vc4)
    endif()

    if(ARCH STREQUAL "i386")
        add_subdirectory(pc98vid)
        add_subdirectory(xboxvmp)
    endif()

endif()
