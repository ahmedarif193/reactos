
add_definitions(
    -DUNICODE -D_UNICODE)
if(DBG)
    add_compile_definitions(NDEBUG)
endif()

include_directories(${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/sound)

list(APPEND SOURCE
    controls.c
    filter.c
    midi.c
    mixer.c
    sup.c
    wave.c
    topology.c
    precomp.h)

add_library(mmixer ${SOURCE})
add_pch(mmixer precomp.h SOURCE)
add_dependencies(mmixer bugcodes xdk)
if(ARCH STREQUAL "arm64ec" AND COMMAND set_arm64ec_native_target)
    add_library(mmixer_native ${SOURCE})
    set_arm64ec_native_target(mmixer_native)
    add_pch(mmixer_native precomp.h SOURCE)
    add_dependencies(mmixer_native bugcodes xdk)
endif()
