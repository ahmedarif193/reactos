add_definitions(-DUNICODE -D_UNICODE -D_NTOSKRNL_ -D_NTSYSTEM_)

list(APPEND SOURCE
    rtl.c)

add_library(rtlver ${SOURCE})
add_dependencies(rtlver bugcodes xdk)
if(ARCH STREQUAL "arm64ec" AND COMMAND set_arm64ec_native_target)
    add_library(rtlver_native ${SOURCE})
    set_arm64ec_native_target(rtlver_native)
    add_dependencies(rtlver_native bugcodes xdk)
endif()
