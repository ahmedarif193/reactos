list(APPEND SOURCE
    hardware.cpp
    miniport.cpp
    rpi5hdmi.cpp
    stream.cpp
    guid.cpp
    purecall.cpp)

add_library(rpi5hdmi MODULE
    ${SOURCE}
    rpi5hdmi.rc)

set_module_type(rpi5hdmi kernelmodedriver)
target_link_libraries(rpi5hdmi ntoskrnl_vista libcntpr ${PSEH_LIB})
add_importlibs(rpi5hdmi ntoskrnl hal portcls)
add_cd_file(TARGET rpi5hdmi DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(rpi5hdmi rpi5hdmi.inf)
