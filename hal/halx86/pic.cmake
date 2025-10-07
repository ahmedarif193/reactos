
list(APPEND HAL_PIC_ASM_SOURCE
    ${HAL_I386_DIR}/generic/systimer.S
    ${HAL_I386_DIR}/generic/trap.S
    ${HAL_I386_DIR}/pic/pic.S)

list(APPEND HAL_PIC_SOURCE
    ${HAL_I386_DIR}/generic/clock.c
    ${HAL_I386_DIR}/generic/profil.c
    ${HAL_I386_DIR}/generic/timer.c
    ${HAL_I386_DIR}/pic/halinit.c
    ${HAL_I386_DIR}/pic/irql.c
    ${HAL_I386_DIR}/pic/pic.c
    ${HAL_I386_DIR}/pic/processor.c)

add_asm_files(lib_hal_pic_asm ${HAL_PIC_ASM_SOURCE})
add_library(lib_hal_pic OBJECT ${HAL_PIC_SOURCE} ${lib_hal_pic_asm})
add_dependencies(lib_hal_pic asm)
