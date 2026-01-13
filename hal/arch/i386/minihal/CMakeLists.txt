
# We need to define this here, because the target definitions are not
# passed to CL, when preprocessing the asm files on MSVC builds.
# See CORE-19847
add_definitions(-D_MINIHAL_)

list(APPEND MINI_HAL_SOURCE
    ../../common/legacy/bus/bushndlr.c
    ../../common/legacy/bus/cmosbus.c
    ../../common/legacy/bus/isabus.c
    ../../common/legacy/bus/pcibus.c
    ../../common/legacy/bussupp.c
    acpi_ecam_stubs.c
    ../generic/drive.c
    ../generic/misc.c
    ../generic/nmi.c
    ../generic/spinlock.c
    ../generic/timer.c
    ../generic/usage.c
    ../pic/pic.c
    ../include/hal.h
    halinit.c)

if(SARCH STREQUAL "xbox")
    list(APPEND MINI_HAL_SOURCE
        ../generic/beep.c
        ../generic/cmos.c
        ../xbox/clock.c
        ../xbox/reboot.c)
elseif(SARCH STREQUAL "pc98")
    list(APPEND MINI_HAL_SOURCE
        ../pc98/beep.c
        ../pc98/clock.c
        ../pc98/cmos.c
        ../pc98/reboot.c)
else()
    list(APPEND MINI_HAL_SOURCE
        ../generic/beep.c
        ../generic/clock.c
        ../generic/cmos.c
        ../generic/reboot.c)
endif()

add_asm_files(mini_hal_asm ../generic/systimer.S)
add_library(mini_hal ${MINI_HAL_SOURCE} ${mini_hal_asm})
target_compile_definitions(mini_hal PRIVATE _BLDR_ _NTSYSTEM_ MINIHAL_HAS_BUS_SUPPORT)

# Disable SSE/SSE2/AVX (FreeLoader runs before SSE is enabled in the CPU)
# Note: This file is already i386-specific (hal/arch/i386/minihal)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(mini_hal PRIVATE
        -mno-mmx
        -mno-sse
        -mno-sse2
        -mno-sse3
        -mno-ssse3
        -mno-sse4.1
        -mno-sse4.2
        -mno-avx
        -mno-avx2)
endif()

add_dependencies(mini_hal psdk bugcodes asm)
add_pch(mini_hal ../include/hal.h MINI_HAL_SOURCE)
