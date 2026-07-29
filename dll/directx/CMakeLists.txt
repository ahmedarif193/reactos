
add_subdirectory(d3d8thk)
if(REACTOS_USE_WDDM)
    add_subdirectory(d3dumdrt)
    add_subdirectory(softgpuum)
    if((ARCH STREQUAL "arm64") AND RPI_SUPPORT)
        add_subdirectory(rpi5vc4um)
    endif()
endif()
#add_subdirectory(bdaplgin) #disabled in trunk
#add_subdirectory(d3d8) #disabled in trunk, see wine subfolder
#add_subdirectory(d3d9) #disabled in trunk, see wine subfolder
#add_subdirectory(ddraw) #disabled in trunk, see wine subfolder
#add_subdirectory(dsound_new) #disabled in trunk
#add_subdirectory(ksproxy) Not needed at the moment
add_subdirectory(ksuser)
#add_subdirectory(msdvbnp) #disabled in trunk
#add_subdirectory(msvidctl) #disabled in trunk
add_subdirectory(wine)
