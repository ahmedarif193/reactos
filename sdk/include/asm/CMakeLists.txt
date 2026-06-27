
add_library(genincdata MODULE genincdata.c)
target_compile_definitions(genincdata PRIVATE NTDDI_VERSION=${REACTOS_TARGET_NTDDI})
if(ARCH STREQUAL "arm64ec")
    target_compile_options(genincdata PRIVATE
        -U_AMD64_ -U_M_AMD64 -U_M_X64 -U_ARM64EC_ -U_M_ARM64EC -U__arm64ec__ -U__x86_64__
        -U__x86_64 -U__amd64__ -D_ARM64_ -D_M_ARM64 -D__aarch64__)
endif()

set_entrypoint(genincdata 0)
add_dependencies(genincdata bugcodes psdk xdk)

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
elseif(ARCH_USES_ARM64_CODEGEN)
    set(_filename ksarm64.h)
endif()

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${_filename}
    COMMAND native-geninc $<TARGET_FILE:genincdata> ${CMAKE_CURRENT_BINARY_DIR}/${_filename} ${OPT_MS}
    DEPENDS genincdata native-geninc)

add_custom_target(asm
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${_filename})
