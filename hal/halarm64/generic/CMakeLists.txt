# Generic ARM64 HAL

include_directories(
    ../include
    ${REACTOS_SOURCE_DIR}/ntoskrnl/include)

list(APPEND HAL_GENERIC_SOURCE
    bus.c
    gic.c
    halinit.c
    irql.c
    misc.c
    portio.c
    processor.c
    qspinlock.c
    spinlock.c
    stubs.c
    timer.c)

add_library(generic_arm64 OBJECT ${HAL_GENERIC_SOURCE})
add_dependencies(generic_arm64 asm)