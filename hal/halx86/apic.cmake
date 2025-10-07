
list(APPEND HAL_APIC_ASM_SOURCE
    ${HAL_I386_DIR}/apic/apictrap.S
    ${HAL_I386_DIR}/apic/tsccal.S)

list(APPEND HAL_APIC_SOURCE
    ${HAL_I386_DIR}/apic/apic.c
    ${HAL_I386_DIR}/apic/apictimer.c
    ${HAL_I386_DIR}/apic/halinit.c
    ${HAL_I386_DIR}/apic/processor.c
    ${HAL_I386_DIR}/apic/rtctimer.c
    ${HAL_I386_DIR}/apic/tsc.c)

add_asm_files(lib_hal_apic_asm ${HAL_APIC_ASM_SOURCE})
add_library(lib_hal_apic OBJECT ${HAL_APIC_SOURCE} ${lib_hal_apic_asm})
add_dependencies(lib_hal_apic asm)
