
list(APPEND HAL_GENERIC_SOURCE
    ${HAL_I386_DIR}/generic/beep.c
    ${HAL_I386_DIR}/generic/cmos.c
    ${HAL_I386_DIR}/generic/display.c
    ${HAL_I386_DIR}/generic/dma.c
    ${HAL_I386_DIR}/generic/drive.c
    ${HAL_I386_DIR}/generic/halinit.c
    ${HAL_I386_DIR}/generic/kdpci.c
    ${HAL_I386_DIR}/generic/memory.c
    ${HAL_I386_DIR}/generic/misc.c
    ${HAL_I386_DIR}/generic/nmi.c
    ${HAL_I386_DIR}/generic/pic.c
    ${HAL_I386_DIR}/generic/reboot.c
    ${HAL_I386_DIR}/generic/sysinfo.c
    ${HAL_I386_DIR}/generic/usage.c
    ${HAL_I386_DIR}/generic/x86bios.c)

if(HAL_ARCH STREQUAL "i386")
    list(APPEND HAL_GENERIC_SOURCE
        ${HAL_I386_DIR}/generic/bios.c
        ${HAL_I386_DIR}/generic/setjmp_shim.c
        ${HAL_I386_DIR}/generic/portio.c)

    list(APPEND HAL_GENERIC_ASM_SOURCE
        ${HAL_I386_DIR}/generic/v86.S)
endif()

add_asm_files(lib_hal_generic_asm ${HAL_GENERIC_ASM_SOURCE})
add_library(lib_hal_generic OBJECT ${HAL_GENERIC_SOURCE} ${lib_hal_generic_asm})
add_dependencies(lib_hal_generic asm)
