
# Generic
list(APPEND HAL_PC98_SOURCE
    ${HAL_I386_DIR}/pc98/beep.c
    ${HAL_I386_DIR}/pc98/clock.c
    ${HAL_I386_DIR}/pc98/cmos.c
    ${HAL_I386_DIR}/pc98/delay.c
    ${HAL_I386_DIR}/pc98/pic.c
    ${HAL_I386_DIR}/pc98/profil.c
    ${HAL_I386_DIR}/pc98/reboot.c
    ${HAL_I386_DIR}/generic/bios.c
    ${HAL_I386_DIR}/generic/setjmp_shim.c
    ${HAL_I386_DIR}/generic/apic_stubs.c
    ${HAL_I386_DIR}/generic/display.c
    ${HAL_COMMON_DIR}/generic/dma.c
    ${HAL_I386_DIR}/generic/drive.c
    ${HAL_I386_DIR}/generic/halinit.c
    ${HAL_I386_DIR}/generic/kdpci.c
    ${HAL_I386_DIR}/generic/memory.c
    ${HAL_I386_DIR}/generic/misc.c
    ${HAL_I386_DIR}/generic/nmi.c
    ${HAL_I386_DIR}/generic/portio.c
    ${HAL_I386_DIR}/generic/sysinfo.c
    ${HAL_I386_DIR}/generic/usage.c
    ${HAL_I386_DIR}/generic/x86bios.c)

list(APPEND HAL_PC98_ASM_SOURCE
    ${HAL_I386_DIR}/generic/v86.S)

# PIC
list(APPEND HAL_PC98_SOURCE
    ${HAL_I386_DIR}/pc98/irql.c
    ${HAL_I386_DIR}/generic/timer.c
    ${HAL_I386_DIR}/pic/halinit.c
    ${HAL_I386_DIR}/pic/pic.c
    ${HAL_I386_DIR}/pic/processor.c)

list(APPEND HAL_PC98_ASM_SOURCE
    ${HAL_I386_DIR}/generic/trap.S
    ${HAL_I386_DIR}/pic/pic.S)

# Legacy
list(APPEND HAL_PC98_SOURCE
    ${HAL_COMMON_DIR}/legacy/bus/bushndlr.c
    ${HAL_COMMON_DIR}/legacy/bus/cmosbus.c
    ${HAL_COMMON_DIR}/legacy/bus/isabus.c
    ${HAL_COMMON_DIR}/legacy/bus/sysbus.c
    ${HAL_COMMON_DIR}/legacy/bussupp.c
    ${HAL_COMMON_DIR}/legacy/bus/pcibus.c
    ${HAL_I386_DIR}/legacy/acpi_ecam_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/pci_classes.c
    ${CMAKE_CURRENT_BINARY_DIR}/pci_vendors.c
    ${HAL_I386_DIR}/legacy/halpnpdd.c
    ${HAL_I386_DIR}/legacy/halpcat.c)

add_asm_files(lib_hal_pc98_asm ${HAL_PC98_ASM_SOURCE})
add_library(lib_hal_pc98 OBJECT ${HAL_PC98_SOURCE} ${lib_hal_pc98_asm})
add_dependencies(lib_hal_pc98 bugcodes xdk asm)
#add_pch(lib_hal_pc98 pc98/halpc98.h)

target_compile_definitions(lib_hal_pc98 PRIVATE SARCH_PC98)
