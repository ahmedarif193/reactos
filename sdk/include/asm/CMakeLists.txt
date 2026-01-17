
add_library(genincdata MODULE genincdata.c)

set_entrypoint(genincdata 0)
add_dependencies(genincdata bugcodes psdk xdk)

# Workaround: rsym has a memory corruption bug when processing this DLL
# that causes it to be killed by macOS. Since this is just a code generator
# tool that doesn't need runtime symbol debugging, skip rsym for this target.
set_property(TARGET genincdata PROPERTY NO_ROSSYM TRUE)

if(MSVC)
    set(OPT_MS "-ms")
else()
    set(OPT_MS "")
endif()

if(ARCH STREQUAL "i386")
    set(_filename ks386.inc)
elseif(ARCH STREQUAL "amd64")
    set(_filename ksamd64.inc)
elseif(ARCH STREQUAL "arm")
    set(_filename ksarm.h)
elseif(ARCH STREQUAL "arm64")
    set(_filename ksarm64.h)
endif()

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${_filename}
    COMMAND native-geninc $<TARGET_FILE:genincdata> ${CMAKE_CURRENT_BINARY_DIR}/${_filename} ${OPT_MS}
    DEPENDS genincdata native-geninc)

add_custom_target(asm
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${_filename})
