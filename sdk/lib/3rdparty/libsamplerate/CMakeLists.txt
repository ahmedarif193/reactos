
list(APPEND SOURCE
    samplerate.c
    src_linear.c
    src_sinc.c
    src_zoh.c
    precomp.h)

add_library(libsamplerate ${SOURCE})
add_dependencies(libsamplerate psdk)
add_pch(libsamplerate precomp.h SOURCE)
if(ARCH STREQUAL "arm64ec" AND COMMAND set_arm64ec_native_target)
    add_library(libsamplerate_native ${SOURCE})
    set_arm64ec_native_target(libsamplerate_native)
    add_dependencies(libsamplerate_native psdk)
    add_pch(libsamplerate_native precomp.h SOURCE)
endif()
