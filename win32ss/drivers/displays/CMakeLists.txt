
if(REACTOS_USE_WDDM)
    add_subdirectory(cdd)
endif()
add_subdirectory(framebuf)
if(ARCH STREQUAL "arm64")
    add_subdirectory(rpi5vc4)
endif()
add_subdirectory(vga)
