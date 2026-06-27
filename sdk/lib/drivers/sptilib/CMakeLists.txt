
list(APPEND SOURCE
    sptilib.c)

add_library(sptilib ${SOURCE})
target_link_libraries(sptilib PRIVATE ${PSEH_LIB})
add_dependencies(sptilib bugcodes xdk)
if(ARCH STREQUAL "arm64ec" AND COMMAND set_arm64ec_native_target)
    add_library(sptilib_native ${SOURCE})
    set_arm64ec_native_target(sptilib_native)
    target_link_libraries(sptilib_native PRIVATE ${PSEH_LIB})
    add_dependencies(sptilib_native bugcodes xdk)
endif()
