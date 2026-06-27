
add_library(strmiids strmiids.c)
if(ARCH STREQUAL "arm64ec" AND COMMAND set_arm64ec_native_target)
    add_library(strmiids_native strmiids.c)
    set_arm64ec_native_target(strmiids_native)
endif()
