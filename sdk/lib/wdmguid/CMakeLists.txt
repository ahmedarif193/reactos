
add_library(wdmguid wdmguid.c)
if(ARCH STREQUAL "arm64ec" AND COMMAND set_arm64ec_native_target)
    add_library(wdmguid_native wdmguid.c)
    set_arm64ec_native_target(wdmguid_native)
endif()
