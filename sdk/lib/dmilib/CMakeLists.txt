
list(APPEND SOURCE
    dmilib.c
    precomp.h)

add_library(dmilib ${SOURCE})
add_pch(dmilib precomp.h SOURCE)
add_dependencies(dmilib psdk)
if(ARCH STREQUAL "arm64ec" AND COMMAND set_arm64ec_native_target)
    add_library(dmilib_native ${SOURCE})
    set_arm64ec_native_target(dmilib_native)
    add_pch(dmilib_native precomp.h SOURCE)
    add_dependencies(dmilib_native psdk)
endif()
