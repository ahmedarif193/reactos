add_definitions(-DUNICODE -D_UNICODE)

list(APPEND SOURCE
    copysup.c)

add_library(copysup ${SOURCE})
target_link_libraries(copysup PRIVATE pseh)
add_dependencies(copysup bugcodes xdk)
if(ARCH STREQUAL "arm64ec" AND COMMAND set_arm64ec_native_target)
    add_library(copysup_native ${SOURCE})
    set_arm64ec_native_target(copysup_native)
    target_link_libraries(copysup_native PRIVATE pseh)
    add_dependencies(copysup_native bugcodes xdk)
endif()
