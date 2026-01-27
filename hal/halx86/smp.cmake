
list(APPEND HAL_SMP_SOURCE
    ${HAL_I386_DIR}/apic/apicsmp.c
    ${HAL_I386_DIR}/generic/buildtype.c
    ${HAL_I386_DIR}/generic/spinlock.c
    ${HAL_I386_DIR}/smp/ipi.c
    ${HAL_I386_DIR}/smp/smp.c
    ${HAL_COMMON_DIR}/generic/sysinfo_stubs.c)

if(HAL_ARCH STREQUAL "i386")
    list(APPEND HAL_SMP_ASM_SOURCE
        ${HAL_I386_DIR}/smp/i386/apentry.S)
    list(APPEND HAL_SMP_SOURCE
        ${HAL_I386_DIR}/smp/i386/spinup.c)
elseif(HAL_ARCH STREQUAL "amd64")
    list(APPEND HAL_SMP_ASM_SOURCE
        ${HAL_AMD64_DIR}/smp/amd64/apentry.S)
    list(APPEND HAL_SMP_SOURCE
        ${HAL_AMD64_DIR}/smp/amd64/spinup.c)
endif()

add_asm_files(lib_hal_smp_asm ${HAL_SMP_ASM_SOURCE})
add_library(lib_hal_smp OBJECT ${HAL_SMP_SOURCE} ${lib_hal_smp_asm})
add_dependencies(lib_hal_smp bugcodes asm xdk)
target_compile_definitions(lib_hal_smp PRIVATE CONFIG_SMP)
