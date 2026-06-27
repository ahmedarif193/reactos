
add_definitions(-D_NTSYSTEM_)

add_library(sdbuslib sdbus.c)
add_dependencies(sdbuslib xdk)
if(ARCH STREQUAL "arm64ec" AND COMMAND set_arm64ec_native_target)
    add_library(sdbuslib_native sdbus.c)
    set_arm64ec_native_target(sdbuslib_native)
    add_dependencies(sdbuslib_native xdk)
endif()
