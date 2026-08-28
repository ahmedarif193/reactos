
add_subdirectory(ac97)
add_subdirectory(CMIDriver)
add_subdirectory(hdaudio)
if(RPI5_SUPPORT)
    add_subdirectory(rpi5hdmi)
endif()
