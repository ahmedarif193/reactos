
list(APPEND HAL_UP_SOURCE
    ${HAL_I386_DIR}/generic/buildtype.c
    ${HAL_I386_DIR}/generic/spinlock.c
    ${HAL_I386_DIR}/generic/up.c)

add_library(lib_hal_up OBJECT ${HAL_UP_SOURCE})
add_dependencies(lib_hal_up bugcodes xdk)
